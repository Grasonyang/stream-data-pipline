# `pipeline_dispatcher` — 開發文件 v1.0

> **對應 repo**：`socket-data-analyzer-c`
> **版本**：v1.0 / 2026-05-11
> **定位**：`pipeline_dispatcher` 是 C 子系統的唯一進入點（entry point），負責接收上層 Node.js 的呼叫、建立 UNIX pipe 管線、以 fork/exec 方式啟動三個子工具，並監控其生命週期。

***

## 一、命名說明

| 舊名稱 | 新名稱 | 命名理由 |
|---|---|---|
| `deal_with_ws_data` | `pipeline_dispatcher` | 符合業界 dispatcher pattern 慣例；明確表達「建立管線並派發子工具」的職責，而非資料處理本身 |

參考業界同類命名：Linux kernel 的 `work_queue_dispatch`、Node.js 的 `child_process` dispatcher、以及 ffmpeg 的 `avfilter_graph_dispatch`，皆以 `dispatch` 表達「協調並觸發執行」而非「直接處理」。[^1]

***

## 二、函式簽章

```c
/**
 * pipeline_dispatcher - 建立完整的資料處理管線
 *
 * @param session_id  WS session 識別碼（來自 Fastify 上層）
 * @param src_dir     chunk 檔案落地目錄（由 API Server writeStream 寫入）
 * @param db_path     clip-store 索引檔路徑
 * @param ttl_seconds clip 索引的 TTL（秒）
 *
 * @return  0  成功（三個子進程皆正常退出）
 *         -1  管線建立失敗（pipe/fork 錯誤）
 *         -2  子進程異常退出
 */
int pipeline_dispatcher(
    const char *session_id,
    const char *src_dir,
    const char *db_path,
    int         ttl_seconds
);
```

***

## 三、職責邊界

`pipeline_dispatcher` **只做以下三件事**，不涉及任何資料邏輯：

1. **建立 IPC 通道**：呼叫 `pipe()` 建立兩條 pipe，將三個子工具的 stdin/stdout 串接
2. **fork + exec 子工具**：依序啟動 `stream-merge`、`log-parse`、`clip-store`
3. **生命週期監控**：用 `waitpid()` 等待並收集子進程退出狀態，異常時記錄錯誤

```
pipeline_dispatcher()
    │
    ├── pipe(fd1)  ← stream-merge stdout → log-parse stdin
    ├── pipe(fd2)  ← log-parse stdout    → clip-store stdin
    │
    ├── fork() → exec stream-merge  [stdout → fd1[^1]]
    ├── fork() → exec log-parse     [stdin ← fd1, stdout → fd2[^1]]
    ├── fork() → exec clip-store    [stdin ← fd2]
    │
    └── waitpid() × 3 → 收集退出狀態
```

***

## 四、與上層的連動（Fastify → C）

Fastify 透過 Node.js `child_process` 模組以 **process 方式呼叫** `pipeline_dispatcher`，而非直接 FFI 呼叫。

```
Fastify（Node.js）
  │
  │  const proc = spawn('./pipeline_dispatcher', [
  │      session_id, src_dir, db_path, String(ttl)
  │  ]);
  │
  ▼
pipeline_dispatcher（獨立 process）
  ├── 建立管線
  ├── 啟動三個子工具
  └── 等待完成後 exit(0)
```

**連動時序**：

```
時間軸
  t=0    Fastify 收到 WS video-in frame，開始 writeStream 寫入 chunk
  t=0    Fastify 同步 spawn pipeline_dispatcher（不阻塞 WS 接收）
  t=0+   pipeline_dispatcher 建立 pipe + fork 三個子工具
  t=1    stream-merge 的 inotify 開始監聽 src_dir，第一個 chunk 到達即觸發
  t=5    stream-merge 累積滿 5s，輸出第一個 clip JSON → pipe1 → log-parse
  t=5+   log-parse 過濾後 → pipe2 → clip-store 寫入索引
  t=N    Fastify WS 連線中斷 / session 結束 → pipeline_dispatcher 偵測到子進程退出
```

***

## 五、與三個子工具的連動

### 5.1 整體 pipe 拓樸

```
[stream-merge]
  stdout ──pipe1──▶ [log-parse]
                        stdout ──pipe2──▶ [clip-store]
                                               │
                                               ▼
                                        /tmp/clips.db（索引）
                                        /tmp/clips/{session}/（clip 檔案）
```

每一層工具對下一層完全透明：

- `stream-merge` 不知道 `log-parse` 的存在，只管把 JSON Lines 打到 stdout
- `log-parse` 不知道輸入來自哪個程式，只管從 stdin 讀並過濾
- `clip-store` 不知道上游是誰，只管把 stdin 的 JSON 寫入索引

這是標準的 **UNIX filter pipeline 設計原則**。[^1]

### 5.2 各子工具啟動參數（由 pipeline_dispatcher 傳入）

| 子工具 | 關鍵啟動參數來源 | 說明 |
|---|---|---|
| `stream-merge` | `session_id`、`src_dir` | 監聽哪個目錄、輸出哪個 session 的 clip |
| `log-parse` | 固定（`--filter type=clip`） | 只過濾 clip 類型的輸出 |
| `clip-store` | `db_path`、`ttl_seconds` | 索引寫到哪裡、TTL 多長 |

### 5.3 異常傳播路徑

```
stream-merge 異常退出
  → pipe1 write end 關閉
  → log-parse 讀到 EOF，正常退出
  → pipe2 write end 關閉
  → clip-store 讀到 EOF，正常退出
  → pipeline_dispatcher 的 waitpid() 收到所有退出狀態
  → pipeline_dispatcher 回傳 -2，Fastify 收到非零 exit code
```

這個「cascade close」機制確保任一工具崩潰時，整條管線能**自動清理**，不會有孤兒進程殘留。[^1]

***

## 六、錯誤處理規範

`pipeline_dispatcher` 必須處理以下錯誤情境：

| 錯誤情境 | 處理方式 | 回傳值 |
|---|---|---|
| `pipe()` 失敗 | `perror("pipe")` + 清理 fd | -1 |
| `fork()` 失敗 | `perror("fork")` + kill 已啟動的子進程 | -1 |
| `exec` 找不到執行檔 | 子進程以 `exit(127)` 退出，父進程透過 waitpid 偵測 | -2 |
| 子進程被 signal 終止（SIGKILL 等） | `WIFSIGNALED(status)` 判斷，記錄 signal 號碼 | -2 |
| 子進程正常但 exit code 非零 | `WEXITSTATUS(status)` 記錄，視為業務錯誤 | -2 |

***

## 七、分工界面（同步開發說明）

`pipeline_dispatcher` 的設計允許三個子工具**完全獨立開發與測試**，不需要等待彼此完成。

### 開發介面契約

每個子工具的契約只有兩件事：

```
stream-merge 的契約：
  INPUT  → 監聽 --src 目錄的 inotify 事件（不依賴 pipeline_dispatcher）
  OUTPUT → stdout 輸出符合規格的 JSON Lines

log-parse 的契約：
  INPUT  → stdin 讀取 JSON Lines
  OUTPUT → stdout 輸出過濾後的 JSON Lines

clip-store 的契約：
  INPUT  → stdin 讀取 clip JSON Lines
  OUTPUT → 寫入 --db 檔案 + stdout 輸出操作確認
```

### 獨立測試方式（不需要 pipeline_dispatcher）

```bash
# 測試 stream-merge 單獨運行
./stream-merge --src /tmp/stream/sess_test --session sess_test | cat

# 測試 log-parse 單獨運行
echo '{"type":"clip","ts":1000,"path":"/tmp/x.mp4"}' | ./log-parse --filter type=clip

# 測試 clip-store 單獨運行
echo '{"type":"clip","session_id":"s1","ts":1000,"path":"/tmp/x.mp4"}' \
  | ./clip-store --db /tmp/test.db --ttl 300

# 測試完整管線（等同 pipeline_dispatcher 的行為）
./stream-merge --src /tmp/stream/sess_test --session sess_test \
  | ./log-parse --filter type=clip \
  | ./clip-store --db /tmp/test.db --ttl 300
```

### 分工建議

| 成員 | 負責範圍 | 依賴 |
|---|---|---|
| 成員 A | `pipeline_dispatcher.c` + `libpipeline.c` 基礎建設 | 無，最先開始 |
| 成員 B | `stream-merge`（核心邏輯） | 只需要 `libpipeline.h` 介面 |
| 成員 C | `log-parse` + `clip-store` | 只需要 JSON Lines 格式規格 |
| 成員 D | 測試腳本 + benchmark + 文件 | 需要各工具 CLI 介面穩定後 |

***

## 八、與 Fastify 上層的錯誤回報

`pipeline_dispatcher` 作為獨立 process 執行，Fastify 透過 exit code 判斷結果：

```js
// Fastify 側的呼叫與錯誤處理
const proc = spawn('./pipeline_dispatcher', [session_id, src_dir, db_path, String(ttl)]);

proc.on('exit', (code) => {
  if (code === 0) {
    logger.info({ session_id }, 'pipeline completed successfully');
  } else {
    logger.error({ session_id, exit_code: code }, 'pipeline_dispatcher failed');
    // 觸發告警或重試邏輯
  }
});

proc.stderr.on('data', (data) => {
  logger.warn({ session_id, msg: data.toString() }, 'pipeline stderr');
});
```

`pipeline_dispatcher` 透過 **stderr** 輸出診斷訊息，**stdout 不使用**（保留給子工具管線的 pass-through 調試模式）。

***

## 九、檔案對應

```
socket-data-analyzer-c/
├── applets/
│   └── pipeline_dispatcher.c   ← 本文件描述的主體
├── lib/
│   ├── libpipeline.h            ← pipeline_dispatcher 依賴的共用介面
│   └── libpipeline.c
└── tests/
    └── test_pipeline_dispatcher.sh
```

***

*本文件對應 `socket-data-analyzer-c` repo，`pipeline_dispatcher` 模組。*
*v1.0 — 2026-05-11*

---

## References

1. [data_analyzer_archi.md](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/143763799/57352e78-c91d-4b6e-9a40-b61d6850d444/data_analyzer_archi.md?AWSAccessKeyId=ASIA2F3EMEYE7ZKO2NN2&Signature=Stf3dvQPt7toGzYjsv%2BEyepqT7I%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEE8aCXVzLWVhc3QtMSJHMEUCIE3xtV8gAMi2j7eMwruDgt%2FVNC0edH1d8je5nRxexp6SAiEAnGNy4WUttK7f371mBrKiPBnSa94hsW98R1c5PRCZtosq8wQIGBABGgw2OTk3NTMzMDk3MDUiDIWQfoyKc4a%2FdopOpSrQBFRhM1GekPmPDmXUwu03SX4CWachmgoDIVAMmtZ0YWPhussVQoCyk%2B1pWTZCLyhj8PpZ67RdCw%2Fp8S8KhHuN9YuFspwGQoH2XreQWkQaGT3DhXjzqc92RvkEk0qdFx359M0tdj2GR8CjDGXyiuN3mqFz7%2BFTFxcVlaxi6ii2B8UXOKIsURb6uNoMAO4ABq0Qc4aUzzD%2FOn0btxb%2FLQUzNsfgjRY53H3cscxyXf%2BqLXvnSKGI4NJ9WMYrBq4Tln5OWMEb3ypPLSLLPkFuLJFX5pHpVIVQ%2FsN6XIGGXnPTy5uEzxjKMeaV0zc0kKoqoEtIro1iYLtZic45PAN12A8S9Xk2%2B1dCI7oTmVON%2BCzF8DdWObFt7CLISdg9kW%2FDyirmsO8dLCpBv8GmNl3amKGvQxAc%2BDY%2F%2BOUqVjyLashy96mRppuIRjYQgHXZ%2B15vLMV0fJQlt8Xq7ibHbv1n%2BNwm3fZ2RWi%2B9amWA09Majnqs6KF12dfk7ibCEWE2wBQAhvVMVUF9httHVeSgyu7ToVqZLZ%2FhqEj3e7eaMkXNIS55giTY8GssD0SfWu5vEeXw3QcDFkVq60Mmm6yu%2BANNMIZHCWOpMEcbDFe%2BbF7VqORgj3BWyQcTPZWBydQjFcqFO5YkW%2FnMHCTmfQBwjpYnrdiX0CesGVJuJn9DF%2Fr0D8xQ%2BpRTAOyDobQ0AVnsmwI4l4KQQq3fWTgYEjEuXgMGCvCDnXmyY7icrSJPu1mzRsYzf%2FmVxSqFizalbUTfywQq2oVYi1kl3D1g4yohd5u%2Bh4%2FYTQw%2B9uH0AY6mAFKpOXA3uBQJrmtfV47Gt1QTQMYW%2B0YWTohL%2FkqTyBgj%2BB740X5Til0M6AoHm%2B9pBYjfW%2BA7mNOfSbuBkb5p0MDTASBogEfZySS7a44UpuUgT%2FHY2ZS03yKAX%2F%2F4SQsq6T%2FLrxhgncVh4Z1KWPLwzxiCiq4Pdkp6ApzkgSjeBwL4am%2FtKew2G7G%2Fl3kWOo7WO%2B4SG0Qqx40Lg%3D%3D&Expires=1778514894) - # Data Analyzer — 獨立開發架構說明

> **版本**：v2.0
> **日期**：2026-04-29
> **定位**：Data Analyzer 是可獨立開發、測試的實時串流處...


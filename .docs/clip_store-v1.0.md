# `clip-store` — 細部開發文件 v1.0

> **對應檔案**：`applets/clip_store.c`
> **版本**：v1.0 / 2026-05-11
> **定位**：`clip-store` 是管線的**終端持久化層**。從 stdin 讀取 `log-parse` 輸出的 clip JSON Lines，以 `session_id:ts` 為 key 寫入純文字 key-value 資料庫，支援 TTL 自動過期、CRUD 查詢、以及 GC 清理。不依賴 SQLite 或任何外部函式庫。

***

## 一、職責總覽

`clip-store` 扮演兩個截然不同的角色，由 CLI 模式切換：

| 模式 | 觸發方式 | 職責 |
|---|---|---|
| **Pipe 寫入模式** | stdin 有資料（管線中使用） | 讀 stdin → 寫入 db → 輸出操作確認到 stdout |
| **查詢/管理模式** | `--get`、`--list`、`--delete`、`--gc` 任一參數 | 直接操作 db 檔，stdin 不讀取 |

兩種模式**互斥**：若同時帶有 `--list` 又有 stdin 資料，以查詢模式優先，stdin 被忽略。

***

## 二、程式進入點與初始化流程

```
main(argc, argv)
    │
    ├── [^1] parse_args()         — 解析 CLI，判斷執行模式
    ├── [^2] validate_config()    — --db 必填；--ttl 必須 > 0
    ├── db_open()            — 開啟或建立 db 檔案（O_CREAT | O_RDWR）
    │       └── db_lock()        — flock(LOCK_EX)，確保同時只有一個 writer
    │
    ├── [4a] 若為 Pipe 寫入模式 → run_pipe_loop()
    └── [4b] 若為查詢/管理模式 → run_query(config)
                                    └── db_unlock() + db_close() → exit
```

`db_open()` 使用 `flock(LOCK_EX)` 對整個 db 檔案加鎖，防止多個 `clip-store` 實例同時寫入產生競態條件 。[^1]

***

## 三、資料庫格式設計

### 3.1 儲存格式（純文字，tab 分隔）

```
# /tmp/clips.db
# 格式：key<TAB>value<TAB>expire_at<NEWLINE>
# expire_at 為 Unix timestamp（秒）；0 表示永不過期
sess_001:1747065600	/tmp/clips/sess_001/1747065600_merged.mp4	1747069200
sess_001:1747065605	/tmp/clips/sess_001/1747065605_merged.mp4	1747069205
sess_002:1747065700	/tmp/clips/sess_002/1747065700_partial.mp4	1747069300
```

**設計理由**：
- 純文字易於 `cat`、`grep`、`awk` 直接操作，符合嵌入式工具精神
- TAB 分隔避免路徑中的空格造成解析歧義
- 不需要 SQLite，無額外 .so 依賴，適合 BusyBox 嵌入式環境

### 3.2 Key 命名規則

```
key = session_id + ":" + ts

範例：
sess_001:1747065600     ← session sess_001，clip 起始時間戳 1747065600
```

`session_id` 最長 63 字元，`ts` 為 10 位 Unix timestamp（秒），總長上限 74 字元 。[^1]

***

## 四、核心資料結構

### 4.1 StoreConfig（CLI 參數對應）

```c
typedef struct {
    char    db_path[PATH_MAX];  /* --db */
    int     ttl_seconds;        /* --ttl（0 = 永不過期） */
    char    get_key;       /* --get <key> */
    char    filter_expr;   /* --list --filter <expr> */
    char    delete_key;    /* --delete <key> */
    int     do_gc;              /* --gc flag */
    int     mode;               /* PIPE | GET | LIST | DELETE | GC */
} StoreConfig;
```

### 4.2 DbEntry（單一記錄的內存表示）

```c
typedef struct {
    char    key;       /* session_id:ts */
    char    value[PATH_MAX];/* clip 檔案路徑 */
    time_t  expire_at;      /* 0 = 永不過期 */
} DbEntry;
```

### 4.3 DbHandle（開啟的資料庫狀態）

```c
typedef struct {
    int     fd;             /* open() 取得的 file descriptor */
    char    path[PATH_MAX]; /* db 檔案路徑 */
    int     is_locked;      /* flock 狀態 */
    size_t  entry_count;    /* 目前記錄數（僅 cache，非即時） */
} DbHandle;
```

***

## 五、Pipe 寫入模式（`run_pipe_loop`）

```
run_pipe_loop(config, db)
    │
    └── while (fgets(line, MAX_LINE, stdin) != NULL)
            │
            ├── [A] json_parse_line(line) → ClipJson
            │       ├── 解析失敗 → LOG_WARN + continue
            │       └── 缺少 session_id 或 ts → LOG_WARN + continue
            │
            ├── [B] build_entry(clip_json, config->ttl_seconds) → DbEntry
            │       ├── entry.key = clip_json.session_id + ":" + clip_json.ts
            │       ├── entry.value = clip_json.path
            │       └── entry.expire_at = time(NULL) + ttl_seconds
            │
            ├── [C] db_set(db, &entry)
            │       ├── 檢查 key 是否已存在（duplicate）
            │       │       └── 存在 → 覆蓋（upsert 語意）
            │       └── append 新行到 db 檔案尾端（O_APPEND 保證原子性）
            │
            └── [D] emit_confirm(entry.key)
                    └── printf("{\"op\":\"set\",\"key\":\"%s\",\"ok\":true}\n", key)
                        fflush(stdout)
```

**Upsert 語意**：若同一 `key` 已存在，以新值覆蓋。實作上採 append-only 寫入，在 `--gc` 或下次 `db_load()` 時去重（保留最後一筆）。[^1]

***

## 六、查詢/管理模式

### 6.1 `--get <key>`

```
db_get(db, key)
    ├── db_load_all()        — 讀取整個 db 檔到記憶體（DbEntry 陣列）
    ├── 線性搜尋 key
    ├── 若找到且未過期 → printf("{\"key\":\"...\",\"value\":\"...\",\"expire_at\":...}\n")
    ├── 若找到但已過期 → printf("{\"key\":\"...\",\"expired\":true}\n")，exit 1
    └── 若不存在       → printf("{\"key\":\"...\",\"found\":false}\n")，exit 1
```

### 6.2 `--list [--filter <expr>]`

```
db_list(db, filter_expr)
    ├── db_load_all()
    ├── 過濾過期項目（expire_at != 0 && expire_at < now()）
    ├── 若有 --filter：套用 filter_expr（語法與 log-parse 相同）
    └── for each 符合項目：
            printf("{\"key\":\"...\",\"value\":\"...\",\"ttl_remaining\":%d}\n", ...)
```

`ttl_remaining` 為 `expire_at - time(NULL)`，方便上層判斷 clip 還有多久過期 。[^1]

### 6.3 `--delete <key>`

```
db_delete(db, key)
    ├── db_load_all()
    ├── 從記憶體陣列移除指定 key
    ├── db_rewrite_all()     — 將更新後的記錄重寫整個 db 檔（原子性：先寫 .tmp 再 rename）
    └── printf("{\"op\":\"delete\",\"key\":\"...\",\"ok\":true}\n")
```

### 6.4 `--gc`（Garbage Collection）

```
db_gc(db)
    ├── db_load_all()
    ├── 過濾掉所有 expire_at < now() 的項目
    ├── 對相同 key 的重複項目去重（保留最後一筆，處理 upsert 殘留）
    ├── db_rewrite_all()
    └── printf("{\"op\":\"gc\",\"removed\":%d,\"remaining\":%d}\n", removed, remaining)
```

**`db_rewrite_all` 的原子性保證**：
```
1. 寫入 /tmp/clips.db.tmp（新內容）
2. fsync(/tmp/clips.db.tmp)
3. rename(/tmp/clips.db.tmp, /tmp/clips.db)   ← POSIX 保證 rename 為原子操作
```
這確保即使在 rewrite 過程中 crash，db 檔案不會處於半寫入狀態 。[^1]

***

## 七、TTL 機制詳解

TTL 的設計採**惰性過期（lazy expiration）**：過期項目不立即刪除，而是在讀取時過濾、在 `--gc` 時清理。

```
寫入時：expire_at = time(NULL) + ttl_seconds
讀取時：若 expire_at != 0 && expire_at < time(NULL) → 視為不存在
GC 時： 批次刪除所有過期項目
```

| 行為 | 時機 |
|---|---|
| 惰性過濾 | `--get`、`--list` 每次查詢時 |
| 批次清理 | 手動執行 `--gc`，或可由外部 cron/定時器定期觸發 |

**為何不用 active expiration**：嵌入式環境不適合背景 thread，惰性過期只需要在查詢時做一次 `time()` 比較，開銷極低 。[^1]

***

## 八、Stdout 輸出格式規格

所有操作的 stdout 輸出均為單行 JSON，方便上層 shell script 或 Fastify 解析：

| 操作 | 輸出格式 |
|---|---|
| 寫入成功 | `{"op":"set","key":"sess_001:1000","ok":true}` |
| 寫入失敗 | `{"op":"set","key":"sess_001:1000","ok":false,"error":"disk full"}` |
| 查詢命中 | `{"key":"sess_001:1000","value":"/tmp/clips/...","ttl_remaining":3200}` |
| 查詢未命中 | `{"key":"sess_001:1000","found":false}` |
| 刪除成功 | `{"op":"delete","key":"sess_001:1000","ok":true}` |
| GC 完成 | `{"op":"gc","removed":15,"remaining":233}` |

診斷訊息（錯誤、警告）**只走 stderr**，stdout 只輸出結構化操作結果 。[^1]

***

## 九、與 `stream_logger` 的整合

```c
LoggerConfig lcfg = {
    .log_file   = NULL,         /* 診斷訊息到 stderr */
    .min_level  = LOG_WARN,
    .format     = LOG_FMT_TEXT,
    .session_id = NULL
};
logger_init(&lcfg);
```

與 `log-parse` 相同，`clip-store` 的診斷輸出嚴格走 stderr，不污染 stdout 管線 。[^1]

***

## 十、CLI 完整規格

```bash
clip-store [OPTIONS]

必要參數：
  --db <path>             db 檔案路徑（不存在則自動建立）

Pipe 寫入模式（從 stdin 讀取 clip JSON Lines）：
  --ttl <seconds>         clip 索引的存活時間（0 = 永不過期，預設：3600）

查詢模式：
  --get <key>             查詢單一 key（格式：session_id:ts）
  --list                  列出所有有效項目
    [--filter <expr>]     搭配 --list，語法與 log-parse 相同

管理模式：
  --delete <key>          刪除單一 key
  --gc                    清除所有過期項目並去重

退出碼：
  0   操作成功
  1   --get / --delete 目標 key 不存在或已過期
  2   db 檔案 I/O 錯誤（無法開啟、無法寫入）
  3   參數錯誤
```

***

## 十一、效能考量與 benchmark 基準

`clip-store` 的對標工具為 `grep`（查詢）和 `awk`（寫入/GC）：

```bash
# --get 等效操作
grep "^sess_001:1000\t" /tmp/clips.db | cut -f2

# --list 等效操作
awk -F'\t' -v now="$(date +%s)" '$3 == 0 || $3 > now' /tmp/clips.db
```

`clip-store` 的優勢在於：
- 統一介面，不需組合多個工具
- `--gc` 的原子性 rewrite 保證 `awk` 無法做到的 crash safety
- JSON 格式輸出方便上層直接 parse，不需再用 `cut` 或 `sed` 處理

Benchmark 目標：100K 條記錄的 db 檔案下，`--list` 查詢應在 `grep | awk` 的 **50% 以內差距** [^1]。

***

## 十二、測試策略

| 測試情境 | 驗證目標 |
|---|---|
| pipe 寫入單筆 | db 檔新增一行，stdout 輸出 `ok:true` |
| upsert（相同 key 再寫） | `--list` 顯示最新值，無重複 key |
| `--get` 命中 | stdout 含正確 value 和 ttl_remaining |
| `--get` 未命中 | exit 1，stdout 含 `found:false` |
| `--get` 過期項目 | exit 1，stdout 含 `expired:true` |
| `--list --filter` | 過濾結果正確，過期項目不出現 |
| `--delete` | db 檔對應行被移除，`--get` 後回傳未命中 |
| `--gc` 清除過期 | removed 數字正確，db 檔縮小 |
| `--gc` 去重 | upsert 後 gc，相同 key 只剩一筆 |
| crash 模擬（rewrite 中途 kill） | db 檔保持完整（rename 原子性） |
| flock 競態（兩個 clip-store 同時寫） | 無 db 檔損壞，兩筆均正確寫入 |

***

## 十三、檔案對應

```
socket-data-analyzer-c/
├── applets/
│   └── clip_store.c       ← 本文件描述的主體
├── lib/
│   ├── stream_logger.h    ← 診斷訊息依賴
│   └── libpipeline.h      ← JSON 工具函式
└── tests/
    └── test_clip_store.sh
```

***

*本文件對應 `socket-data-analyzer-c` repo，`clip_store.c` 模組。*
*v1.0 — 2026-05-11*

---

## References

1. [data_analyzer_archi.md](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/143763799/57352e78-c91d-4b6e-9a40-b61d6850d444/data_analyzer_archi.md?AWSAccessKeyId=ASIA2F3EMEYEUKUIM2AC&Signature=uelUEWUz3cmr%2F%2Bfg0KBeWjpQ8As%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEE8aCXVzLWVhc3QtMSJGMEQCIBXV0OpjsHBBvweqCGAvbCs2UsuGRJqHEk90h%2FTkyRLwAiAdCgCViM9rYOtDbV78tR0Jg9AB76wpet62pRN7sBruSCrzBAgYEAEaDDY5OTc1MzMwOTcwNSIM2UZc8T5Dl%2Fzk%2B%2BJ%2BKtAEF2EjIY5ifK1s44DcyW6a78lJC4r5UELARnUi86mX3IvpEehyjKypG2DC4mFzJUQTjwUdBrYrbbdRopP5sIpizbTuoZDtqWzkne0vRl0WTd9fVq6YAcvSdRcwZx7eoWfwOQ5dUfnBtj5uGw%2Bew7yLbuG46aeIOb8%2BkgVFsWTdQ%2FqMn%2FnVZqFKNI%2FCy2JFwb0yKo7Dk7xnwuvFc50j4%2BTVvs6giECJakkTBHh%2BxJw7Ue%2B6EvwxraBkA6VwR%2BwBd2T6%2BbwMOyvptDIpDTbAwl4QbwBjfXCSasKUuPYlYdjIw6Oj%2F%2BKPaEpBcD8x73NLYDZeFWRoihQOmw9tNcm87Q4E6yBs0qZA%2Bh5awW9zjXZjTCFcrqTbonsV4%2BosMntt0qCDnZKeGmmTwxLK28a2htAXQW%2BM7J36n%2B04R0qqiA5a4%2BmJak5Uv1MpecmEtE7Q3hj17CD%2BsWeOZWfGTARh1R0J8i7ZbuEbVbadyZlibMkyuQwr66K9ha2UGb30MQb2NkyqKxozvH1HjXnJ9lABLycXzNviGIsbdhtKrGx2nO9YkZs06O6h1OEvmkAn8VZFWt57X%2FONbtmDKC36XaCdvH8BybruKvEXGPXQKKe%2FH7%2BKUtXNbuRJTr8dq5suaectNMv8Eqt5cGSdV4rt%2BwukmcmR9ey7A2S7u3uWJ4Wi%2FYgr9irXAdWcsmRnv4FznJTiNrrwgKP8ltHnZM%2Fa3ryOQvS9GtkYx5mKqvysXv7i36icwI%2FFMuVW7P1DnLWX3DlyuiFfHGrXnjDmacZrDO2zEAi72TDa2YfQBjqZAZtQkKMmZ3b2mZ452Ux2jzLdTmw0GQlkEewLGshJiWVekUksGidYeUyHZ08shExDY1kyyYXJ0EOOmkOKH9mJpBGPovge%2F%2FH%2BsQzqWf7BFxia1Ba30ShWitBd6yfFa5QfIDxQvrKKflrYfjjtVUhi%2FBpTiZplRMM6hLf4KY9tqEJC4Xr2XQQbHuYfTaKy5a06lIQG%2FAwge9jH0w%3D%3D&Expires=1778514605) - # Data Analyzer — 獨立開發架構說明

> **版本**：v2.0
> **日期**：2026-04-29
> **定位**：Data Analyzer 是可獨立開發、測試的實時串流處...

2. [project_assignment.pdf](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/collection_f130a32f-3bc7-49c8-9eee-1fc964be7bbf/ee34f601-d9e2-467f-8a18-8a5f7e5febfa/project_assignment.pdf?AWSAccessKeyId=ASIA2F3EMEYEUKUIM2AC&Signature=5Ufe97por9ia38xlK3WNryeZbyo%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEE8aCXVzLWVhc3QtMSJGMEQCIBXV0OpjsHBBvweqCGAvbCs2UsuGRJqHEk90h%2FTkyRLwAiAdCgCViM9rYOtDbV78tR0Jg9AB76wpet62pRN7sBruSCrzBAgYEAEaDDY5OTc1MzMwOTcwNSIM2UZc8T5Dl%2Fzk%2B%2BJ%2BKtAEF2EjIY5ifK1s44DcyW6a78lJC4r5UELARnUi86mX3IvpEehyjKypG2DC4mFzJUQTjwUdBrYrbbdRopP5sIpizbTuoZDtqWzkne0vRl0WTd9fVq6YAcvSdRcwZx7eoWfwOQ5dUfnBtj5uGw%2Bew7yLbuG46aeIOb8%2BkgVFsWTdQ%2FqMn%2FnVZqFKNI%2FCy2JFwb0yKo7Dk7xnwuvFc50j4%2BTVvs6giECJakkTBHh%2BxJw7Ue%2B6EvwxraBkA6VwR%2BwBd2T6%2BbwMOyvptDIpDTbAwl4QbwBjfXCSasKUuPYlYdjIw6Oj%2F%2BKPaEpBcD8x73NLYDZeFWRoihQOmw9tNcm87Q4E6yBs0qZA%2Bh5awW9zjXZjTCFcrqTbonsV4%2BosMntt0qCDnZKeGmmTwxLK28a2htAXQW%2BM7J36n%2B04R0qqiA5a4%2BmJak5Uv1MpecmEtE7Q3hj17CD%2BsWeOZWfGTARh1R0J8i7ZbuEbVbadyZlibMkyuQwr66K9ha2UGb30MQb2NkyqKxozvH1HjXnJ9lABLycXzNviGIsbdhtKrGx2nO9YkZs06O6h1OEvmkAn8VZFWt57X%2FONbtmDKC36XaCdvH8BybruKvEXGPXQKKe%2FH7%2BKUtXNbuRJTr8dq5suaectNMv8Eqt5cGSdV4rt%2BwukmcmR9ey7A2S7u3uWJ4Wi%2FYgr9irXAdWcsmRnv4FznJTiNrrwgKP8ltHnZM%2Fa3ryOQvS9GtkYx5mKqvysXv7i36icwI%2FFMuVW7P1DnLWX3DlyuiFfHGrXnjDmacZrDO2zEAi72TDa2YfQBjqZAZtQkKMmZ3b2mZ452Ux2jzLdTmw0GQlkEewLGshJiWVekUksGidYeUyHZ08shExDY1kyyYXJ0EOOmkOKH9mJpBGPovge%2F%2FH%2BsQzqWf7BFxia1Ba30ShWitBd6yfFa5QfIDxQvrKKflrYfjjtVUhi%2FBpTiZplRMM6hLf4KY9tqEJC4Xr2XQQbHuYfTaKy5a06lIQG%2FAwge9jH0w%3D%3D&Expires=1778514605) - UNIX 系統程式設計 
期末專題說明文件 
 
學期：114 學年度第二學期 
專題佔總成績：30% 
課程代碼：54015

目錄 
一、專題目的與學習目標 
二、專題選項說明 
    選項 A...


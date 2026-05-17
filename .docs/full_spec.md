# socket-data-analyzer-c — v1 System Spec
> **版本**：v1.1 / 2026-05-18（補入 WS packet 格式、chunk 命名、spawn 時機）
> **定位**：本文件描述 `socket-data-analyzer-c` 子系統各模組之間的連動關係與介面契約。不涉及各模組的實作細節，細節請參閱對應的細部開發文件。

***
## 一、系統架構圖
```mermaid
flowchart TD
    A["Fastify API Server\n(Node.js)"]
    B["pipeline_dispatcher\n(C entry point)"]
    C["stream_merge\n(applet)"]
    D["log_parse\n(applet)"]
    E["clip_store\n(applet)"]
    F["/tmp/stream/{session}/\n(filesystem chunks)"]
    G["/tmp/clips.db\n(key-value index)"]
    H["Agent\n(downstream consumer)"]
    L["stream_logger\n(shared lib)"]

    A -- "spawn(session_id, src_dir, db_path, ttl)" --> B
    A -- "writeStream → chunk_NNNN.h264/.aac/.json" --> F
    B -- "fork + exec" --> C
    B -- "fork + exec" --> D
    B -- "fork + exec" --> E
    F -- "inotify IN_CLOSE_WRITE" --> C
    C -- "pipe stdout → stdin\nJSON Lines" --> D
    D -- "pipe stdout → stdin\nfiltered JSON Lines" --> E
    E -- "db write" --> G
    G -- "query / poll" --> H
    C -. "LOG_*" .-> L
    D -. "LOG_*" .-> L
    E -. "LOG_*" .-> L
    B -. "LOG_*" .-> L
```

***
## 二、模組清單
| 模組 | 類型 | 對應檔案 |
|---|---|---|
| `pipeline_dispatcher` | C entry point | `applets/pipeline_dispatcher.c` |
| `stream_merge` | BusyBox applet | `applets/stream_merge.c` |
| `log_parse` | BusyBox applet | `applets/log_parse.c` |
| `clip_store` | BusyBox applet | `applets/clip_store.c` |
| `stream_logger` | 共用函式庫 | `lib/stream_logger.c` |
| `libpipeline` | 共用函式庫 | `lib/libpipeline.c` |

***
## 三、模組間連動說明
### 3.1 Fastify → `pipeline_dispatcher`

#### 3.1.1 WS Packet 格式（上層輸入）

Fastify (`edge-ws-host` repo) 從 ESP32-S3 收到的 WS binary frame 結構：

```
Byte offset  長度     內容
──────────── ──────── ─────────────────────────────────
0 ~ 3        4 bytes  opCode（ASCII 字串）
4 ~ 7        4 bytes  payloadSize（UInt32BE）
8 ~ N        N bytes  payload（依 opCode 決定格式）
```

| OpCode（4 bytes ASCII） | hex | payload 格式 | C 側對應動作 |
|---|---|---|---|
| `STRT` | `0x53545254` | UTF-8 字串 → `eventId` | 建立 `/tmp/stream/{eventId}/` 目錄，準備接收 chunk |
| `DATA` | `0x44415441` | 原始 binary（h264 / aac raw bytes） | Fastify 寫入 `chunk_NNNN.bin` |
| `END_` | `0x454E445F` | 空或 UTF-8 確認字串 | Fastify 建立 sentinel + spawn `pipeline_dispatcher` |
| `JSON` | `0x4A534F4E` | UTF-8 JSON 字串 → metadata | Fastify 寫入 `chunk_NNNN.json` |

#### 3.1.2 v1 chunk 落地檔名規則

每收到一個 `DATA`/`JSON` packet，Fastify 切一個獨立檔案：

```
/tmp/stream/{session_id}/
    chunk_0000.bin     ← 第 1 個 DATA packet 的 payload
    chunk_0001.bin     ← 第 2 個 DATA packet 的 payload
    chunk_0002.json    ← 中間夾雜的 JSON metadata
    ...
    .pipeline_end      ← END_ opcode 後建立的 sentinel
```

seq 來自 Fastify 的 in-memory 計數器，零填充 4 位數，遞增不重置。`stream_merge` 直接以檔名 seq 解析，**v1 不使用 binary header**（移除原本 `parse_binary_header()` 的依賴）。

#### 3.1.3 Spawn 時機

Fastify 在收到 `END_` opcode 並完成 `streamer.end()` 後：

1. 建立 sentinel：`fs.writeFileSync('/tmp/stream/{eventId}/.pipeline_end', '')`
2. `child_process.spawn('./pipeline_dispatcher', [session_id, src_dir, db_path, ttl_seconds])`

**設計選擇**：spawn 放在 `END_` 而非 `STRT`，原因是 v1 的 chunk 落地策略要求資料完整後才啟動 inotify（避免 race condition）。v2 規劃將 spawn 移到 `STRT`、改用 streaming ingestion。

兩件事互不阻塞：WS 接收與 chunk 落地持續進行，`pipeline_dispatcher` 在獨立 process 中執行，Fastify 透過 exit code 判斷管線是否正常結束。

**介面**：CLI 參數（`argv`）傳入，exit code 傳回。Fastify 監聽 `stderr` 取得診斷訊息。

#### 3.1.4 session_id 對齊

`full_spec.md` 使用的 `session_id` 與 Fastify 程式碼的 `eventId` 語意相同。v1 直接以 `eventId` 字串作為 `session_id` 傳入 `pipeline_dispatcher`，**不需要轉換或映射**。

***
### 3.2 `pipeline_dispatcher` → 三個 applet
`pipeline_dispatcher` 使用 `pipe()` + `fork()` + `exec()` 建立以下管線：

```
stream_merge  stdout
      │  pipe_1
      ▼
log_parse     stdin / stdout
      │  pipe_2
      ▼
clip_store    stdin
```

`pipeline_dispatcher` 本身不讀寫任何業務資料，只負責：
- 建立 `pipe_1`、`pipe_2`
- 以正確的 fd 重定向（`dup2`）啟動三個子 process
- 關閉父進程中所有 pipe fd
- 以 `waitpid()` 等待並收集三個子 process 的退出狀態

任一子 process 非正常退出，`pipeline_dispatcher` 回傳 `-2` 給 Fastify。

**介面**：UNIX pipe（`pipe_1`、`pipe_2`），`waitpid` 退出狀態。

***
### 3.3 Filesystem → `stream_merge`
`stream_merge` 透過 `libpipeline` 提供的 `pipeline_watch_dir()` 封裝，以 `inotify` 監聽 `src_dir`（`/tmp/stream/{session_id}/`）。

觸發事件：`IN_CLOSE_WRITE`（Fastify `writeStream` 關閉 chunk 檔案時）

每次事件觸發，`stream_merge` 讀取新抵達的 chunk 檔，進行品質檢查、gap 偵測、buffer 累積，每滿 5s 或偵測到 gap 時，輸出一條 clip JSON Line 到 `pipe_1`。

**介面**：`inotify` 事件（filesystem 邊界），`pipe_1` stdout（與 `log_parse` 的邊界）。

***
### 3.4 `stream_merge` → `log_parse`（透過 `pipe_1`）
`stream_merge` 的 stdout 直接接通 `log_parse` 的 stdin。資料格式為**壓縮 JSON Lines**，每行一個完整 JSON 物件，以 `\n` 為分隔符。

`log_parse` 以 `--filter type=clip` 過濾，只讓 clip 類型的 JSON Line 通過，其餘類型（如 metrics、heartbeat）被靜默丟棄。過濾後的 JSON Line 輸出到 `pipe_2`。

**介面**：`pipe_1`（JSON Lines，純 stdin/stdout，無共享記憶體）。

***
### 3.5 `log_parse` → `clip_store`（透過 `pipe_2`）
`log_parse` 的 stdout 直接接通 `clip_store` 的 stdin。`clip_store` 讀取每條 clip JSON Line，提取 `session_id` 與 `ts` 組合成 key，將 `path` 作為 value，加上 TTL 寫入 `clips.db`。

每筆寫入成功後，`clip_store` 輸出一條操作確認 JSON 到自身的 stdout（僅供 debug，`pipeline_dispatcher` 不讀取此輸出）。

**介面**：`pipe_2`（JSON Lines），`clips.db` 檔案（filesystem 邊界）。

***
### 3.6 `clip_store` → Agent（下游消費）
Agent 不在本子系統範疇內，透過兩種方式消費 `clip_store` 的輸出：

| 消費方式 | 指令 | 適用場景 |
|---|---|---|
| 拉模式查詢 | `clip_store --db /tmp/clips.db --list --filter session_id=sess_001` | Agent 定期 polling |
| 管線即時消費 | `stream_merge \| log_parse \| clip_store \| agent_consumer` | 需要最低延遲 |

**介面**：`clips.db` 純文字 key-value 檔案，或 stdout JSON Lines（即時管線模式）。

***
### 3.7 `libpipeline`（共用基礎函式庫）

所有 applet 共用的基礎函式庫，封裝 inotify 監聽、時間取得、JSON 壓縮、sentinel 判別。詳見 [libpipeline-v1.0](libpipeline-v1.0)。

**重點**：sentinel 檔名固定為 `.pipeline_end`，由 Fastify 建立、`stream_merge` 透過 `pipeline_is_sentinel()` 判別、`pipeline_dispatcher` 在管線收束後清理。

***
### 3.8 `stream_logger`（橫切面依賴）
`stream_logger` 是所有模組共用的日誌函式庫，不參與業務資料流。各模組透過 `#include "stream_logger.h"` 引入，使用 `LOG_INFO()`、`LOG_WARN()` 等巨集輸出診斷訊息。

**約定**：所有診斷訊息只走 **stderr**，不污染任何 pipe 的 stdout 資料流。

***
## 四、資料流總覽
```
ESP32-S3 WS frame
    │
    ▼
Fastify (解析 frame_type + seq + payload)
    ├── writeStream → /tmp/stream/{session}/video/chunk_NNNN.h264
    ├── writeStream → /tmp/stream/{session}/audio/chunk_NNNN.aac
    ├── writeStream → /tmp/stream/{session}/meta/chunk_NNNN.json
    └── spawn → pipeline_dispatcher
                    │
                    ├─ pipe_1 ─────────────────────────────────────┐
                    │                                               │
              [stream_merge]                                  [log_parse]
              inotify 監聽                                    stdin 讀取
              5s 切割                                         type=clip 過濾
              gap 偵測                                        stdout 輸出
              stdout → pipe_1                                       │
                                                             pipe_2 │
                                                                    ▼
                                                            [clip_store]
                                                            stdin 讀取
                                                            寫入 clips.db
                                                                    │
                                                                    ▼
                                                            /tmp/clips.db
                                                                    │
                                                                    ▼
                                                              Agent 消費
```

***
## 五、模組介面契約摘要
| 連動邊界 | 介面類型 | 格式 | 方向 |
|---|---|---|---|
| Fastify → `pipeline_dispatcher` | `spawn()` CLI 參數 | `argv[]` 字串 | 單向呼叫 |
| Fastify ← `pipeline_dispatcher` | exit code + stderr | 整數 / 文字 | 回傳結果 |
| `pipeline_dispatcher` → applets | `fork` + `exec` | CLI argv + fd 重定向 | 單向啟動 |
| `stream_merge` → `log_parse` | UNIX pipe | JSON Lines（UTF-8） | 單向串流 |
| `log_parse` → `clip_store` | UNIX pipe | JSON Lines（UTF-8） | 單向串流 |
| `clip_store` → filesystem | `write()` | Tab-separated text | 單向寫入 |
| filesystem → `stream_merge` | `inotify` | `IN_CLOSE_WRITE` 事件 | 事件通知 |
| 各模組 → `stream_logger` | 函式呼叫（`LOG_*`） | 格式化字串 | 單向輸出 |

***
## 六、錯誤傳播路徑
異常從子工具向上傳遞遵循 UNIX cascade close 機制：

```
stream_merge 崩潰
    → pipe_1 write end 關閉
    → log_parse 讀到 EOF → 正常退出
    → pipe_2 write end 關閉
    → clip_store 讀到 EOF → 正常退出
    → pipeline_dispatcher 的 waitpid() 收到所有退出狀態
    → pipeline_dispatcher exit(-2)
    → Fastify spawn 'exit' 事件觸發告警
```

任一環節的崩潰都能**自動清理整條管線**，不留孤兒 process。

***
## 七、開發分工介面
各模組可完全獨立開發，只需遵守以下介面約定：

| 模組 | 只需知道 | 不需要知道 |
|---|---|---|
| `stream_merge` | `libpipeline.h`、JSON Lines 輸出格式 | `log_parse` 如何過濾 |
| `log_parse` | JSON Lines 輸入格式、`stream_logger.h` | `stream_merge` 如何產生資料 |
| `clip_store` | 過濾後 JSON Lines 輸入格式、`stream_logger.h` | `log_parse` 的過濾邏輯 |
| `pipeline_dispatcher` | 各 applet 的 CLI 介面 | 任何 applet 的內部實作 |

***

*本文件為 `socket-data-analyzer-c` v1 系統規格，對應細部文件請參閱各模組的 `*_dev_doc`。*
*v1.0 — 2026-05-11*
# `stream-merge` — 細部開發文件 v1.0

> **對應檔案**：`applets/stream_merge.c`
> **版本**：v1.0 / 2026-05-11
> **定位**：`stream-merge` 是整個 Data Analyzer 管線的**唯一資料讀取者**。它透過 inotify 監聽 filesystem，主動累積 chunk、偵測 gap、切割 clips、提取 events，並將結果以 JSON Lines 形式輸出到 stdout。

***

## 一、職責總覽

`stream-merge` 的職責可以拆成五個相互獨立的子系統，各自有明確的邊界：

| 子系統 | 負責範圍 | 不負責的事 |
|---|---|---|
| **Watch**（監聽層） | inotify 目錄監聽、新檔觸發 | chunk 的內容解析 |
| **Ingest**（讀入層） | 讀取 chunk 檔案、解析 binary header | 判斷 gap 或 timeout |
| **Quality**（質檢層） | CRC32 校驗、去重、seq 驗證 | 決定是否輸出 clip |
| **State Machine**（狀態機） | gap 偵測、buffer 累積、clip 切割決策 | 實際 I/O 操作 |
| **Emit**（輸出層） | 合併 chunks、生成 JSON、fflush stdout | 下游如何消費 |

***

## 二、程式進入點與初始化流程

```
main(argc, argv)
    │
    ├── [^1] parse_args()          — 解析 CLI 參數，填入 Config struct
    ├── [^2] validate_config()     — 驗證必要參數（--src、--session 不可缺）
    ├── init_watch()          — 建立 inotify fd，監聽 src 目錄
    ├── init_state()          — 初始化 StreamState（last_seq=-1, buf=空）
    ├── init_metrics()        — 歸零所有 QoS 計數器
    ├── restore_state()       — 若 --state-file 存在，從磁碟恢復中斷前狀態
    │
    └── run_event_loop()      — 進入主事件循環（阻塞直到 idle timeout 或 SIGTERM）
```

初始化完成後，程式完全進入事件驅動模式，不使用輪詢 。[^1]

***

## 三、核心資料結構

### 3.1 Config（CLI 參數對應）

```c
typedef struct {
    char    src_dir[PATH_MAX];      // --src
    char    session_id;         // --session
    int     chunk_duration_s;       // --chunk-duration（預設 1）
    int     target_duration_s;      // --target-duration（預設 5）
    int     gap_threshold_s;        // --gap-threshold（預設 5）
    int     idle_timeout_s;         // --idle-timeout（預設 10）
    int     quality_check;          // --quality-check flag
    int     extract_events;         // --extract-events flag
    char    state_file[PATH_MAX];   // --state-file
    char    log_file[PATH_MAX];     // --log-file
    char    metrics_file[PATH_MAX]; // --metrics-file
} Config;
```

### 3.2 ChunkEntry（單一 chunk 的內存表示）

```c
typedef struct {
    int      seq;               // chunk 序號（來自檔名或 binary header）
    time_t   timestamp;         // chunk 對應的媒體時間戳
    uint8_t  stream_type;       // VIDEO=0x02 | AUDIO=0x03 | META=0x04
    uint32_t size;              // payload 大小（bytes）
    uint8_t *data;              // heap-allocated payload（用後 free）
    uint32_t crc32;             // header 中記錄的 checksum
    int      is_corrupted;      // 質檢結果
} ChunkEntry;
```

### 3.3 StreamState（跨 chunk 的持久狀態）

```c
typedef struct {
    int          last_seq;              // 上一個成功接受的 chunk seq
    time_t       last_chunk_time;       // 上一個 chunk 的系統時間（用於 idle 計算）
    time_t       buffer_start_time;     // 當前 buffer 第一個 chunk 的時間戳
    int          accumulated_count;     // 目前 buffer 中的 chunk 數
    ChunkEntry   buffer[MAX_BUF_SIZE];  // chunk 緩衝陣列（固定大小）
    int          buf_head;              // buffer 的有效長度
    EventList    pending_events;        // 從 meta chunks 提取但尚未輸出的 events
    QosMetrics   metrics;               // 即時 QoS 計數器
} StreamState;
```

***

## 四、主事件循環（`run_event_loop`）

主循環是整個程式的核心，以 inotify fd 的 `poll()` 為驅動，每次迭代處理一個檔案事件或超時 。[^1]

```
run_event_loop(config, state)
    │
    ├── poll(inotify_fd, timeout=100ms)
    │       │
    │       ├── [有事件] read_inotify_events()
    │       │       └── for each event:
    │       │               ├── is_sentinel()? → handle_sentinel() → break loop
    │       │               └── on_new_chunk_file(filepath) → [見第五節]
    │       │
    │       └── [超時 / 無事件] check_idle_timeout()
    │               └── 若 now - last_chunk_time > idle_timeout_s
    │                       └── flush_partial("idle_timeout") → reset_state()
    │
    └── [loop 結束後] flush_remaining() → save_state() → write_metrics()
```

**關鍵設計**：`poll()` 的 timeout 設為 100ms，確保 idle timeout 偵測的精度在 100ms 以內，不需要額外的 timer thread 。[^1]

***

## 五、新 Chunk 到達的處理流程（`on_new_chunk_file`）

這是最核心的路徑，每個 chunk 到達都會走一遍：

```
on_new_chunk_file(filepath)
    │
    ├── [A] ingest_chunk(filepath)
    │       ├── open() + read()
    │       ├── parse_binary_header() → 填入 ChunkEntry
    │       └── 回傳 ChunkEntry（或 NULL 若讀取失敗）
    │
    ├── [B] quality_check(chunk, state)   （若 --quality-check 啟用）
    │       ├── crc32_verify(chunk)
    │       │       └── 失敗 → log_warn + metrics.corrupted++ + return SKIP
    │       └── dedup_check(chunk->seq, state)
    │               └── 重複 → log_warn + metrics.duplicated++ + return SKIP
    │
    ├── [C] gap_check(chunk->seq, state)
    │       ├── expected_seq = state->last_seq + 1
    │       └── chunk->seq != expected_seq && last_seq != -1
    │               └── 發現 gap！
    │                       ├── flush_partial("gap_detected", gap_info)
    │                       └── reset_state()
    │                           （注意：reset 後仍繼續處理此 chunk）
    │
    ├── [D] buffer_append(chunk, state)
    │       ├── state->buffer[state->buf_head++] = *chunk
    │       ├── state->last_seq = chunk->seq
    │       ├── state->last_chunk_time = now()
    │       └── 若 --extract-events && stream_type == META
    │               └── extract_events(chunk) → append to state->pending_events
    │
    └── [E] emit_check(state, config)
            └── state->accumulated_count >= config->target_duration_s / chunk_duration_s
                    └── flush_complete() → reset_state()
```

**流程中的關鍵不變式**：gap 偵測到之後仍然繼續處理當前 chunk（而非丟棄），這確保新的 5s 週期從導致 gap 的那個 chunk 開始累積，不浪費資料 。[^1]

***

## 六、三種 Flush 路徑

所有的 "輸出" 動作都統一走 `flush_*` 函式族，確保輸出格式一致。

### 6.1 `flush_complete(state, config)`
觸發條件：`accumulated_count == target_chunks`

```
flush_complete()
    ├── merge_video_chunks(state->buffer) → output_path（合併 .h264 raw data）
    ├── build_clip_json(state, "complete", "target_duration_met", output_path)
    ├── emit_json_line(json)    — 寫入 stdout
    ├── fflush(stdout)          — 確保下游 log-parse 立即收到
    ├── log_info("clip_N written to %s", output_path)
    └── metrics.clips_output++
```

### 6.2 `flush_partial(state, config, reason, gap_info)`
觸發條件：gap 偵測 或 idle timeout

```
flush_partial()
    ├── （若 buffer 為空則只記錄日誌，直接返回）
    ├── merge_video_chunks(state->buffer) → output_path（partial 檔名含 _partial）
    ├── build_clip_json(state, "partial", reason, output_path, gap_info)
    ├── emit_json_line(json)
    ├── fflush(stdout)
    ├── log_warn("Partial clip due to: %s", reason)
    └── metrics.gaps_detected++（若 reason == "gap_detected"）
```

### 6.3 `flush_remaining(state, config)`
觸發條件：程式正常結束時（loop 退出後）

```
flush_remaining()
    └── 若 state->accumulated_count > 0
            └── flush_partial(state, config, "session_end", NULL)
```

***

## 七、Gap 狀態機詳解

狀態機是 `stream-merge` 的核心邏輯，由 `gap_check()` 與 `emit_check()` 共同維護 。[^1]

```
         新 chunk 到達
              │
    ┌─────────▼──────────┐
    │      Collecting     │◀────────────────────┐
    │  (buffer 累積中)    │                     │
    └──────┬──────┬───────┘                     │
           │      │                             │
   seq 連續 │      │ seq 不連續                  │
   未滿 5s  │      │ (gap 偵測)                  │
           │      ▼                             │
           │  ┌──────────────┐                  │
           │  │  EmitPartial │                  │
           │  │  flush_partial│                 │
           │  └──────┬───────┘                  │
           │         │                          │
   累積滿   │         └──────────────────┐       │
   5s       ▼                           ▼       │
    ┌──────────────┐              ┌─────────┐   │
    │  EmitComplete│              │  Reset  │───┘
    │ flush_complete│             │ 清空 buf │
    └──────┬───────┘             └─────────┘
           │
           └──────────────────────────▶ Reset
```

**RejectLateChunk 的判斷**：若 `chunk->seq < state->last_seq`，代表這是 Reset 後遲到的舊 chunk，直接 `log_warn` 後 `return`，不進入任何 flush 路徑 。[^1]

***

## 八、Events 提取子流程（`--extract-events`）

當 `stream_type == META（0x04）` 且啟用 `--extract-events` 時，chunk 的 payload 是一個 JSON 物件，`extract_events()` 負責解析並追加到 `state->pending_events`：

```
extract_events(chunk)
    ├── json_parse(chunk->data, chunk->size) → event_obj
    ├── for each event in event_obj["events"]:
    │       └── pending_events.append({
    │               .timestamp  = event["timestamp"],
    │               .type       = event["type"],
    │               .confidence = event["confidence"],
    │               .raw_json   = event  ← 保留完整原始 JSON
    │           })
    └── metrics.events_extracted += count
```

`pending_events` 在 `flush_complete` 或 `flush_partial` 時被整體序列化進 clip JSON 的 `"events"` 欄位，然後清空 。[^1]

***

## 九、Side-effect 輸出（非 stdout）

除了 stdout 的 JSON Lines，`stream-merge` 還會維護三個磁碟副作用，**均為非阻塞的非同步寫入**，不影響主事件循環的延遲：

### 9.1 State File（`--state-file`）
每次 `reset_state()` 後，將關鍵狀態序列化到磁碟，支援 crash recovery：

```
last_seq=<int>
last_timestamp=<unix_ts>
accumulated_ms=<int>
chunks_processed=<int>
clips_output=<int>
gaps_detected=<int>
corrupted_chunks=<int>
```

### 9.2 Log File（`--log-file`）
格式：`[LEVEL] ISO8601_TIMESTAMP 訊息`

| Level | 觸發時機 |
|---|---|
| INFO | session 開始、每個 chunk 到達、每個 clip 輸出 |
| DEBUG | events 提取、buffer 狀態變化 |
| WARN | gap 偵測、idle timeout、corrupted chunk |
| ERROR | I/O 失敗、json 解析錯誤 |

### 9.3 Metrics File（`--metrics-file`）
每次 `flush_*` 後更新，JSON 格式。包含 `total_chunks_seen`、`corruption_rate`、`gaps_detected`、`processing_lag_ms` 等 QoS 指標 。[^1]

***

## 十、與 `libpipeline` 的依賴關係

`stream-merge` 直接使用以下 `libpipeline` 提供的函式，不自行實作：

| libpipeline 函式 | stream-merge 的使用場景 |
|---|---|
| `pipeline_watch_dir()` | `init_watch()` 封裝 inotify 建立 |
| `pipeline_now_ms()` | idle timeout 計算、processing_lag_ms 統計 |
| `pipeline_compress_json()` | `emit_json_line()` 輸出前壓縮 JSON |
| `pipeline_is_sentinel()` | 偵測 END 檔案（`handle_sentinel()`） |

***

## 十一、CLI 完整規格

```bash
stream-merge [OPTIONS]

必要參數：
  --src   <dir>         chunk 來源目錄（由 API Server writeStream 寫入）
  --session <id>        session 識別碼

可選參數（含預設值）：
  --chunk-duration <s>  每個 chunk 的時間長度（預設：1）
  --target-duration <s> 目標 clip 長度（預設：5）
  --gap-threshold <s>   判定 gap 的 seq 缺口閾值（預設：5）
  --gap-policy <policy> gap 後行為：output（立即輸出）/ skip（捨棄）（預設：output）
  --quality-check       啟用 CRC32 校驗與去重（預設：關閉）
  --extract-events      從 meta chunks 提取 events（預設：關閉）
  --idle-timeout <s>    無新 chunk 後觸發 partial flush 的等待秒數（預設：10）
  --state-file <path>   狀態檔路徑（用於 crash recovery）
  --log-file <path>     日誌輸出路徑（預設：stderr）
  --metrics-file <path> QoS 指標輸出路徑

退出碼：
  0   正常結束（idle timeout 或 sentinel 觸發）
  1   參數錯誤
  2   I/O 錯誤（src 目錄不存在、inotify 建立失敗）
  3   內部錯誤（記憶體分配失敗等）
```

***

## 十二、測試策略

每個子系統可以獨立測試，不需要完整管線：

| 測試情境 | 測試方式 | 驗證目標 |
|---|---|---|
| 正常 5s 累積輸出 | `synthetic_stream.py --chunks 5 --interval 1` | stdout 出現 `status=complete` |
| Gap 偵測與 partial flush | `synthetic_stream.py --gap-after 3` | stdout 出現 `status=partial` + `reason=gap_detected` |
| Idle timeout | 寫 3 個 chunk 後停止 12s | stdout 出現 `reason=idle_timeout` |
| CRC 校驗 | 寫入損毀 chunk（手動 flip bit） | log WARN，metrics.corrupted++ |
| Late chunk 拒收 | 在 gap 後補送舊 seq | log WARN，不觸發 flush |
| Crash recovery | 中途 kill -9，重啟後帶 --state-file | 從上次 last_seq 繼續 |

***

## 十三、檔案對應

```
socket-data-analyzer-c/
├── applets/
│   └── stream_merge.c       ← 本文件描述的主體
├── lib/
│   ├── libpipeline.h        ← Watch、JSON、時間戳等共用介面
│   └── libpipeline.c
└── tests/
    └── test_stream_merge.sh
```

***

*本文件對應 `socket-data-analyzer-c` repo，`stream_merge.c` 模組。*
*v1.0 — 2026-05-11*

---

## References

1. [data_analyzer_archi.md](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/143763799/57352e78-c91d-4b6e-9a40-b61d6850d444/data_analyzer_archi.md?AWSAccessKeyId=ASIA2F3EMEYE4NW6FXDY&Signature=UGmPLi%2BKM9S7JYtHYjEv243NFoc%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEE8aCXVzLWVhc3QtMSJGMEQCIFRSl%2B6DnL6AR19QM095BXZfpKrJh%2F%2BwBzDYAK4oWmyiAiB7WNGUgOpWufxMD%2BMlP1BnUXLbOCkUU%2FK4e2BzwcaVvSrzBAgYEAEaDDY5OTc1MzMwOTcwNSIMFU68np6KuLvo8bXyKtAEYQiL9fsm7x0UwIZ9VCM2C%2FldnLTKdV8vj%2FNKUPs%2Bo8GElIBIUQPgW1YSRZbpgJ76B1BqEueAs3xp9fW%2Fl5Z%2FbZEhFfPuoRJgqSjIIOgzLEJEiK8X4ajYHrUWZ62lLRF2IyTtqvLLtJmkedYq6lgtgyk6H%2F9g4%2BskmNeCFFbOH9LrtBdIPKaBbx55WpQX1PfFe1Es%2BmnJC8qWR7KoHH1emnA%2Fk7h1nNJCeQhBt5HJJMN47i4bnyui8DAHEAs%2BbykX%2FI1%2ByUB5ckFeA%2B2K%2FHUH6YutFcmOvx1NObbrvoknqmWRrybU2JCdgUTGhcN2yJ2%2BJnXpoWGW4Hc1kjj6R5foxArQGazr6mmkLHKH42wIBm1XOQtnTzFsNn1A5Tox0Vim5D2w%2FOn9M9BoPIVGgfH121JsYuJwNwHG2iiJdWY%2FotvDeqTSBjCgr5krNNj68aFU6lhsTfR6TucTo8dHJMzMj8%2FwBbVv1wfTFjgamIa03lbQj%2BKRx3gCqnRzt8z3bEq%2BpvqA5fWNCtxddSqIjVjYrm%2Fa7mmnbdDqbQ6A8MVJlqTeajQfsuD67asMhNOTK4oReFDUJJcVDqDh0peJsmH%2BohOzvmN5B4Zj9wrupk39099fIDFK9hIxYudVhHiB0Wt0QDsbiFfnT2UdBNu9HaKhL8%2FVNdGUBG4ePB6WIhC%2FP9GJpSYReyWkiKO%2FWs0mlX3XSuCKG2iqry7ZRWDXIpxt2JH%2BeW03BDgH15fePxzgBYPAsgdwErQxdW9oPJJuVuDzirdogyRvCpCZuCg2iBPM3jCl3IfQBjqZAbInxT8DnIz7Qjq0VjifyycEo2lEzYk330c2SoT8FP%2BLewVbBC4oHp5IkJZYWf%2FX4OBBFPp1BVzGWCadI5h6z%2FEXEEr6BEHfnL1w2Evlr5lCfD5OFpCUxT49MKntPgxk900i%2BtiZJaHYPjvziot6WwdfDPNpxbfFLCHOzJvfRnlK3vIin0lvbZm%2Bulw9lLPI1YP4Tk2a19mJ7w%3D%3D&Expires=1778514936) - # Data Analyzer — 獨立開發架構說明

> **版本**：v2.0
> **日期**：2026-04-29
> **定位**：Data Analyzer 是可獨立開發、測試的實時串流處...

2. [project_assignment.pdf](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/collection_f130a32f-3bc7-49c8-9eee-1fc964be7bbf/ee34f601-d9e2-467f-8a18-8a5f7e5febfa/project_assignment.pdf?AWSAccessKeyId=ASIA2F3EMEYE4NW6FXDY&Signature=aNOPPXjbdskrFDiPduL99WCGcl0%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEE8aCXVzLWVhc3QtMSJGMEQCIFRSl%2B6DnL6AR19QM095BXZfpKrJh%2F%2BwBzDYAK4oWmyiAiB7WNGUgOpWufxMD%2BMlP1BnUXLbOCkUU%2FK4e2BzwcaVvSrzBAgYEAEaDDY5OTc1MzMwOTcwNSIMFU68np6KuLvo8bXyKtAEYQiL9fsm7x0UwIZ9VCM2C%2FldnLTKdV8vj%2FNKUPs%2Bo8GElIBIUQPgW1YSRZbpgJ76B1BqEueAs3xp9fW%2Fl5Z%2FbZEhFfPuoRJgqSjIIOgzLEJEiK8X4ajYHrUWZ62lLRF2IyTtqvLLtJmkedYq6lgtgyk6H%2F9g4%2BskmNeCFFbOH9LrtBdIPKaBbx55WpQX1PfFe1Es%2BmnJC8qWR7KoHH1emnA%2Fk7h1nNJCeQhBt5HJJMN47i4bnyui8DAHEAs%2BbykX%2FI1%2ByUB5ckFeA%2B2K%2FHUH6YutFcmOvx1NObbrvoknqmWRrybU2JCdgUTGhcN2yJ2%2BJnXpoWGW4Hc1kjj6R5foxArQGazr6mmkLHKH42wIBm1XOQtnTzFsNn1A5Tox0Vim5D2w%2FOn9M9BoPIVGgfH121JsYuJwNwHG2iiJdWY%2FotvDeqTSBjCgr5krNNj68aFU6lhsTfR6TucTo8dHJMzMj8%2FwBbVv1wfTFjgamIa03lbQj%2BKRx3gCqnRzt8z3bEq%2BpvqA5fWNCtxddSqIjVjYrm%2Fa7mmnbdDqbQ6A8MVJlqTeajQfsuD67asMhNOTK4oReFDUJJcVDqDh0peJsmH%2BohOzvmN5B4Zj9wrupk39099fIDFK9hIxYudVhHiB0Wt0QDsbiFfnT2UdBNu9HaKhL8%2FVNdGUBG4ePB6WIhC%2FP9GJpSYReyWkiKO%2FWs0mlX3XSuCKG2iqry7ZRWDXIpxt2JH%2BeW03BDgH15fePxzgBYPAsgdwErQxdW9oPJJuVuDzirdogyRvCpCZuCg2iBPM3jCl3IfQBjqZAbInxT8DnIz7Qjq0VjifyycEo2lEzYk330c2SoT8FP%2BLewVbBC4oHp5IkJZYWf%2FX4OBBFPp1BVzGWCadI5h6z%2FEXEEr6BEHfnL1w2Evlr5lCfD5OFpCUxT49MKntPgxk900i%2BtiZJaHYPjvziot6WwdfDPNpxbfFLCHOzJvfRnlK3vIin0lvbZm%2Bulw9lLPI1YP4Tk2a19mJ7w%3D%3D&Expires=1778514936) - UNIX 系統程式設計 
期末專題說明文件 
 
學期：114 學年度第二學期 
專題佔總成績：30% 
課程代碼：54015

目錄 
一、專題目的與學習目標 
二、專題選項說明 
    選項 A...


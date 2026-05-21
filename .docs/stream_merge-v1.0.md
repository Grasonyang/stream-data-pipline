# stream_merge

`stream_merge` 是 pipeline 的唯一 stream reader。它讀取上層持續 append 的 `{session_id}.bin` binary video buffer，搭配 `{session_id}.meta.jsonl` sidecar 判斷 chunk 的 byte range 與時間資訊，依固定時間窗與 continuity 規則切出 clip，最後輸出 clip metadata JSON Lines 到 stdout。

## Responsibilities

- 開啟 `{src_dir}/{session_id}.bin`（驗證存在；v2.1 不讀取 byte 內容）。
- 開啟 `{src_dir}/{session_id}.meta.jsonl`，持續讀取新增的 meta records。
- 監聽 meta 檔案 `IN_MODIFY` 與目錄中的 `.pipeline_end`。
- 依 meta sidecar 判斷 chunk byte range、timestamp、sequence、offset continuity。
- 依 5s 時間窗與 continuity 切出 complete / partial clip。
- 偵測 `.pipeline_end` 後 drain 剩餘 meta records，flush final clip，exit 0。

## Inputs

```text
stream_merge --src <src_dir> --session <session_id>
             [--clip-secs <n>]
             [--idle-secs <n>]
```

| 參數 | 預設 | 說明 |
|------|------|------|
| `--src` | required | session artifact 目錄 |
| `--session` | required | session ID，決定 `.bin` / `.meta.jsonl` 檔名 |
| `--clip-secs` | 5 | clip 時間窗（秒）；`ts_ms` span >= clip_ms 時切割 |
| `--idle-secs` | 2 | idle timeout（秒）；超時後 emit partial clip |

預期檔案：

```text
{src_dir}/{session_id}.bin
{src_dir}/{session_id}.meta.jsonl
{src_dir}/.pipeline_end
```

- `{session_id}.bin`：binary video bytes，由 `edge-ws-host` 逐 DATA message append。
- `{session_id}.meta.jsonl`：chunk metadata sidecar，每行一個 JSON record（格式見下）。
- `.pipeline_end`：writer 完成 signal；上層收到 END_ 並寫完 session artifact 後建立。

## Meta Record 格式

每行一個 JSON object，以 `\n` 結尾：

```json
{"kind":"data","seq":1,"offset":0,"length":4096,"ts_ms":1747065600000}
```

| 欄位 | 型別 | 說明 |
|------|------|------|
| `kind` | string | `"data"` 為 binary video chunk；其他 kind（如 `"json"`）目前 skip |
| `seq` | uint64 | chunk sequence number，從 1 開始，單調遞增 |
| `offset` | uint64 | 此 chunk 在 `.bin` 中的 byte offset |
| `length` | uint64 | 此 chunk 的 byte 數 |
| `ts_ms` | int64 | 此 chunk 的起始時間戳（Unix ms） |

## FSM 設計

```
SM_IDLE  ──first data chunk──▶  SM_COLLECTING
SM_COLLECTING  ──continuity ok, span < clip_ms──▶  SM_COLLECTING
SM_COLLECTING  ──continuity ok, span >= clip_ms──▶  emit_complete → SM_IDLE → SM_COLLECTING
SM_COLLECTING  ──continuity break──▶  emit_partial → SM_IDLE → SM_COLLECTING
SM_COLLECTING  ──idle timeout──▶  emit_partial → SM_IDLE
SM_*  ──sentinel + final drain──▶  emit final clip → exit 0
```

**Continuity check（兩條件均必須成立）：**

1. `record.seq == state.next_seq`（sequence 連續）
2. `record.offset == state.next_offset`（offset 連續：前一 chunk 的 `offset + length`）

任一失敗 → emit partial clip + reset + 以觸發 record 開始新 clip。

**時間窗判斷：**

`record.ts_ms - clip.start_ts_ms >= clip_ms`

## Output Contract

stdout 每行是一個完整 JSON object，以 `\n` 結尾：

```json
{"type":"clip","session_id":"sess_001","ts":1747065600,"path":"/tmp/stream/sess_001/1747065600.bin","offset":0,"length":20480,"complete":true}
```

| 欄位 | 說明 |
|------|------|
| `type` | `"clip"` |
| `session_id` | session ID |
| `ts` | clip 起始時間（Unix 秒，`ts_ms / 1000`） |
| `path` | synthetic path（v2.1 不寫出實際 binary 小檔；代表 clip 在 session `.bin` 的位置） |
| `offset` | clip 在 session `.bin` 中的起始 byte offset |
| `length` | clip 的 byte 數 |
| `complete` | `true` = 時間窗滿的完整 clip；`false` = continuity 斷裂或 idle timeout |

下游 `log_parse --filter type=clip` 與 `clip_store` 以 `session_id`、`ts`、`path` 欄位為主。

## Sentinel Behavior

`.pipeline_end` 表示上層已完成寫入。

收到 sentinel 後不會立刻退出；先繼續 drain `.meta.jsonl` 到 EOF，再 flush final clip。

若 process 啟動時 sentinel 已存在，`stream_merge` 仍會先把目前 meta 內容 drain 完再退出。

## Stdout / Stderr Rule

- stdout：只放 JSON Lines。
- stderr：log、warning、debug、error（via `stream_logger`）。

## Implementation Notes

- `IN_MODIFY` 事件可能被 kernel 合併，不可假設事件數等於 write 次數。
- 每次被喚醒後都應讀到 `read()` 暫時沒有更多資料為止。
- EOF 在看到 sentinel 前不代表 session 結束，只代表目前還沒有新資料。
- `drain_meta()` 以 `\n` 分隔切行，partial line 保留在 buffer 等下次 append。
- idle timeout poll interval = `min(idle_remaining_ms, 1000ms)`，用 `pipeline_get_monotonic_time_ms()` 計算。
- `.bin` fd 僅用於驗證存在；v2.1 不讀取 binary content。

## Future Work（v2.1 明確不做）

- **CRC32 驗算**：v2.1 meta record 無 `crc32` 欄位；驗算留 future work。
- **Binary clip 實際抽出**：`path` 為 synthetic；未真正切出可播放小檔。
- **Events extraction**：meta record 的 `events` 陣列解析留 future work。
- **UDP/亂序 late-packet handling**：continuity 斷裂直接輸出 partial clip，不做 reorder buffer。

## Local Test Focus

- 新 meta records 到達後從正確 offset 繼續讀。
- 多次 write 被合併成一次 event 時不漏 record。
- sentinel 後會 drain final meta records 並 flush clip。
- continuity 斷裂時輸出 partial clip。
- stdout 不混入 log。

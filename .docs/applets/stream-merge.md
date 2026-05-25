# stream_merge

`stream_merge` 讀取 session 的 sidecar metadata，透過 FSM 管理時間窗與連續性狀態，並輸出 clip JSON Lines。

## 架構定位

`stream_merge` 是 pipeline 的 source transformer。它讀取上游 ingestor 寫出的 append-only 檔案，將 filesystem updates 轉換成 stdout 上的 `clip` metadata stream。

它是嚴格的 file-based applet，不處理 socket，也不接 RTP/UDP。檔案事件由 `inotify` + `poll` 驅動。

## 輸入

```text
{src_dir}/{session_id}.bin
{src_dir}/{session_id}.meta.jsonl
{src_dir}/.pipeline_end
```

## 上游交接 Contract

`stream_merge` 透過 filesystem 與上游 ingestor 串接，不透過 direct function call 或 socket API。

上游 ingestor（例如 RTP/UDP receiver demo）對每個 session 必須做三件事：

1. append payload bytes 到 `{session_id}.bin`。
2. 每個 payload chunk append 一行 metadata 到 `{session_id}.meta.jsonl`。
3. session 結束後建立 `.pipeline_end`。

metadata row 是上游與 `stream_merge` 的同步 contract。`offset` / `length` 指向大型 session-level `.bin` 內的 byte range；`sequence` / `ts_ms` 讓 `stream_merge` 判斷 clip 邊界。

安全寫入順序：

```text
append 4096 bytes to sess.bin
append {"kind":"data","sequence":1,"offset":0,"length":4096,"ts_ms":1000}\n to sess.meta.jsonl

append 4096 bytes to sess.bin
append {"kind":"data","sequence":2,"offset":4096,"length":4096,"ts_ms":2000}\n to sess.meta.jsonl

touch .pipeline_end
```

建議規則：先寫 `.bin` payload，再寫對應 metadata row。這能保證 `stream_merge` 看到 metadata 時，被引用的 byte range 已存在。

## Metadata 格式

```json
{"kind":"data","sequence":1,"offset":0,"length":4096,"ts_ms":1747065600000,"events":["motion"]}
```

- `sequence`：單調遞增 chunk id，可跳號，但不可回頭。
- `offset` / `length`：session-level `.bin` 內的 byte range。
- `ts_ms`：來源端 timestamp，單位 milliseconds。
- `events`：未來可選欄位；目前暫不解析 nested array。

## 輸出

`stream_merge` 對 stdout 輸出單行 JSON record，代表一段 clip byte range：

```json
{"type":"clip","session_id":"sess","ts":1747065600,"path":"/tmp/stream/sess/sess_1747065600.bin","offset":0,"length":4096,"complete":true}
```

`stream_merge` 不產生新的 media file。輸出 record 是指向原始 session-level `.bin` 的 metadata pointer；下游可依 `path`、`offset`、`length` 做 on-demand extraction 或 mux。

## 下游 Media Handling

聚合後的 `.bin` range 不在 `stream_merge` 內轉檔。media extraction 是 downstream concern。

建議後續 stage：

- `clip_store`：儲存 clip JSON record，key 可用 `session_id:ts`。
- `clip_extract` / `clip_mux`：讀取 clip record，從 `.bin` 擷取 byte range，輸出 standalone artifact。
- `ffmpeg`：可由 extractor/muxer 作為 third-party tool 使用，例如 raw H.264 -> MP4，AAC -> M4A。

概念流程：

```text
stream_merge | log_parse --filter type=clip | clip_store
clip_store --get sess:1747065600 -> clip_extract -> output.mp4
```

這樣能讓 `stream_merge` 專注在 stream aggregation，不把 codec/container 細節塞進核心 pipeline。

## 核心 FSM

`stream_merge` 的核心是 `sm_fsm`。它只根據 `sequence`、`offset`、`ts_ms` 與當前狀態判斷 clip 邊界。

1. **Collecting**：當 `sequence == expected_seq` 且 `offset == expected_offset` 時，累積 `length`。
2. **RejectLateChunk**：若 `sequence < expected_seq`，視為 late/duplicate chunk，直接忽略。
3. **EmitComplete**：若 `ts_ms - start_ts_ms` 達到 `--clip-secs`，輸出 `complete: true` clip。
4. **EmitPartial**：若 `sequence > expected_seq` 或 `offset` 不連續，輸出 `complete: false` partial clip，並從新 chunk 重新開始。
5. **Idle Timeout**：若超過 `--idle-secs` 沒有新資料，將目前累積資料輸出為 partial clip。

## 模組結構

```text
applets/stream_merge/
  stream_merge.c   CLI、inotify/poll、主 I/O 流程
  sm_fsm.*         gap-aware clip FSM，不碰 file I/O
  sm_reader.*      JSONL scalar 欄位解析與 meta_record validation
  sm_events.*      未來 event tag aggregation 的小型 set helper
```

## v2.3 Non-Goals

- 不處理 RTP socket；RTP receive/reorder 屬於上游 ingestor。
- 不做 `.bin` -> MP4/MP3；media muxing 屬於 downstream extractor/muxer。
- 不做 CRC32 validation；目前不是課程要求，可視需求後續補。

## Dependencies

- 使用 `libpipeline.h` 的 dynamic buffer、inotify watch、sentinel helper。
- 使用 repo 內 embedded cJSON，並透過 `jsonl_codec` scalar helpers 讀 metadata。
- 不修改 `log_parse` 或 `clip_store` 的內部行為。

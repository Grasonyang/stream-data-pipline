# Core Overview

`ws_pipeline_dispatcher` 是 C / UNIX pipeline repo。它不接 WebSocket，也不解析 ESP32 packet。

## Input Layout

```text
/tmp/stream/{session_id}/
  {session_id}.bin
  {session_id}.meta.jsonl
  .pipeline_end
```

- `.bin`：session-level binary video buffer，由上層 append DATA payload。
- `.meta.jsonl`：每個 DATA chunk 的 metadata，canonical 欄位是 `sequence`、`offset`、`length`、`ts_ms`。
- `.pipeline_end`：上層收到 `END_` 且 artifact 寫完。

## Pipeline

```text
pipeline_dispatcher
  -> stream_merge
  -> log_parse --filter type=clip
  -> clip_store
```

## Responsibilities

- `pipeline_dispatcher`：fork、pipe、exec、waitpid。
- `stream_merge`：讀 sidecar，做時間窗與 continuity 檢查，輸出 clip metadata。
- `log_parse`：解析 structured logs，或過濾 JSONL records。
- `clip_store`：寫入純文字 clip index，支援 TTL/GC。

## Non-Goals

- WebSocket server。
- ESP32 packet parser。
- UDP reorder / late packet handling。
- 真正切出 playable mp4 clip。
- benchmark、final report evidence、compatibility matrix。

## Current Future Work

- `GRA-30`：上層 `edge-ws-host` artifact contract 對齊。
- `GRA-26` / `GRA-27` / `GRA-28` / `GRA-29`：final evidence、demo、benchmark、compatibility。

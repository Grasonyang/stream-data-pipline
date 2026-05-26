# 課程要求對照

| 要求 | 狀態 | 證據 |
| --- | --- | --- |
| C applets | 已完成 | `stream_merge`、`log_parse`、`clip_store` 由 `make` 建置 |
| UNIX pipeline | 已完成 | `pipeline_dispatcher` 透過 `box` 單一執行檔建立 `stream_merge -> log_parse -> clip_store` |
| Process management | 已完成 | `pipe()`、`fork()`、`execv()`、`waitpid()`、signal cleanup |
| stdout/stderr discipline | 已完成 | applet tests + `stream_logger` |
| Structured parser | 已完成 | `log_parse --regex --fields --format json/csv` |
| Stream filter | 已完成 | `log_parse --filter type=clip` |
| Stream transform | 已完成 | `stream_merge` 從 `.meta.jsonl` 輸出 clip byte-range metadata |
| File-backed storage | 已完成 | `clip_store --db`、TTL、查詢、GC/compact |
| Shared C helpers | 已完成 | `libpipeline`、`stream_logger` |
| Demo/benchmark/final evidence | 進行中 | UDP demo scripts、benchmark script |
| Compatibility/man/help docs | 進行中 | `.docs/`、man pages、`--help` |

## 已知責任邊界

- 本 repo 不實作正式 WebSocket/RTP ingress；UDP scripts 僅作 demo 與 contract 展示。
- 本 repo 不解析 ESP32 專屬 packet header。
- `stream_merge` 不直接產生 playable MP4/MP3，而是輸出 `.bin` byte range metadata。
- 跨 repo artifact 細節以 Linear integration docs 為準。

# Compliance Summary

| Requirement | Status | Evidence |
| --- | --- | --- |
| C applets | Done | `stream_merge`, `log_parse`, `clip_store` build via `make` |
| UNIX pipeline | Done | `pipeline_dispatcher` creates `stream_merge -> log_parse -> clip_store` |
| Process management | Done | `pipe()`, `fork()`, `execv()`, `waitpid()` |
| stdout/stderr discipline | Done | applet tests + `stream_logger` |
| Structured parser | Done | `log_parse --regex --fields --format json/csv` |
| Stream filter | Done | `log_parse --filter type=clip` |
| Stream transform | Done | `stream_merge` emits clip byte-range metadata from `.meta.jsonl` |
| File-backed storage | Done | `clip_store --db`, TTL, GC |
| Shared C helpers | Done | `libpipeline`, `stream_logger` |
| Demo/benchmark/final evidence | Planned | v2.2 work |
| Compatibility/man/help docs | Planned | v2.2 work |

## Known Scope Boundaries

- This repo does not implement WebSocket ingress.
- This repo does not parse ESP32 packet headers.
- `stream_merge` does not physically cut mp4 files in v2.1.
- Cross-repo artifact details live in Linear.

# stream_merge

Reads session sidecar metadata and emits clip JSON Lines.

## Input

```text
{src_dir}/{session_id}.bin
{src_dir}/{session_id}.meta.jsonl
{src_dir}/.pipeline_end
```

Meta record format:

```json
{"kind":"data","sequence":1,"offset":0,"length":4096,"ts_ms":1747065600000}
```

`sequence` is the canonical field. Old `seq` records are malformed and skipped.

## Output

```json
{"type":"clip","session_id":"sess","ts":1747065600,"path":"/tmp/stream/sess/sess_1747065600.bin","offset":0,"length":4096,"complete":true}
```

## Rules

- Opens `.bin` to verify existence; v2.1 does not read video bytes.
- Reads `.meta.jsonl` incrementally.
- Emits complete clips when `ts_ms` span reaches `--clip-secs`.
- Emits partial clips on `sequence` or `offset` continuity break.
- Emits partial clips on idle timeout.
- After `.pipeline_end`, drains remaining metadata and flushes the final clip.
- Logs only to stderr.

## Not Implemented

- CRC verification.
- Event merge.
- UDP reorder handling.
- Physical mp4 extraction.

# stream_merge

Reads session sidecar metadata, manages time/continuity state via a Finite State Machine (FSM), and emits clip JSON Lines.

## Architecture

`stream_merge` is the source transformer in the pipeline. It reads append-only files written by an upstream network ingestor (e.g., an RTP receiver) and converts physical file updates into a stream of logical `clip` metadata events on `stdout`.

It is **strictly file-based** and handles no network sockets. It relies on `inotify` and `poll`.

## Input

```text
{src_dir}/{session_id}.bin
{src_dir}/{session_id}.meta.jsonl
{src_dir}/.pipeline_end
```

## Upstream Handoff Contract

`stream_merge` connects to the upstream ingestor through the filesystem, not through a socket or direct API call.

The upstream ingestor, such as an RTP/UDP receiver, must do exactly three things for each session:

1.  Append payload bytes to `{session_id}.bin`.
2.  Append one metadata row to `{session_id}.meta.jsonl` for each payload chunk.
3.  Create `.pipeline_end` after the session is complete.

The metadata row is the synchronization contract between upstream and `stream_merge`. `offset` and `length` point into the large session-level `.bin`; `sequence` and `ts_ms` let `stream_merge` decide clip boundaries.

Example upstream write order:

```text
append 4096 bytes to sess.bin
append {"kind":"data","sequence":1,"offset":0,"length":4096,"ts_ms":1000}\n to sess.meta.jsonl

append 4096 bytes to sess.bin
append {"kind":"data","sequence":2,"offset":4096,"length":4096,"ts_ms":2000}\n to sess.meta.jsonl

touch .pipeline_end
```

`stream_merge` watches the directory and sidecar file with `inotify`, drains new metadata lines, and emits clip JSON Lines to stdout. The upstream does not need to call `stream_merge` functions directly.

Recommended safety rule: write payload bytes to `.bin` before appending the matching metadata row. This guarantees that when `stream_merge` observes the metadata row, the referenced byte range already exists.

The upstream ingestor must write `meta_record` lines to `.meta.jsonl`.
Format:
```json
{"kind":"data","sequence":1,"offset":0,"length":4096,"ts_ms":1747065600000,"events":["motion"]}
```
*   `sequence`: Monotonically increasing chunk identifier.
*   `offset` / `length`: Byte range inside the session-level `.bin` buffer.
*   `ts_ms`: Source timestamp in milliseconds.
*   `events`: Optional future array of event strings to aggregate into the emitted clip.

## Output

Emits single-line JSON records to `stdout` representing a clip byte-range.

```json
{"type":"clip","session_id":"sess","ts":1747065600,"path":"/tmp/stream/sess/sess_1747065600.bin","offset":0,"length":4096,"complete":true}
```

`stream_merge` does not physically create a new media file. The output record is a pointer into the original session-level `.bin` file. Downstream tools can use `path`, `offset`, and `length` to extract or mux the byte range on demand.

## Downstream Media Handling

The aggregated `.bin` range is intentionally not converted by `stream_merge`. Media extraction is a separate downstream concern.

Recommended follow-up stages:

*   `clip_store`: Stores the clip JSON record using a stable key such as `session_id:ts`.
*   `clip_extract` or `clip_mux`: Reads a stored clip record, extracts the byte range from `.bin`, and writes a standalone artifact.
*   `ffmpeg`: Optional third-party tool used by `clip_extract`/`clip_mux` for container remuxing, e.g. raw H.264 to MP4 or AAC to M4A.

Example concept:

```text
stream_merge | log_parse --filter type=clip | clip_store
clip_store --get sess:1747065600 -> clip_extract -> output.mp4
```

This keeps `stream_merge` focused on stream aggregation and keeps codec/container details outside the core pipeline.

## Core FSM (Gap State Machine)

The heart of `stream_merge` is `sm_fsm`, a mathematical state machine that enforces data continuity based solely on the `sequence` and `ts_ms` fields.

1.  **Collecting**: Accumulates `length` as long as `sequence == expected_seq`.
2.  **RejectLateChunk (Deduplication)**: If `sequence < expected_seq`, the chunk is an out-of-order duplicate. The FSM ignores it.
3.  **EmitComplete**: If the accumulated time (`ts_ms - start_ts_ms`) reaches the target window (e.g., 5 seconds), the FSM emits a `complete: true` clip and resets.
4.  **EmitPartial (Gap Detection)**: If `sequence > expected_seq`, a network packet loss (gap) occurred. The FSM immediately emits the accumulated data as a `complete: false` partial clip and resets.
5.  **Idle Timeout**: If no data arrives within the idle window (e.g., 2 seconds), the FSM flushes the current buffer as a partial clip.

## Module Structure (v2.3)

*   `stream_merge.c`: UNIX orchestration (CLI, `inotify`, `poll`, non-blocking I/O).
*   `sm_fsm.c` / `sm_fsm.h`: The Core State Machine (No file I/O).
*   `sm_reader.c` / `sm_reader.h`: JSONL parsing and normalized `meta_record` validation.
*   `sm_events.c` / `sm_events.h`: String set for future event aggregation.

## Non-Goals For v2.3

*   No RTP socket handling. RTP receive/reorder belongs to the upstream ingestor.
*   No `.bin` to MP4/MP3 conversion. Media muxing belongs to a downstream extractor/muxer.
*   No CRC32 validation. It can be added later if required, but it is not part of the current assignment scope.

## Dependencies

- Uses `libpipeline.h` for buffer and sentinel management.
- Zero external dependencies (cJSON is embedded).
- Does not modify existing applets (`log_parse`, `clip_store`).

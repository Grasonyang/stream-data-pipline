# pipeline_dispatcher

Builds the fixed three-process pipeline:

```text
stream_merge | log_parse --filter type=clip | clip_store
```

## CLI

```text
pipeline_dispatcher <session_id> <src_dir> <db_path> <ttl_seconds>
```

## Behavior

- Resolves sibling binaries from its own executable path.
- Creates two pipes.
- Forks and execs the three applets.
- Closes unused pipe fds in parent and children.
- Waits for all children.
- Returns success only when all children exit `0`.

## Failure Model

- Bad args: exits `2`.
- Setup/spawn failure: kills and reaps already-started children.
- Child non-zero or signal death: parent exits non-zero.

# clip_store

File-backed clip index.

## DB Format

```text
key<TAB>value<TAB>expire_at<NEWLINE>
```

- key: `session_id:ts`
- value: clip path/reference from input JSON
- `expire_at=0`: never expires

## Commands

```text
clip_store --db <path> --ttl <seconds>
clip_store --db <path> --get <key>
clip_store --db <path> --gc
```

## Behavior

- Append mode reads clip JSON Lines from stdin.
- `--get` returns live records only.
- `--gc` rewrites through temp file + rename.
- Uses file locking for write paths.

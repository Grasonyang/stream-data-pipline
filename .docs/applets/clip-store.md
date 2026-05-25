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
clip_store --db <path> --set <key=value>
clip_store --db <path> --get <key>
clip_store --db <path> --list
clip_store --db <path> --prefix <session_id:>
clip_store --db <path> --delete <key>
clip_store --db <path> --compact
clip_store --db <path> --gc
```

## Behavior

- Append mode reads clip JSON Lines from stdin.
- `--set` appends one key-value row.
- `--get` returns live records only.
- `--list` and `--prefix` show live records only.
- `--delete` appends tombstone rows (empty value).
- `--compact`/`--gc` rewrites through temp file + rename.
- Compaction drops expired rows, tombstones, and overwritten old rows.
- Query and compaction rebuild an in-memory hash index of latest key states.
- Uses file locking for write paths.

# clip_store

`clip_store` 是 pipeline 終端的 file-backed clip index。它從 stdin 接收 clip JSON Lines，寫入 append-only 純文字 DB，並提供查詢、TTL、delete tombstone 與 compact/GC。

## DB Format

```text
key<TAB>value<TAB>expire_at<NEWLINE>
```

- `key`：預設為 `session_id:ts`。
- `value`：clip path/reference，目前由 input JSON 的 `path` 欄位提供。
- `expire_at=0`：永不過期。

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

## 行為

- 預設 append mode 從 stdin 讀取 clip JSON Lines。
- `--set` append 一筆 key-value row。
- `--get` 只回傳 latest live record。
- `--list` 與 `--prefix` 只顯示 live records。
- `--delete` append tombstone row（空 value）。
- `--compact` / `--gc` 透過 temp file + rename 重寫 DB。
- compaction 會丟棄 expired rows、tombstones、以及被覆蓋的舊 rows。
- query 與 compaction 都會重建 in-memory hash index，保留每個 key 的 latest state。
- write path 使用 file locking，避免並發寫入破壞 DB。

## Pipeline 用法

```text
stream_merge ... | log_parse --filter type=clip | clip_store --db clips.db --ttl 300
```

`clip_store` 只儲存 metadata reference，不負責讀取或轉換 `.bin` media bytes。

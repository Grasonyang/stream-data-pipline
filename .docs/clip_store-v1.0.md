# clip_store

`clip_store` 是 pipeline 的終端持久化層。它從 stdin 讀取 clip JSON Lines，寫入純文字 index。

## Responsibilities

- 從 stdin 讀取 filtered clip JSON Lines。
- 用 `session_id:ts` 建立 key。
- 用 `path` 作為 value。
- 寫入 `--db` 指定的純文字 index。
- 支援 TTL。
- 提供 repo-local 查詢 / GC CLI。
- 解析失敗的輸入行寫 stderr warning，並略過該行。

## Required Integration Mode

```text
clip_store --db <db_path> --ttl <ttl_seconds>
```

stdin input example：

```json
{"type":"clip","session_id":"sess_001","ts":1747065600,"path":"/tmp/clips/sess_001/1747065600.mp4"}
```

## DB Format

`/tmp/clips.db` 是純文字檔，不是 SQLite。
範例：

```text
sess_001:1747065600	/tmp/clips/sess_001/1747065600.mp4 1747069200
```

## Write Semantics

- key 格式：`session_id:ts`。
- 相同 key 再寫入採 latest-write-wins 語意；`--get` 會掃描最後一筆該 key。
- 寫入路徑目前是 append-only；`--gc` 會重寫檔案，保留每個 key 最新且未過期的 row。
- `expire_at` 由每次寫入當下時間加上 `--ttl` 決定。
- `--ttl 0` 表示永不過期，寫入 `expire_at = 0` 作為 sentinel；`--get` 與 `--gc` 均以 `expire_at == 0` 判斷為有效 row，不會視為過期。
- `--gc` 使用 tmp file + rename：先將存活 rows 寫入 `db_path.tmp`，`fsync()` 後以 `rename()` 原子取代原檔，crash 不會造成 DB 遺失。

## Optional Repo-Local Modes

以下 CLI 是 repo 內部管理用途，不是跨 repo contract：

- `--get <key>`
- `--gc`

目前尚未實作：

- `--list`
- `--delete`

## Stdout / Stderr Rule

- stdout：`--get` 時輸出查詢到的 raw path；一般 ingest / `--gc` 成功時不輸出內容。
- stderr：diagnostic log 與 malformed clip warning。

因此它目前比較像 pipeline sink + repo-local maintenance CLI，而不是完整 CRUD shell tool。

## Locking And Expiry

- process 啟動後會對 db 檔案拿 `flock(LOCK_EX)`，避免多個 writer 同時破壞檔案內容。
- `--get` 只回傳尚未過期的最新 row；過期資料即使還在檔案中，也不會回傳。
- `--gc` 才會實際把過期資料與舊版本 row 從檔案中清掉。

## Local Test Focus

- pipe 寫入單筆 clip。
- 相同 key upsert 後查詢最新值。
- TTL 過期後查詢不回傳有效結果。
- GC 去重與清理過期資料。
- concurrent writer 不破壞 db。

# stream-data-pipeline 文件

這裡只放 repo 內部文件。跨 repo contract 以 Linear integration docs 為準。

## 先讀這三份

1. `README.md`：專案目的、build/test/demo。
2. `core/overview.md`：本 repo 負責什麼、不負責什麼、資料怎麼流。
3. `core/compliance.md`：課程要求對照與剩餘交付項。

## 模組文件

- `applets/pipeline-dispatcher.md`：建立 `box stream_merge | box log_parse | box clip_store`。
- `applets/stream-merge.md`：讀 `.meta.jsonl`，輸出 clip byte-range metadata。
- `applets/log-parse.md`：regex parse 與 JSONL filter。
- `applets/clip-store.md`：file-backed clip index、TTL、GC。

## Repo 目錄

- `applets/`：正式 applet 與 `main.c` dispatcher entrypoint。
- `lib/`：共享 helper 與 codec 模組。
- `tests/`：lib/applet 單元測試與 shell 整合測試。
- `scripts/`：benchmark、UDP demo、applet examples。
- `man/`：四個 applet 的 man pages。
- `.third-party/`：vendored `cJSON`、`miniz`。

## 文件規則

- 不在 repo docs 重複 Linear 的 cross-repo 規格。
- 不寫尚未實作的長篇設計。
- 若 README、repo docs、Linear 衝突：Linear 管跨 repo contract，repo docs 管本 repo 實作。

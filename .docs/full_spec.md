# ws_pipeline_dispatcher Internal Spec

本文件只描述 `ws_pipeline_dispatcher` repo 內部如何工作。跨 repo contract 請看 Linear：

- [Simplified Integration Spec](https://linear.app/grason/document/simplified-integration-spec-bcfbf6ee3e4c)

若本文件與 Linear 衝突，以 Linear 為準。

## Repo Role

`ws_pipeline_dispatcher` 是 C / UNIX pipeline。它不接 WebSocket，也不負責 ESP32 packet parsing。

它負責：

- 接收 `edge-ws-host` 透過 `spawn()` 傳入的 CLI 參數。
- 讀取 `/tmp/stream/{session_id}/{session_id}.bin`。
- 監聽 `.pipeline_end` 作為 session 結束訊號。
- 產生 clip JSON Lines。
- 過濾 `type=clip`。
- 寫入 `/tmp/clips.db` 純文字 index。

## Non-Goals

以下不屬於這個 repo 的責任：

- 建立 WebSocket server 或管理 client connection。
- 解析 ESP32 原始封包格式。
- 定義跨 repo runtime sequence。
- 追蹤所有 integration mismatch；這類專案層級資訊以 Linear 為主。
- 在 v2.0 提供 benchmark numbers、Toybox/GNU compatibility matrix 或完整 man pages。

## Internal Pipeline

```text
pipeline_dispatcher
  ├─ stream_merge  stdout ── pipe_1 ──> log_parse
  ├─ log_parse     stdout ── pipe_2 ──> clip_store
  └─ clip_store    writes /tmp/clips.db
```

每個 process 的角色：

- `pipeline_dispatcher`：建立 pipe、spawn child、回收 exit status。
- `stream_merge`：從 growing file 讀新資料，等到 sentinel 出現後 drain 最後 bytes，輸出 clip-oriented JSON Lines。
- `log_parse`：對上游 structured records 做 parse / filter / reformat。
- `clip_store`：把 clip records 寫入 file-backed index，並支援 TTL / GC。

## Runtime Data Flow

正常資料流如下：

1. 上層先建立 `/tmp/stream/{session_id}/` 與對應資料檔。
2. `pipeline_dispatcher` 以 CLI 參數啟動三個 applet。
3. `stream_merge` 持續讀 `{session_id}.bin` 的新增內容。
4. `stream_merge` 把辨識出的 clip records 輸出到 stdout。
5. `log_parse` 從 stdin 讀取 records，保留 `type=clip` 或做格式轉換。
6. `clip_store` 從 stdin 讀取 clip JSON Lines，寫入 `db_path`。
7. `.pipeline_end` 出現後，`stream_merge` drain 剩餘 bytes 並結束；下游跟著 EOF 收束。

這個 repo 的重點不是 packet ingress，而是把已存在的 session data 透過 UNIX pipeline 收斂成可查詢的 clip index。

## Runtime Inputs

`pipeline_dispatcher` 由 `edge-ws-host` 在 `STRT` 後啟動。

```text
pipeline_dispatcher <session_id> <src_dir> <db_path> <ttl_seconds>
```

目前約定：

- `session_id` 等同 `eventId`。
- `src_dir` 是 `/tmp/stream/{session_id}/`。
- `db_path` 預設是 `/tmp/clips.db`。
- `ttl_seconds` 由上層決定。

若上層想調整啟動時機、CLI 來源或 session layout，這屬於 integration contract，必須先改 Linear 文件。

## Filesystem Assumptions

本 repo 假設上層已建立以下 layout：

```text
/tmp/stream/{session_id}/
    {session_id}.bin
    {session_id}.meta.jsonl
    .pipeline_end
```

`{session_id}.bin` 是 append-only growing blob。`stream_merge` 以 tail-read 方式讀取新增 bytes。

`.pipeline_end` 表示上層已完成寫入。它不是啟動 pipeline 的 trigger。

`clip_store` 目前把 clips 寫到單一 file-backed index，例如 `/tmp/clips.db`。這是 repo-local storage artifact，不等於跨 repo database contract。

## Applet Responsibility Split

- `pipeline_dispatcher`
  - 建立 `pipe()`。
  - `fork()` / `execv()` child processes。
  - 關閉不需要的 fd。
  - `waitpid()` 收斂 child exit status。
- `stream_merge`
  - 監聽 growing file 追加內容。
  - 感知 sentinel，決定何時 drain 並正常退出。
  - 保持 stdout 為 structured records，不輸出診斷文字。
- `log_parse`
  - 支援 regex parse mode 與 integration filter mode。
  - 支援 JSON / CSV output 與欄位選取。
  - 負責資料過濾與轉換，不負責持久化。
- `clip_store`
  - 支援 append / get / gc 相關 CLI 路徑。
  - 管理 TTL 與 file-backed index 格式。
  - 對文件要誠實，不應描述尚未完成的完整 CRUD 或 crash-safety。

## Shared Library Responsibility Split

- `libpipeline`
  - 放 repo 內 applet 共用的 POSIX / inotify / sentinel helper。
  - 不解析 JSON，不持有業務資料格式知識。
- `stream_logger`
  - 提供 stderr-only diagnostic logging。
  - 保護 stdout 不被 log 汙染。

這兩個 library 的定位是支援 applet，不是獨立對外 API 產品。

## Stdout / Stderr Rule

stdout 是資料流，只能放下一層要讀的 structured output。

stderr 是診斷流，所有 log、warning、error 都必須走 stderr。

這條規則很重要，否則 UNIX pipe 會被 log 污染。

對這個 repo 而言，這是一條 architecture rule，不只是 coding style。任何新功能若需要額外輸出資訊，都應先判斷那是資料還是診斷訊息。

## Process Topology And Failure Model

- pipeline 是固定三段：`stream_merge | log_parse | clip_store`
- parent process 只負責 orchestration，不處理 clip payload
- 任一 child 提前失敗時，pipe 會自然斷開，其他 child 應能觀察到 EOF 或 write failure 並收束
- `pipeline_dispatcher` 最終用 exit code 對上層回報 pipeline 是否成功

## Error Propagation

任一 child process 異常時，pipe 會自然 EOF，其他 child 會收束。`pipeline_dispatcher` 用 `waitpid()` 收集狀態，並用 exit code 回報給上層。

repo docs 應只描述目前實作有的 error model：

- 參數錯誤
- `pipe()` / `fork()` / `execv()` 失敗
- child process 非 0 exit
- child process 被 signal 終止

更細的 retry policy、上層補償機制或 session restart 策略，不在這個 repo 內定義。

## Build, Test, And Demo Relationship

- `Makefile` 提供 repo-local build / test / smoke 入口。
- `make test` 是目前最主要的 correctness evidence。
- README 的 quick demo 用來說明最小可執行路徑，不等於 final demo evidence。
- benchmark、compatibility matrix、report-ready demo artifacts 屬於後續 v2.2 收斂。

因此 `full_spec` 應說明 repo 如何工作，但不應把尚未補齊的 benchmark/man/help 說成已完成交付。

## What Belongs Here

- applet CLI 與 exit code。
- repo-local build/test/run 注意事項。
- C 實作細節與內部資料結構。
- debug、logging、failure handling。
- repo 內部 architecture、責任切分與資料流。

## What Belongs In Linear

- `edge-ws-host` 與本 repo 的 contract。
- packet format。
- filesystem layout 的跨 repo 決策。
- runtime sequence。
- current gaps / project tracking。

## Reading Path

如果是第一次進 repo，建議順序：

1. `README.md`：先看課程主軸、build/test/demo。
2. `.docs/compliance-matrix.md`：確認哪些要求已完成、哪些是 follow-up。
3. `.docs/full_spec.md`：理解 repo role、pipeline 結構與內部邊界。
4. applet / library spec：進入單一模組細節。

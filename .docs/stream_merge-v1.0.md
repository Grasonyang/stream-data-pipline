# stream_merge

`stream_merge` 是 pipeline 的唯一 stream reader。它讀取上層持續 append 的 `{session_id}.bin`，輸出 clip JSON Lines 到 stdout。

## Responsibilities

- 開啟 `{src_dir}/{session_id}.bin`。
- 監聽檔案 `IN_MODIFY` / writer close，並監聽目錄中的 `.pipeline_end`。
- 從上次 offset 繼續 tail-read。
- 在內部 buffer 累積 bytes。
- 以 brace-balanced JSON object framing 從 growing blob 切出完整 object。
- 只輸出看起來是 `"type":"clip"` 的 object；其他 object 直接忽略。
- 每個通過條件的 object 原樣輸出到 stdout，並補一個 `\n`。
- 偵測 `.pipeline_end` 後 drain 剩餘 bytes，flush final clip，exit 0。

## Inputs

```text
stream_merge --src <src_dir> --session <session_id>
```

預期檔案：

```text
{src_dir}/{session_id}.bin
{src_dir}/.pipeline_end
```

## Output Contract

stdout 每行是一個完整 JSON object，並以 `\n` 結尾。

目前實作不重建 schema，而是直接轉送從 blob 中切出的完整 JSON object。只要 object 內可辨識 `type=clip`，就會被輸出給下游 `log_parse --filter type=clip` / `clip_store`。

常見輸出 shape：

```json
{"type":"clip","session_id":"sess_001","ts":1747065600,"path":"/tmp/clips/sess_001/1747065600.mp4"}
```

## Sentinel Behavior

`.pipeline_end` 表示上層已 close write stream。

收到 sentinel 後不應立刻退出；必須先繼續 read 到 EOF，再 flush final clip。

若 process 啟動時 sentinel 已存在，`stream_merge` 仍會先把目前檔案內容 drain 完再退出。

## Stdout / Stderr Rule

- stdout：只放 JSON Lines。
- stderr：log、warning、debug、error。

## Implementation Notes

- `IN_MODIFY` 事件可能被 kernel 合併，不可假設事件數等於 write 次數。
- 每次被喚醒後都應讀到 `read()` 暫時沒有更多資料為止。
- EOF 在看到 sentinel 前不代表 session 結束，只代表目前還沒有新資料。
- object framing 目前只靠大括號深度與字串跳脫處理，不做完整 JSON schema 驗證。
- 目前沒有 state file / crash recovery cursor；重啟後會從檔案開頭重新掃描。

## Local Test Focus

- append bytes 後會從正確 offset 繼續讀。
- 多次 write 被合併成一次 event 時不漏資料。
- sentinel 後會 drain final bytes。
- stdout 不混入 log。

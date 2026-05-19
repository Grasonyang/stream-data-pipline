# log_parse

`log_parse` 是 stdin -> stdout 的 pipe-friendly 結構化日誌解析器。

它需要同時滿足兩個需求：

- 課程方向三「Embedded Data Pipeline」：支援 regex-based 解析、JSON/CSV 輸出、欄位選取與條件過濾。
- 本 repo integration pipeline：支援 `log_parse --filter type=clip`，保留 `stream_merge` 送出的 clip JSON Lines 給 `clip_store`。

## Responsibilities

- 從 stdin 逐行讀取 input line。
- 支援 regex-based parsing，將 raw log line 轉成 structured record。
- 支援輸出 `--format json` 與 `--format csv`。
- 支援 `--fields <f1,f2,...>` 欄位選取。
- 支援最小 filter expression：`--filter key=value`。
- integration mode 下，支援已是 JSON Lines 的 input，並用 `--filter type=clip` 過濾。
- regex / JSON line parse failure 走 stderr warning，並跳過該行。
- filter syntax error、CLI 組合錯誤與 invalid regex/format 直接回傳 non-zero。

## Course Tool Mode

課程要求的主要模式是結構化日誌解析器。最小 CLI shape：

```text
log_parse --regex <pattern> --fields <f1,f2,...> --format json [--filter key=value]
log_parse --regex <pattern> --fields <f1,f2,...> --format csv  [--filter key=value]
```

`--regex` 使用 POSIX Extended Regular Expressions (ERE)。實作應以
`regcomp(..., REG_EXTENDED)` 編譯 pattern，讓 `()`、`+` 等 ERE 語法與
文件範例一致。欄位名稱由 `--fields` 提供，依 `regmatch_t` 的
`match[1..N]` capture group 順序對應。

範例 input：

```text
1747065600 clip /tmp/clips/sess_001/1747065600.mp4
```

範例 command：

```text
log_parse --regex '^([0-9]+) ([a-z_]+) (.+)$' --fields ts,type,path --format json --filter type=clip
```

範例 output：

```json
{"ts":"1747065600","type":"clip","path":"/tmp/clips/sess_001/1747065600.mp4"}
```

目前 regex mode 的 JSON output 會把 capture group 全部當成字串輸出，不會自動把數字欄位轉成 JSON number。

## Integration Mode

`pipeline_dispatcher` 串接時的必要模式：

```text
log_parse --filter type=clip
```

在這個模式下，stdin 預期是 JSON Lines。`log_parse` 不改寫通過 filter 的 line，直接 pass-through 給 `clip_store`。

目前的 JSON line 判定與 filter 是最小實作：

- line 去掉前後空白後必須以 `{` 開頭、`}` 結尾。
- `--filter key=value` 只比對 JSON string value，不支援數字、巢狀欄位或複合 expression。
- 若 key 不存在或值不相等，該行會被靜默丟棄，不視為錯誤。

範例 input：

```json
{"type":"clip","session_id":"sess_001","ts":1747065600,"path":"/tmp/clips/sess_001/1747065600.mp4"}
{"type":"heartbeat","session_id":"sess_001","ts":1747065601}
```

範例 output：

```json
{"type":"clip","session_id":"sess_001","ts":1747065600,"path":"/tmp/clips/sess_001/1747065600.mp4"}
```

## JSON Handling Boundary

JSON parsing/filtering 屬於 `log_parse` 的責任，不屬於 `libpipeline`。

`libpipeline` 不引入 `cJSON`，也不提供 JSON compression helper。`log_parse` 目前在 applet 內用最小 key lookup 處理 JSON Lines，足夠支援 `type=clip` 過濾，但不是完整 JSON parser。

## Stdout / Stderr Rule

- stdout：只輸出通過 parse/filter 的 structured records。
- stderr：parse error、JSON error、filter syntax error、diagnostic log。

這條規則很重要，否則 UNIX pipe 會被 warning/log 污染。

## Exit Codes

- `0`：stdin EOF，正常結束。
- `1`：參數錯誤、CLI 組合錯誤、filter syntax error、invalid regex 或 unsupported format。
- `2`：stdin 讀取錯誤。

## Local Test Focus

- regex raw log input 可輸出 JSON。
- regex raw log input 可輸出 CSV。
- `--filter type=clip` 只保留 `type=clip` record。
- integration JSON Lines input 可 pass-through clip records。
- malformed input 不讓 process crash。
- stdout 不混入 warning 或 diagnostic log。

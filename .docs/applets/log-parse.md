# log_parse

`log_parse` 是 stdin -> stdout 的 structured record processor。它支援 regex 欄位提取、JSONL filter，以及輕量 count mode。

## Parse Mode

```text
log_parse --regex <pattern> --fields a,b,c --format json|csv
```

- 從 stdin 逐行讀取。
- 使用 POSIX extended regex。
- 將 capture groups 依序映射到 `--fields` 欄位名。
- 對 stdout 輸出 JSON Lines 或 CSV。
- regex capture value 一律視為字串。

## Filter Mode

```text
log_parse --filter <expr>
```

- 從 stdin 讀取 JSON Lines。
- 只保留 top-level scalar 欄位符合條件的 records。
- pipeline 中預設用法是 `log_parse --filter type=clip`。

支援的 filter expressions：

| Operator | 語意 | 範例 |
| --- | --- | --- |
| `=` | 等於 | `type=clip` |
| `!=` | 不等於 | `type!=heartbeat` |
| `>` | 數值大於 | `ts>1747065600` |
| `~` | 包含 substring | `path~/clips/` |

JSONL filter mode 預設會原樣 pass-through matching lines。

## Count Mode

```text
log_parse --filter type=clip --format count
log_parse --regex <pattern> --fields a,b,c --filter type=clip --format count
```

- 計算通過 filter 的 records 數量。
- 對 stdout 輸出一個十進位整數。
- regex parse mode 與 JSONL filter mode 都可使用。

## Format Rules

- `--format json` 與 `--format csv` 需要搭配 `--regex`。
- `--format count` 可搭配或不搭配 `--regex`。
- JSONL mode 若沒有 `--format count`，只做 pass-through，不把 JSONL 轉 CSV。

## 限制

- JSONL filter 只檢查 top-level fields。
- `>` 只接受 numeric values。
- `~` 會比對 scalar field 的文字表示。

## Stream Rule

- stdout：只輸出資料。
- stderr：只輸出 diagnostics。

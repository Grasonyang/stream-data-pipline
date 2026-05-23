# Compatibility Matrix — Toybox / GNU 工具對比

本文件說明三個 applet 與 GNU coreutils / Toybox 工具的 CLI 相容程度，
並標示無直接對應工具的比較限制。

測試環境：Ubuntu 22.04（GNU grep 3.7、GNU awk 5.1.0）；Toybox 未預裝，
行為參照 [Toybox 原始碼與文件](https://landley.net/toybox/)。

---

## 總表

| Applet | 最近似 GNU 工具 | 最近似 Toybox 工具 | 相容程度 |
|---|---|---|---|
| `log_parse` (filter mode) | `grep` | `grep` | 部分相容（差異見下） |
| `log_parse` (parse mode) | `awk` | `awk` | 功能相近，介面不同 |
| `clip_store` | 無直接對應 | 無直接對應 | 不適用（見比較限制） |
| `stream_merge` | 無直接對應 | 無直接對應 | 不適用（見比較限制） |

---

## log_parse — Filter Mode vs grep

### 相容行為

| 行為 | log_parse | GNU grep | Toybox grep |
|---|---|---|---|
| 無匹配時輸出空白 | ✅ | ✅ | ✅ |
| 匹配行原樣輸出 stdout | ✅ | ✅ | ✅ |
| 不匹配行不輸出 | ✅ | ✅ | ✅ |
| 診斷訊息寫 stderr | ✅ | ✅ | ✅ |
| 空輸入正常結束 | ✅（exit 0） | ✅（exit 1） | ✅（exit 1） |

### 差異行為（已驗證）

| 差異項目 | log_parse | GNU grep |
|---|---|---|
| **無匹配 exit code** | `0` | `1` |
| **數字型欄位匹配** | `--filter val=42` 不匹配 `{"val":42}`（僅比對字串值） | `grep '"val":42'` 匹配 |
| **匹配模式** | 精確 JSON 欄位語意比對（key=value） | 任意正規表達式，逐字元匹配整行 |
| **輸入格式** | 只接受 JSON Lines | 接受任何純文字 |

**無匹配 exit code 說明：**
`log_parse --filter` 在沒有任何記錄匹配時仍返回 exit 0，
與 `grep` 的 exit 1（無匹配）行為不同。
若要在 shell script 中依匹配結果分岐，需改用 `grep` 或以 `wc -l` 計算輸出行數。

**數字型欄位說明：**
`--filter key=value` 的 value 以字串形式比對，
JSON 中的數字值（無引號）無法透過此語法匹配，
需改用 `grep '"val":42'` 或 `jq` 做數值過濾。

### 使用等效指令

```bash
# log_parse --filter type=clip
grep '"type":"clip"'          # GNU / Toybox grep（行為接近但不等同）
jq 'select(.type == "clip")'  # 語意完全等同（需安裝 jq）
```

---

## log_parse — Parse Mode vs awk

### 相容行為

| 行為 | log_parse | GNU awk |
|---|---|---|
| 讀 stdin，寫 stdout | ✅ | ✅ |
| 多欄位提取 | ✅（regex capture groups） | ✅（`$1, $2...`） |
| CSV 輸出 | ✅（`--format csv`） | 需手動拼接 |

### 差異行為（已驗證）

| 差異項目 | log_parse | GNU awk |
|---|---|---|
| **欄位提取方式** | POSIX ERE capture groups（`(...)` 依序對應 `--fields`） | 空白/分隔符切割（`$1 $2...`） |
| **JSON 輸出** | 內建（`--format json`，預設） | 需手動 `printf "{...}"` |
| **CSV 輸出** | 內建（`--format csv`，含逗號跳脫） | 需手動處理逗號跳脫 |
| **尾端空白** | 無（regex 精確捕獲） | awk 多欄位拼接常有尾端空格（已驗證） |
| **行為宣告** | 只接受 POSIX ERE | 支援 awk 自有的模式語言 |

### 使用等效指令

```bash
# log_parse --regex '^(\S+) ([A-Z]+) (.+)$' --fields ts,level,msg --format json
awk '{printf "{\"ts\":\"%s\",\"level\":\"%s\",\"msg\":\"%s\"}\n", $1,$2,$3}'
# 注意：awk 版本需手動處理多詞 msg 欄位與 JSON 特殊字元跳脫
```

---

## clip_store — 比較限制

`clip_store` 是 pipeline-domain-specific 的 file-backed key-value index，
**GNU coreutils 與 Toybox 均無直接對應工具**。

以下說明比較方法論：

### 功能面等效分解

| clip_store 功能 | GNU shell 等效方式 |
|---|---|
| append（stdin → DB） | `awk '{print ...}' >> db.txt` |
| TTL 管理 | 無內建；需 `awk` 結合 `date` 手動過濾 |
| `--get key` 查詢 | `grep "^$key\t" db.txt \| tail -1 \| cut -f2` |
| `--gc` 垃圾回收 | 無原子性保證；需 `sort \| awk \| mv` 組合 |
| 並發寫入安全（flock） | 無；GNU `flock(1)` 需額外包裝 |

### 為何沒有直接對應工具

- GNU `dbm` / `gdbm` 提供 KV 儲存但不接受 stdin JSON Lines 輸入
- `tee` / `sponge` 只處理流，不做 TTL 或鍵值語意
- 嵌入式環境（BusyBox / Toybox）無對應的 KV store applet

---

## stream_merge — 比較限制

`stream_merge` 是處理 growing binary + sidecar 的 pipeline-specific 工具，
**GNU coreutils 與 Toybox 均無直接對應工具**。

### Pipeline 組合等效分析

本專案 pipeline：
```
stream_merge | log_parse --filter type=clip | clip_store
```

最接近的 GNU shell 等效組合：
```bash
inotifywait -m -e modify "$src_dir/$session.meta.jsonl" \
  | while read ...; do tail -n +$last "$src_dir/$session.meta.jsonl"; done \
  | grep '"type":"clip"' \
  | awk '{...}' >> clips.db
```

| 功能差異 | 本專案 | GNU shell 等效 |
|---|---|---|
| 增量讀取 sidecar | ✅（inotify + offset tracking） | 需 `inotifywait` + `tail -n +N` |
| 時間窗口切割（5s clip） | ✅（ts_ms 累積計算） | 無內建；需手動計算 |
| Continuity break 偵測 | ✅（sequence + offset 驗證） | 無內建 |
| sentinel 結束信號 | ✅（`.pipeline_end` 檔案） | 無標準化機制 |
| O(1) 記憶體使用 | ✅（streaming，不載入整個 sidecar） | 取決於實作 |
| 並發安全 | ✅（flock，binary file open） | 取決於實作 |

### 為何沒有直接對應工具

`stream_merge` 的語意是「從 append-only sidecar 重建時間對齊的 byte-range 邊界」，
這是 video streaming pipeline 的領域特定操作，不在 UNIX general-purpose 工具的設計範疇內。

---

## 對期末報告的說明建議

1. `log_parse` 可合理聲稱「功能上相容 `grep` filter 行為，並擴充 JSON-native 語意」
2. `clip_store` 與 `stream_merge` 應說明為「領域特定工具」，比較基準為 pipeline 組合而非單一 applet
3. exit code 差異（log_parse exit 0 vs grep exit 1 on no-match）是已知設計取捨，不影響 pipeline 組合，但需在報告中說明

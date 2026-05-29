# Compatibility Matrix — Toybox / GNU 工具對比

本文件說明三個 applet (`stream_merge`、`log_parse`、`clip_store`) 與
GNU coreutils / Toybox / 其他常見 UNIX 工具的 CLI 相容程度。
無直接對應的 applet，列出**最具比較意義的替代工具組合**，並標示
比較類型，避免在期末報告中做出不對等的論述。

> 本專案是 **BusyBox 嵌入式資料管線**，因此 benchmark 在
> `systemd-run --user --scope -p MemoryMax=64M -p CPUQuota=50% -p AllowedCPUs=0`
> 包裝下執行，對標**嵌入式 Linux 等級**硬體（OpenWrt / 入門級板子）。
> 此限制條件本身會影響「哪個對標工具有資格作為比較」——詳見最後章節
> 「資源受限對比較選擇的影響」。

---

## 比較類型定義

| 類型 | 定義 | 報告中可作的論述 |
|---|---|---|
| **A：CLI 行為等效** | 在相同輸入下產生相同輸出，CLI 介面語意可互換 | 「我們的工具 vs X，吞吐量比為 …」這類直接比較 |
| **B：問題域語意等效** | 解決同一問題但 CLI 不同（需手動拼接 flags） | 「在 JSON 過濾這個問題域，我們 vs X 為 …」 |
| **C：組合等效** | 沒有單一工具能對應；用 N 個 GNU/Toybox 工具拼湊出近似行為 | 「以 GNU shell pipeline 模擬同等行為，我們 vs 拼湊組合為 …」 |
| **D：參考性對照** | 解決的是不同問題（如 byte-level 子字串 vs JSON 語意） | 「僅供參考；非公平基準」 |

---

## 總表

| Applet | 最近似 GNU | 最近似 Toybox | 替代比較目標（含 type） | 結論 |
|---|---|---|---|---|
| `log_parse` filter mode | `grep` | `grep` | `jq -c 'select(.k==v)'` (B), GNU `awk -F` (C), `grep '"k":"v"'` (D) | 與 `jq` 比為主要公平基準 |
| `log_parse` parse mode | `awk` | `awk` | GNU `awk` (B), `sed -E` + `awk` (C), `grep -oE` (D) | 與 `awk` 比為主要公平基準 |
| `log_parse` aggregation | `awk` | `awk` | GNU `awk match()` (B), `jq` reduce (B) | `awk` 為主，`jq reduce` 為輔 |
| `clip_store` ingest | 無 | 無 | `awk + gzip` 寫平面檔 (C), `sqlite3 INSERT` (C, 需 apt 安裝) | 採 `awk + gzip` 作組合對標 |
| `clip_store` query | 無 | 無 | `grep + cut` 從平面 DB 查 (C), `sqlite3 SELECT` (C) | 採 `grep + cut` 組合對標 |
| `stream_merge` | 無 | 無 | `awk` windowing on sidecar (C), `dd + awk` (C), `tail -f + jq` (C) | 採 `awk` windowing 組合對標 |
| `pipeline_dispatcher` | 無 | 無 | shell pipe + `wait` (C) | 比較 process 啟動 overhead 與 RSS |

---

## log_parse — Filter Mode

### CLI 對應

| 行為 | log_parse | GNU grep | Toybox grep | jq |
|---|---|---|---|---|
| 讀 stdin → 寫 stdout | ✅ | ✅ | ✅ | ✅ |
| 匹配行原樣輸出 | ✅ | ✅ | ✅ | ✅ |
| 不匹配行不輸出 | ✅ | ✅ | ✅ | ✅ |
| 診斷訊息寫 stderr | ✅ | ✅ | ✅ | ✅ |
| **JSON 結構感知** | ✅ | ❌ | ❌ | ✅ |
| **支援數值/布林比較** | ✅ | ❌ | ❌ | ✅ |
| **支援 `key!=value`** | ✅ | 需 `-v` | 需 `-v` | ✅ |
| 空輸入 exit code | 0 | 1 | 1 | 0 |

### 差異與設計理由

| 差異 | log_parse | GNU/Toybox grep | jq |
|---|---|---|---|
| **匹配模式** | JSON key=value 語意 | 任意正規表達式對整行 | JSON AST 求值 |
| **假陽性** | 無 | `{"msg":"type:clip_x"}` 會誤匹配 | 無 |
| **輸入限制** | JSON Lines | 任何文字 | JSON Lines |
| **無匹配 exit** | 0 | 1 | 0 |

### 比較對象與分類

| 對標 | 類型 | 說明 |
|---|---|---|
| **`jq -c 'select(.type=="clip")'`** | **B — 主比較對象** | 兩者都做 JSON 結構感知過濾；jq 是 DSL 直譯器，log_parse 是手寫 C 字串掃描 |
| `awk -F'"type":"' '{...}'` | C — 組合等效 | 用 awk 拆字串模擬 JSON 解析；脆弱但快 |
| GNU/Toybox `grep '"type":"clip"'` | D — 參考性對照 | byte-level 比對，會誤匹配；不能作公平基準 |

### 為何不直接用 GNU grep 做主比較

`grep '"type":"clip"'` 是 byte-level 子字串掃描，對下列輸入產生**語意錯誤**：

```jsonl
{"msg":"type:clip_note"}      ← grep 誤匹配；log_parse 正確跳過
{"type":"clip_annotated"}     ← grep 誤匹配（值不完全相等）；log_parse 正確跳過
{"meta":{"type":"clip"}}      ← grep 誤匹配巢狀；log_parse 行為依語意定義
```

直接拿 grep 與 log_parse 比吞吐量會誇大 log_parse 的優勢。
**因此主要比較對象是 `jq`，並把 `grep` 標為 D 類參考**。

---

## log_parse — Parse Mode (regex → JSON/CSV)

### CLI 對應

| 行為 | log_parse | GNU awk | Toybox awk |
|---|---|---|---|
| 讀 stdin → 寫 stdout | ✅ | ✅ | ✅ |
| 多欄位提取 | ✅ (capture groups) | ✅ (`$1,$2,…`) | ✅ |
| 內建 JSON 輸出 | ✅ (`--format json`) | ❌ 需手動 `printf` | ❌ |
| 內建 CSV 輸出 | ✅ (`--format csv`，自動跳脫) | ❌ 需手動 | ❌ |
| 行為宣告 | POSIX ERE | awk DSL | awk DSL |

### 比較對象與分類

| 對標 | 類型 | 等效指令 |
|---|---|---|
| **GNU `awk match($0, /…/, a)`** | **B — 主比較** | `awk 'match($0,/^(\S+) ([A-Z]+) (.+)$/,a){printf "{\"ts\":\"%s\",\"level\":\"%s\",\"msg\":\"%s\"}\n",a[1],a[2],a[3]}'` |
| `sed -E` + `awk` 組合 | C | 先 sed 擷取，再 awk 格式化 |
| `grep -oE` | D | 只能擷取整個匹配字串，無法多欄位 |

---

## log_parse — Aggregation Mode (`--sum`, `--avg`, `--max`, `--min`)

### CLI 對應

| 行為 | log_parse | GNU awk |
|---|---|---|
| 即時聚合（streaming） | ✅ | ✅ |
| 多 metric 同時 | ✅ (一次跑出 sum/avg) | ✅ 需手寫 |
| Memory 邊界 | O(1) | O(1) |

### 比較對象

| 對標 | 類型 | 等效指令 |
|---|---|---|
| **GNU `awk match()` + 累加** | **B — 主比較** | `awk 'match($0,/.../,a){sum+=a[1]} END{print sum}'` |
| `jq -s 'reduce ... as $x (0; . + $x.dur)'` | C | jq slurp 模式，O(n) 記憶體 |

---

## clip_store — 無直接對應，採組合對標

`clip_store` 是 file-backed KV index，**GNU coreutils 與 Toybox 都無單一工具對應**。
以下說明替代比較目標：

### 功能分解與對應

| clip_store 功能 | 替代組合 | 類型 |
|---|---|---|
| append（stdin → DB）+ 壓縮 | `awk '{print key "\t" val}' \| gzip >> db.gz` | C |
| TTL 過期管理 | 無 GNU 內建；需 `awk` + `date` 手動過濾 | C（缺少原子性） |
| `--get key` 查詢 | `zgrep "^$key\t" db.gz \| tail -1 \| cut -f2` | C |
| `--list` / `--prefix` | `zcat db.gz \| awk '{print $1}'` | C |
| `--gc` 垃圾回收 | 無；需 `sort \| awk \| mv` + `flock` | C（無原子性保證） |
| 並發寫入安全 | `flock(1)` 包裝 | C |

### 主比較對象

```bash
# 對標：awk + gzip ingest（行為近似 clip_store --ttl 0）
awk -v now=$(date +%s) '{print "key_"NR "\t" $0 "\t" (now+300)}' \
  | gzip > clips_ref.gz
```

**為何不是 sqlite3**：

- sqlite3 是 page-based B-tree，並非 streaming 寫入；對 4 KB record × 10k 寫入會
  benchmark 出截然不同的工作負載。
- BusyBox / Toybox 嵌入式環境**通常不包 sqlite3**，列入比較會偏離專案定位。
- 若期末報告需要展示 KV 儲存比較，sqlite3 可作 C 類補充對標。

**為何不是 gdbm**：

- gdbm 沒有標準的 CLI 工具（`gdbmtool` 在 Debian 是另一個套件）。
- gdbm 是 hash-on-disk，無 TTL、無壓縮、無 streaming append 語意。

### 限制聲明

`clip_store` 的功能組合（streaming append + zlib 壓縮 + Base64 編碼 + TTL + flock 並發安全 + GC）
**沒有任何單一 GNU/Toybox 工具能直接對應**。所有比較都是 C 類組合等效。
報告中應說明：「我們是用 awk+gzip 模擬最接近的行為，比較吞吐量與 DB 大小」。

---

## stream_merge — 無直接對應，採組合對標

`stream_merge` 處理 growing binary + sidecar JSONL，將時間窗對應的 byte-range 抽出
為 clip metadata。**沒有任何單一 GNU/Toybox 工具對應**。

### 功能分解

| stream_merge 功能 | 替代組合 | 類型 |
|---|---|---|
| 增量讀取 sidecar | `tail -f` 或 `inotifywait` + 偏移追蹤 | C |
| 5s 時間窗切割 | `awk` 累積 ts_ms 並重置 | C |
| Continuity break 偵測 | `awk` 比對 sequence + offset | C |
| Byte-range 抽出 | `dd if=bin bs=1 skip=$off count=$len` | C |
| sentinel 結束信號 | 監看 `.pipeline_end` 檔（自訂） | C |

### 主比較對象

對齊 benchmark 用的等效 GNU shell 組合：

```bash
# awk windowing on sidecar — emit one "clip" per 5s window
awk -F'[:,}]' '
  /"kind":"data"/ {
    for(i=1;i<=NF;i++){
      if($i=="\"ts_ms\"") ts=$(i+1);
      if($i=="\"length\"") len=$(i+1);
    }
    if(start==0) start=ts;
    if(ts-start>=5000){print "clip"; start=ts}
  }
' bench_medium.meta.jsonl
```

**注意**：此 awk 版本**只 emit windowing 切割點，未真的 `dd` 出 byte-range**。
若要真正等效，需 inner loop `dd if=… bs=1 skip=… count=…`，每段 clip fork 一次 `dd`，
吞吐量會劇烈下滑。因此 benchmark 只比較 windowing 部分，並在報告中註明這點。

### inotify 路徑 vs 預載路徑

`stream_merge` 真實運作下用 inotify 等待 sidecar 增長；benchmark 用「預先生成完整檔」模式，
避免 inotify event 排程造成測量噪音。awk 對標也採同樣模式。

---

## pipeline_dispatcher — Process Pipeline 基準

不是 applet，是「把三個 applet 串成 UNIX pipeline 的 C 程式」。
最直接的對標是 shell 自己做的 pipeline：

```bash
# Shell-only equivalent
./.build/box stream_merge S T \
  | ./.build/box log_parse --filter type=clip \
  | ./.build/box clip_store --db /tmp/clips.db --ttl 0
```

| 比較維度 | pipeline_dispatcher | Shell pipeline |
|---|---|---|
| Process fork 數 | 3 + 1 (dispatcher) | 3 + 1 (shell) |
| 信號處理 | 自訂（SIGINT/SIGTERM 統一傳播） | shell 預設 |
| Exit code 收斂 | 自訂（任一 child 失敗即整體失敗） | shell `pipefail` 開關 |
| Memory overhead | 一個 dispatcher RSS | 一個 shell RSS（通常更大） |

主要對標：**bash pipe + `wait`** 組合。屬 C 類（語意接近，但實作層不同）。

---

## 資源受限對比較選擇的影響

本專案的 benchmark 在 `systemd-run --user --scope` 包裝下執行（`MemoryMax=64M`,
`CPUQuota=50%`, `AllowedCPUs=0`），對應嵌入式 Linux 等級配置。這會直接影響
「哪些對標工具能跑完」：

| 對標工具 | 嵌入式預期表現 | 受限下行為 |
|---|---|---|
| GNU `awk` | 良好（小 binary、O(1) memory） | 通過 |
| GNU `grep` | 良好 | 通過 |
| Toybox `grep`/`awk` | 良好（為嵌入式設計） | 通過，但 grep 缺優化（實測比 GNU grep 慢 ~90×） |
| `jq` | **可能 OOM**（每行新 jv 物件，C runtime 大） | **觀察點** — Phase 7 stress test 顯示 jq slurp RSS 達 63 MB，逼近 64 MB cgroup MemoryMax，在 systemd-run 路徑下會被 OOM-kill |
| `sqlite3` | 中等（page cache 受限） | 可能不可用（套件大、不在 BusyBox 中） |
| `inotifywait` | 良好 | 通過 |

**這個受限環境就是專案最具說服力的賣點**：當 jq slurp 在 64 MB RAM 下 OOM 時，
`log_parse` 仍能維持 streaming 行為（RSS ~3.3 MB 不變）——這是「為嵌入式設計」的證明。

### Toybox 工具效能不一定優於 GNU

本次 benchmark 的非預期發現：**Toybox grep 比 GNU grep 慢 ~90×**（實測 0.28 MB/s
vs 27.23 MB/s）。Toybox 為求 binary 體積優化，未實作 Boyer-Moore 等 fast-skip
演算法。報告中應註明：「嵌入式工具集 ≠ 一定快」，挑選對標工具前必須實測，不能僅憑
「Toybox 為嵌入式設計」就假設它快。

---

## 對期末報告的論述建議

1. **不要拿 grep 跟 log_parse 直接比吞吐量**。`grep` 是 D 類參考，會誇大 log_parse 的優勢且報告會被打。
2. **主要比較對象列 jq、awk、awk+gzip 三組**，分別對應 filter、parse/aggregation、clip_store ingest 三個 phase。
3. **stream_merge 與 clip_store 應自己定位為「領域特定」**，比較目標是組合等效（C 類），不是單一工具。
4. **強調資源受限上下文**：jq slurp 在 cgroup MemoryMax=64M 下 OOM，這比「比 jq 快 7 倍」更有說服力——它證明這套工具能用，對標工具不能。
5. **Exit code 差異（log_parse exit 0 vs grep exit 1 on no-match）** 列為設計取捨，不是相容性缺陷。
6. **Toybox grep 慢 ~90×** 是有趣的副線發現，可放在「對標工具選擇方法論」段落，說明為何不能僅憑工具集名稱（Toybox 為嵌入式設計）就假設效能。

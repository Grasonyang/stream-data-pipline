# Benchmark Report — ws_pipeline_dispatcher

## 測試環境

* **OS**: Linux 6.6.114 (WSL2, Microsoft Standard) on Windows 11
* **CPU**: Intel Core i5-10300H @ 2.50 GHz，8 logical cores (4C8T)
* **RAM**: 7.7 GB（WSL2 配置）
* **Storage**: NTFS（C:\ via WSL2 DrvFs），非原生 Linux ext4；DrvFs 橋接有額外 IO overhead，吞吐量數字偏保守。
* **Ingest 假設**: 本系統目前假設 Ingress 為 WebSocket/TCP 環境，串流資料具備保序性（Ordered）。尚未將 UDP drop/reorder 亂序重組列為已支援行為。

---

## 執行方式 (Reproducibility)

本測試具備完全可重跑性 (Repo-local)，請於專案根目錄執行：

```bash
# 1. 編譯專案
make clean && make

# 2. 執行自動化 Benchmark 腳本（涵蓋 Phase 1–4）
./scripts/bench.sh
```

腳本依序執行四個 Phase：

* **Phase 1**：產生 small / medium / malformed 三種 binary dataset
* **Phase 2**：`stream_merge` 獨立吞吐量基準
* **Phase 3**：`log_parse --filter` vs `jq select()` 語意等同比較（`grep` 僅作無效參考）
* **Phase 4**：`pipeline_dispatcher` 端到端管線基準

---

## Phase 2：stream_merge 獨立基準

**輸入**：`bench_medium.bin`（40 MB，10,000 × 4 KB chunks，5 s 時間窗）

| 指標 | 數值 |
| --- | --- |
| Throughput | **130.64 MB/s** |
| Latency (Real Time) | 0.299 seconds |
| Clips Emitted | 200 |
| Max RSS | 2560 kbytes (~2.5 MB) |

### 記憶體分析（O(1) Bounded）

本系統採用 Streaming 處理模型：

* JSONL Metadata 解析：4 KB buffer
* Binary byte-range 抽取（`pread`）：8 KB buffer

即便吞吐量高達 40 MB，記憶體峰值仍穩定在 2.5 MB 以下，符合嵌入式邊緣端限制。

### 與 GNU 工具的比較限制

`stream_merge` 在 GNU coreutils 與 Toybox 中**無直接對應工具**。最接近的 GNU shell 等效組合為：

```bash
inotifywait -m -e modify "$src_dir/$session.meta.jsonl" \
  | while read ...; do tail -n +$last "$src_dir/$session.meta.jsonl"; done \
  | grep '"type":"clip"' \
  | awk '{...}' >> clips.db
```

但此組合有結構性差異：`dd` 每段 clip 需獨立 fork process，而 `stream_merge` 使用 `pread()` 在同一 process 完成所有 byte-range 抽取；且 5s 時間窗切割、sequence/offset 連續性驗證、inotify sentinel 偵測均無 coreutils 直接對應。直接比較 throughput 無意義，故不作數字比較。

---

## Phase 3：log_parse --filter vs jq 語意等同比較

### 為何 grep 不是公平基準

`grep '"type":"clip"'` 執行的是 **byte-level 子字串掃描**，不解析 JSON 結構。它會對以下情境產生假陽性：

```jsonl
{"msg": "type:clip_note"}   ← grep 誤匹配；log_parse 不匹配
{"type": "clip_annotated"}  ← grep 誤匹配；log_parse 不匹配（值不完全相等）
```

因此 grep 與 log_parse 的**語意目標根本不同**，直接比較吞吐量對 log_parse 不公平。語意完全等同的比較對象是 `jq 'select(.type == "clip")'`（key/value 精確比對，JSON 結構感知）。

> **注意**：`jq` 並非 GNU coreutils 或 util-linux 原版工具，故本比較**不符合作業「與 GNU coreutils 對比」的形式要求**。然而在 JSON 過濾這個問題域中，`jq` 是唯一語意等同的工具；`grep` 雖是 GNU coreutils，但它解決的是不同問題（通用文字掃描）。

### 比較條件

| 條件 | 說明 |
| --- | --- |
| 輸入資料 | 50,000 筆緊湊 JSON Lines（`separators=(',',':')`），4.34 MB |
| 過濾目標 | `type=clip`（字串欄位值） |
| 匹配記錄數 | 10,000 筆（佔 20%） |
| 輸出方式 | 三者皆寫入檔案（排除 stdout buffer 差異） |

### 結果

| 工具 | 指令 | Throughput | 匹配行數 | 備註 |
| --- | --- | --- | --- | --- |
| `log_parse` | `./build/log_parse --filter type=clip` | **25.03 MB/s** | 10,000 | 語意正確 |
| `jq` 1.6 | `jq -c 'select(.type == "clip")'` | **14.98 MB/s** | 10,000 | 語意等同（主要比較對象） |
| `grep` (GNU 3.7) | `grep '"type":"clip"'` | **67.50 MB/s** | 10,000 | **僅供參考，語意不等同** |
| **比率（主）** | log_parse / jq | **167.1%** | — | log_parse 顯著優於 jq |
| 比率（參考） | log_parse / grep | 37.1% | — | 不公平比較，列出供說明 |

### 效能分析

**log_parse vs jq（語意等同，公平比較）：**

| 維度 | log_parse --filter | jq select() |
| --- | --- | --- |
| 解析方式 | 手寫 POSIX C JSON 欄位掃描器 | 完整 jq DSL 直譯器（含 compile + GC） |
| 假陽性防範 | 是（精確 key=value 比對） | 是（AST 語意求值） |
| 記憶體配置 | 固定 stack buffer，零 heap | 每行動態配置 jv 物件 |
| Startup overhead | 微秒級 | 毫秒級（DSL compile） |

`log_parse` 吞吐量（25.03 MB/s）為 `jq`（14.98 MB/s）的 **167.1%**，**顯著超越語意等同的比較基準**，達成「嵌入式資源受限環境」的設計目標。差距來源：`log_parse` 使用固定 stack buffer 的手寫 C 掃描器；`jq` 需要每行動態配置 jv 物件並執行 DSL compile。

**log_parse vs grep（語意不等同，僅供說明）：**

`log_parse` 吞吐量為 `grep` 的 **37.1%**。差距源自 JSON parse overhead vs byte-level SIMD scan，是 **accuracy-throughput tradeoff** 而非可消除的優化空間。若犧牲語意正確性退化為 substring scan，則 log_parse 失去存在意義。此比較**不具備設計層面的意義**，列出僅供完整性參考。

---

## Phase 4：End-to-End Pipeline 基準

**架構**：`pipeline_dispatcher` 串接三個 applet：

```text
stream_merge bench_medium test_env
    | log_parse --filter type=clip
    | clip_store --db clips_e2e.db --ttl 0
```

**輸入**：`bench_medium.bin`（39.06 MB，10,000 × 4 KB chunks）

| 指標 | 數值 |
| --- | --- |
| End-to-End Latency | **0.264 seconds** |
| End-to-End Throughput | **147.95 MB/s** |
| Clips stored in DB | 200 |
| Max RSS (pipeline_dispatcher) | 2432 kbytes (~2.4 MB) |

### 說明

* 端到端吞吐量（147.95 MB/s）高於 stream_merge 獨立基準（130.64 MB/s），原因是 pipeline 各 stage 透過 pipe 並發執行，隱藏了 log_parse 與 clip_store 的處理時間。
* 200 個 clips 全數寫入 KV 資料庫，可透過 `./build/clip_store --db test_env/clips_e2e.db --get bench_medium:<ts>` 查詢。
* 記憶體峰值仍維持在 2.5 MB 以下，三個 child processes 均採用 streaming 模型。

### clip_store 與 GNU 工具的比較限制

`clip_store` 在 GNU coreutils 與 Toybox 中**無直接對應工具**：

* GNU `dbm`/`gdbm` 提供 KV 儲存但不接受 stdin JSON Lines 輸入
* `tee`/`sponge` 只處理流，無 TTL 或鍵值語意
* 嵌入式環境（BusyBox / Toybox）無對應的 KV store applet

最接近的 shell 等效方式需要 `awk` + `date` + `flock` 手動組合，且缺乏原子性保證。詳見 `.docs/core/compatibility.md`。

---

## 對期末報告的說明建議

1. **stream_merge**：領域特定工具，無 GNU 直接對應，效能基準以自身吞吐量（130.64 MB/s）和記憶體足跡（O(1) bounded）為核心指標。
2. **log_parse**：語意等同的比較對象是 `jq select()`（非 GNU coreutils），log_parse 吞吐量為 jq 的 **167.1%**，顯著超越語意基準。`grep` 不做 JSON 解析，語意不等同，不應作為主要比較對象（37.1%，僅列出供說明）。
3. **clip_store**：領域特定工具，無 GNU 直接對應，比較基準為 pipeline 組合而非單一 applet。
4. **End-to-End**：完整管線（Phase 4）展示三個 applet 透過 UNIX pipe 組合的實際效能，符合「三個工具可透過 UNIX pipe 組合」的設計要求。

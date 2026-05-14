# `log-parse` — 細部開發文件 v1.0

> **對應檔案**：`applets/log_parse.c`
> **版本**：v1.0 / 2026-05-11
> **定位**：`log-parse` 是管線中的**過濾與轉換層**。從 stdin 讀取 `stream-merge` 輸出的 JSON Lines，依條件過濾、提取指定欄位、轉換輸出格式，再寫入 stdout。遵循 UNIX filter 設計原則：無狀態、可組合、stdin→stdout。

***

## 一、職責總覽

`log-parse` 的職責嚴格限定在**三個轉換動作**，不做任何資料儲存或副作用：

| 動作 | 說明 | 對應 CLI |
|---|---|---|
| **Filter**（過濾） | 只讓符合條件的 JSON Lines 通過 | `--filter key=value` |
| **Project**（欄位提取） | 只輸出指定的欄位子集 | `--fields ts,path,events` |
| **Transform**（格式轉換） | 輸出 JSON、CSV 或純計數 | `--format json\|csv\|count` |

這三個動作**依序套用**：先 Filter，再 Project，再 Transform。每條輸入行各自獨立處理，`log-parse` 不跨行累積任何狀態。

***

## 二、程式進入點與初始化流程

```
main(argc, argv)
    │
    ├── [^1] parse_args()         — 解析 CLI 參數，填入 ParseConfig struct
    ├── [^2] validate_config()    — 驗證 filter 語法是否合法
    ├── compile_filters()    — 將 --filter 字串預先編譯為 FilterRule 陣列
    │
    └── run_parse_loop()     — 進入主讀取循環（阻塞直到 stdin EOF）
```

`compile_filters()` 在啟動時一次性解析所有 filter 表達式，主循環中只做比對，不做字串解析，確保 hot path 效能 。[^1]

***

## 三、核心資料結構

### 3.1 ParseConfig（CLI 參數對應）

```c
typedef struct {
    FilterRule  filters[MAX_FILTERS];   /* --filter 規則陣列 */
    int         filter_count;
    char        fields[MAX_FIELDS]; /* --fields 提取的欄位名稱 */
    int         field_count;            /* 0 表示輸出全部欄位 */
    OutputFormat format;                /* JSON | CSV | COUNT */
    int         csv_header_printed;    /* CSV 模式下是否已輸出 header 行 */
} ParseConfig;
```

### 3.2 FilterRule（過濾規則的編譯結果）

```c
typedef enum {
    OP_EQ,      /* key=value    精確匹配 */
    OP_NEQ,     /* key!=value   排除 */
    OP_GT,      /* key>value    數值大於 */
    OP_LT,      /* key<value    數值小於 */
    OP_CONTAINS /* key~value    字串包含 */
} FilterOp;

typedef struct {
    char      key;      /* 要比對的 JSON key（支援一層，不支援巢狀） */
    FilterOp  op;           /* 比對運算子 */
    char      value_str; /* 原始字串值 */
    double    value_num;    /* 若為數值比較，預先 parse 好的 double */
    int       is_numeric;   /* 1 = 數值比較，0 = 字串比較 */
} FilterRule;
```

### 3.3 OutputFormat

```c
typedef enum {
    FMT_JSON  = 0,   /* 預設：輸出完整或 projected JSON Lines */
    FMT_CSV   = 1,   /* 第一行為 header，後續每行為對應值 */
    FMT_COUNT = 2    /* 只輸出通過 filter 的行數（單一整數） */
} OutputFormat;
```

***

## 四、主讀取循環（`run_parse_loop`）

```
run_parse_loop(config)
    │
    └── while (fgets(line, MAX_LINE, stdin) != NULL)
            │
            ├── [A] json_parse_line(line)
            │       ├── 解析為 key-value 對的輕量結構（JsonObj）
            │       └── 解析失敗 → log_warn + continue（跳過此行）
            │
            ├── [B] apply_filters(json_obj, config->filters)
            │       ├── for each FilterRule:
            │       │       └── eval_filter(json_obj, rule) → bool
            │       └── 任一 rule 不符合 → continue（此行被過濾掉）
            │
            ├── [C] apply_projection(json_obj, config->fields)
            │       └── field_count > 0 → 建立只含指定欄位的新 JsonObj
            │           field_count == 0 → 原封不動傳遞
            │
            └── [D] emit_output(projected_obj, config)
                    ├── FMT_JSON  → serialize_json(obj) + puts()
                    ├── FMT_CSV   → emit_csv_row(obj, config)
                    └── FMT_COUNT → count++（loop 結束後才 printf("%d\n", count)）
```

**行大小上限**：`MAX_LINE = 65536`（64 KB），足以容納含大量 events 陣列的 clip JSON。超過此上限的行視為格式錯誤，輸出 `LOG_WARN` 後跳過 。[^1]

***

## 五、Filter 評估邏輯（`eval_filter`）

```
eval_filter(json_obj, rule)
    │
    ├── value = json_get(json_obj, rule->key)
    │       └── key 不存在 → 依 op 決定：
    │               EQ / GT / LT / CONTAINS → 視為不符合（return false）
    │               NEQ → 視為符合（key 不存在等同值不等於任何東西）
    │
    └── 依 rule->op 執行比對：
            OP_EQ       → strcmp(value, rule->value_str) == 0
            OP_NEQ      → strcmp(value, rule->value_str) != 0
            OP_GT       → atof(value) > rule->value_num
            OP_LT       → atof(value) < rule->value_num
            OP_CONTAINS → strstr(value, rule->value_str) != NULL
```

多個 `--filter` 規則之間為 **AND 語意**：所有規則必須同時符合，該行才通過。如需 OR 語意，使用者應透過多次 pipe 組合（Unix 慣例）。[^1]

***

## 六、Filter 語法完整規格

| 語法 | 範例 | 說明 |
|---|---|---|
| `key=value` | `--filter type=clip` | 精確匹配字串 |
| `key!=value` | `--filter status!=partial` | 排除特定值 |
| `key>value` | `--filter ts>1747065600000` | 數值大於（自動偵測數值型別） |
| `key<value` | `--filter duration_ms<3000` | 數值小於 |
| `key~value` | `--filter session_id~sess_` | 字串包含（substring match） |

**型別自動偵測**：若 `value` 能被 `strtod()` 完整解析，則視為數值比較，否則為字串比較。這讓 `ts>1747065600000` 和 `type=clip` 都能正確運作，無需使用者宣告型別 。[^1]

***

## 七、CSV 輸出模式

CSV 模式下，第一條通過 filter 的行會觸發 header 輸出，之後每行輸出對應值。

```
輸入（JSON Lines）：
{"ts":1000,"session_id":"s1","type":"clip","path":"/tmp/x.mp4","status":"complete"}
{"ts":1005,"session_id":"s1","type":"clip","path":"/tmp/y.mp4","status":"partial"}

指令：
log-parse --filter type=clip --fields ts,session_id,status --format csv

輸出：
ts,session_id,status
1000,s1,complete
1005,s1,partial
```

**CSV 逸出規則**：
- 值中含逗號或換行 → 以雙引號包圍
- 值中含雙引號 → 以 `""` 跳脫
- 值為 JSON 陣列或物件（如 `events`）→ 序列化為 JSON 字串後再做 CSV 逸出

***

## 八、COUNT 輸出模式

`--format count` 不輸出任何資料行，只在 stdin EOF 後輸出一個整數：

```bash
# 計算某 session 有多少個 complete clip
cat stream.log | log-parse --filter type=clip --filter status=complete --format count
# 輸出：248
```

適合在 shell script 中做條件判斷或 benchmark 統計 。[^1]

***

## 九、`--fields` 的 events 陣列處理

`events` 是 JSON 陣列欄位，在不同輸出格式下的行為：

| 輸出格式 | `--fields` 包含 `events` 時的行為 |
|---|---|
| JSON | 保留完整 JSON 陣列，原樣輸出 |
| CSV | 將整個陣列序列化為 JSON 字串（`"[{...},{...}]"`），置於單一 CSV 欄位 |
| COUNT | events 欄位不影響計數邏輯 |

***

## 十、與 `stream_logger` 的整合

`log-parse` 使用 `stream_logger` 模組輸出自身的診斷訊息，初始化設定：

```c
LoggerConfig lcfg = {
    .log_file   = NULL,         /* log-parse 的診斷訊息輸出到 stderr，不干擾 stdout 管線 */
    .min_level  = LOG_WARN,     /* 只在有問題時才輸出，不污染 stderr */
    .format     = LOG_FMT_TEXT,
    .session_id = NULL
};
logger_init(&lcfg);
```

**關鍵約定**：`log-parse` 的診斷訊息**只走 stderr**，stdout 只輸出過濾後的資料，確保 pipe 組合的正確性 。[^1]

***

## 十一、CLI 完整規格

```bash
log-parse [OPTIONS]

讀取來源：stdin（固定，符合 UNIX filter 慣例）
輸出目標：stdout

過濾選項（可重複使用，多個 filter 為 AND 語意）：
  --filter <expr>        過濾表達式（見第六節語法規格）

欄位選項：
  --fields <f1,f2,...>   只輸出指定欄位（逗號分隔，省略則輸出全部欄位）

輸出格式：
  --format json          JSON Lines 輸出（預設）
  --format csv           CSV 輸出（首行為 header）
  --format count         只輸出符合條件的行數

退出碼：
  0   正常結束（stdin EOF）
  1   參數錯誤（filter 語法非法、未知 --format 值）
  2   stdin 讀取錯誤
```

***

## 十二、效能考量與 benchmark 基準

`log-parse` 的對標工具為 `grep + jq` 組合：

```bash
# log-parse 的等效 shell 操作
cat data.jsonl | grep '"type":"clip"' | jq -c '{ts,session_id,path}'

# 預期 log-parse 勝出的原因：
# 1. 不啟動兩個 process（grep + jq），只有一個
# 2. 不使用 regex 引擎（grep），而是直接 JSON key lookup
# 3. JSON 解析使用固定 stack buffer，無 heap overhead
```

Benchmark 目標：在 100MB JSON Lines 輸入下，處理速度應在 `grep | jq` 的 **50% 以內差距**，即不超過兩倍慢 [^1]。

***

## 十三、測試策略

| 測試情境 | 輸入 | 驗證目標 |
|---|---|---|
| 基本 filter 通過 | `{"type":"clip","ts":1000}` + `--filter type=clip` | stdout 有輸出 |
| filter 過濾排除 | `{"type":"metric","ts":1000}` + `--filter type=clip` | stdout 無輸出 |
| 數值比較 | `--filter ts>999` | ts=1000 通過，ts=998 被過濾 |
| 多重 AND filter | `--filter type=clip --filter status=complete` | partial 被過濾掉 |
| `--fields` 欄位提取 | `--fields ts,path` | 輸出只含 ts 和 path |
| CSV 格式 + header | `--format csv --fields ts,status` | 第一行為 `ts,status` |
| COUNT 模式 | 10 行輸入，5 行通過 | stdout 輸出 `5` |
| 損壞 JSON 行 | 非法 JSON 行混入 | LOG_WARN 到 stderr，其餘行正常處理 |
| events 陣列 CSV | `--fields ts,events --format csv` | events 欄位為合法 JSON 字串 |

***

## 十四、檔案對應

```
socket-data-analyzer-c/
├── applets/
│   └── log_parse.c        ← 本文件描述的主體
├── lib/
│   ├── stream_logger.h    ← 診斷訊息依賴
│   └── libpipeline.h      ← pipeline_compress_json 等共用工具
└── tests/
    └── test_log_parse.sh
```

***

*本文件對應 `socket-data-analyzer-c` repo，`log_parse.c` 模組。*
*v1.0 — 2026-05-11*

---

## References

1. [data_analyzer_archi.md](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/143763799/57352e78-c91d-4b6e-9a40-b61d6850d444/data_analyzer_archi.md?AWSAccessKeyId=ASIA2F3EMEYEURBJYAAU&Signature=berJkMCAPQEkJGI%2BqamfDYpmP7o%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEFAaCXVzLWVhc3QtMSJHMEUCIQCX%2Fk%2FJHEsrqzuC1%2B7OaZbm0nHy73I%2F1FKy%2FkLRBKu0KAIgJcO6bFmscqYTB5UBtrrNVixVeUyErH7rsctMwwg%2Fqfgq8wQIGRABGgw2OTk3NTMzMDk3MDUiDJdbYGGR3p1RaNboHirQBIfitXemlgPTPJgNRyiMwcTcf%2BmbXEPSWHMntOXGEa6AjT6aaK3wPr0cQqfx2rVnp9OrEcFguwzTxaNZ8NWiCWeFwGNzle92piNb9bAYt4j7TaYMPR521Z5aWf%2FpE9bFzB0Q5LEeHv1mI8LMHWsXE50ONC%2BxJfVkV9AXerTgJmDVYhknnmZuFZnBy0V8sHmQLcypJJrA5etz61ShzB2rcZ4LzdnamqxgrNkVZQZe7sj2hzpTfuABWOjp%2BnbBlo4BOUvsEARCYLOAsAY7HYbih8up%2BmQaJLJd84arfxpMCiyH%2BjTJUrJNGHJj53wuaZRyUecGO7bgN1Slhjzt%2Bbc1OsldqhRTvsnYj5CTjQJGxZV0esVY1mfLYwbt0Nu6cBbKGniz7%2BG%2BNlnQv165Sc78GEt9fvlvBVUyM2vNWJt%2BuL1jblQIf7dfN5vNwwR4uuDgeWTCjiUzGka%2BSELA27SX96g8eUvokbf6JfaDLKWjuhF1iYpvody1z%2FI8oUtTzwvA2nUkhnQ0vgVOFQDYE9HcuKERzDNo1%2B80S%2FT%2FSvp%2Bq4gybT%2FMGJmBPUX%2Fhuyqo%2B2bXEOz1ASpHfJV%2F3Hii7xvtRoK2Sr61CD43pta9L11alaHv4U%2FVdmpYHAXQ2dx5O9d2ta6c0KqKzCg3TBN%2Fzsi5jk0UvS79U8FFDt1av%2Fd7Bp6mI1cuDjxAzWd09STXZlzNZlNgyLQBBMYjYAzwPWQQMdOkacDs7E5%2F%2Br%2BkwqAJENjUySGXrvvUkHzO6s4B4prIwUjMc56AHUC%2BD3kMrMSn04wnuiH0AY6mAFczubh6i9GVDMxj45K1feM3bVFn%2BmyTHx1232q0gasp%2B%2FI0AQGgDoch0nGzpc8QME3uc1z5YEytJCuCQkyIYxsrPz0j8BNRzSqGvNG0pKhkppkJwvZaqekflqGNDQrfjFGgPMGWys8FWBkqDr6m2bbsF7HKm4%2Fs8CxSuqRuL8%2FhdR5D0B2EwdVgCLeuUgLaOgK1GEiBoEXng%3D%3D&Expires=1778516465) - # Data Analyzer — 獨立開發架構說明

> **版本**：v2.0
> **日期**：2026-04-29
> **定位**：Data Analyzer 是可獨立開發、測試的實時串流處...

2. [project_assignment.pdf](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/collection_f130a32f-3bc7-49c8-9eee-1fc964be7bbf/ee34f601-d9e2-467f-8a18-8a5f7e5febfa/project_assignment.pdf?AWSAccessKeyId=ASIA2F3EMEYEURBJYAAU&Signature=S5HZ%2FdU1AbO28AksSsYzGL6bm3s%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEFAaCXVzLWVhc3QtMSJHMEUCIQCX%2Fk%2FJHEsrqzuC1%2B7OaZbm0nHy73I%2F1FKy%2FkLRBKu0KAIgJcO6bFmscqYTB5UBtrrNVixVeUyErH7rsctMwwg%2Fqfgq8wQIGRABGgw2OTk3NTMzMDk3MDUiDJdbYGGR3p1RaNboHirQBIfitXemlgPTPJgNRyiMwcTcf%2BmbXEPSWHMntOXGEa6AjT6aaK3wPr0cQqfx2rVnp9OrEcFguwzTxaNZ8NWiCWeFwGNzle92piNb9bAYt4j7TaYMPR521Z5aWf%2FpE9bFzB0Q5LEeHv1mI8LMHWsXE50ONC%2BxJfVkV9AXerTgJmDVYhknnmZuFZnBy0V8sHmQLcypJJrA5etz61ShzB2rcZ4LzdnamqxgrNkVZQZe7sj2hzpTfuABWOjp%2BnbBlo4BOUvsEARCYLOAsAY7HYbih8up%2BmQaJLJd84arfxpMCiyH%2BjTJUrJNGHJj53wuaZRyUecGO7bgN1Slhjzt%2Bbc1OsldqhRTvsnYj5CTjQJGxZV0esVY1mfLYwbt0Nu6cBbKGniz7%2BG%2BNlnQv165Sc78GEt9fvlvBVUyM2vNWJt%2BuL1jblQIf7dfN5vNwwR4uuDgeWTCjiUzGka%2BSELA27SX96g8eUvokbf6JfaDLKWjuhF1iYpvody1z%2FI8oUtTzwvA2nUkhnQ0vgVOFQDYE9HcuKERzDNo1%2B80S%2FT%2FSvp%2Bq4gybT%2FMGJmBPUX%2Fhuyqo%2B2bXEOz1ASpHfJV%2F3Hii7xvtRoK2Sr61CD43pta9L11alaHv4U%2FVdmpYHAXQ2dx5O9d2ta6c0KqKzCg3TBN%2Fzsi5jk0UvS79U8FFDt1av%2Fd7Bp6mI1cuDjxAzWd09STXZlzNZlNgyLQBBMYjYAzwPWQQMdOkacDs7E5%2F%2Br%2BkwqAJENjUySGXrvvUkHzO6s4B4prIwUjMc56AHUC%2BD3kMrMSn04wnuiH0AY6mAFczubh6i9GVDMxj45K1feM3bVFn%2BmyTHx1232q0gasp%2B%2FI0AQGgDoch0nGzpc8QME3uc1z5YEytJCuCQkyIYxsrPz0j8BNRzSqGvNG0pKhkppkJwvZaqekflqGNDQrfjFGgPMGWys8FWBkqDr6m2bbsF7HKm4%2Fs8CxSuqRuL8%2FhdR5D0B2EwdVgCLeuUgLaOgK1GEiBoEXng%3D%3D&Expires=1778516465) - UNIX 系統程式設計 
期末專題說明文件 
 
學期：114 學年度第二學期 
專題佔總成績：30% 
課程代碼：54015

目錄 
一、專題目的與學習目標 
二、專題選項說明 
    選項 A...


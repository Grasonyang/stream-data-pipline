# `stream_logger` — 細部開發文件 v1.0

> **對應檔案**：`lib/stream_logger.c` / `lib/stream_logger.h`
> **版本**：v1.0 / 2026-05-11
> **定位**：`stream_logger` 是整個 C 子系統（`stream-merge`、`log-parse`、`clip-store`、`pipeline_dispatcher`）共用的日誌模組。以輕量、零依賴、thread-safe 為設計目標，提供結構化日誌輸出、log rotation、以及可選的 JSON 格式輸出供機器解析。

***

## 一、設計目標與限制

| 目標 | 說明 |
|---|---|
| **零依賴** | 只使用 POSIX 標準 API（`write`、`open`、`flock`、`time`），不依賴 syslog 或第三方函式庫 |
| **非阻塞** | 日誌寫入不得阻塞主事件循環，使用 `O_NONBLOCK` + internal ring buffer |
| **Thread-safe** | 使用 `flock()` 保護檔案寫入，使用 atomic 操作保護 in-memory counter |
| **可機器解析** | 支援 `--log-format json` 輸出，讓外部工具直接 `grep` 或 `jq` 處理 |
| **嵌入式友善** | 不使用動態記憶體分配（`malloc`）在 hot path 上，log entry 用固定大小 stack buffer |

***

## 二、公開介面（`stream_logger.h`）

```c
#ifndef STREAM_LOGGER_H
#define STREAM_LOGGER_H

#include <stdarg.h>

/* Log 等級定義 */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4   /* LOG_FATAL 寫入後呼叫 abort() */
} LogLevel;

/* 輸出格式 */
typedef enum {
    LOG_FMT_TEXT = 0,   /* [LEVEL] ISO8601 message  （預設） */
    LOG_FMT_JSON = 1    /* {"level":"INFO","ts":...,"msg":"..."} */
} LogFormat;

/* Logger 初始化設定 */
typedef struct {
    const char *log_file;       /* NULL 表示輸出到 stderr */
    LogLevel    min_level;      /* 低於此 level 的訊息被靜默 */
    LogFormat   format;         /* TEXT 或 JSON */
    size_t      max_file_size;  /* bytes，超過觸發 rotation（0 表示不 rotate） */
    int         max_rotations;  /* 保留幾個舊檔（rotation 上限，預設 3） */
    const char *session_id;     /* 若非 NULL，每條 log 附加 session_id 欄位 */
} LoggerConfig;

/* 初始化 / 關閉 */
int  logger_init(const LoggerConfig *cfg);
void logger_close(void);

/* 核心寫入 API（使用 printf-style format） */
void logger_write(LogLevel level, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* 便利巨集（自動帶入 __FILE__ 和 __LINE__） */
#define LOG_DEBUG(fmt, ...) logger_write(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  logger_write(LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  logger_write(LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) logger_write(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) logger_write(LOG_FATAL, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/* 手動觸發 log rotation */
int logger_rotate(void);

/* 取得目前 log 檔案大小（bytes） */
size_t logger_current_size(void);

#endif /* STREAM_LOGGER_H */
```

***

## 三、內部架構

```
logger_write(level, file, line, fmt, ...)
    │
    ├── [^1] level_filter()
    │       └── level < cfg.min_level → return（靜默）
    │
    ├── [^2] format_entry()              — 純 stack 操作，無 malloc
    │       ├── get_iso8601_timestamp() → char ts
    │       ├── vsnprintf(msg_buf, MAX_MSG, fmt, args)
    │       └── TEXT 模式：snprintf(entry, "[LEVEL] ts [file:line] msg\n")
    │           JSON 模式：snprintf(entry, "{\"level\":\"...\",\"ts\":...,\"msg\":\"...\",\"src\":\"file:line\"}\n")
    │
    ├── write_entry()
    │       ├── 目標為 stderr  → write(STDERR_FILENO, entry, len)
    │       └── 目標為檔案     → flock(fd, LOCK_EX)
    │                               write(fd, entry, len)
    │                               flock(fd, LOCK_UN)
    │
    └── rotation_check()           — 僅在 max_file_size > 0 時執行
            └── logger_current_size() >= cfg.max_file_size
                    └── logger_rotate()
```

**Hot path 原則**：步驟 1-3 全部在 stack 上操作，唯一的系統呼叫是 `write()` 和 `flock()`，不觸發 heap allocation 。[^1]

***

## 四、Log Entry 格式

### TEXT 格式（預設，人類可讀）

```
[INFO]  2026-05-11T23:00:00.123 [stream_merge.c:247] Stream session started: sess_001
[DEBUG] 2026-05-11T23:00:01.001 [stream_merge.c:312] chunk_0000 received (1000 bytes, seq=0)
[WARN]  2026-05-11T23:00:20.500 [stream_merge.c:389] Gap detected at seq=10, last_seq=9, gap_duration=5.2s
[ERROR] 2026-05-11T23:00:25.200 [stream_merge.c:401] CRC mismatch chunk seq=15, expected=0xABCD, got=0x1234
[FATAL] 2026-05-11T23:00:30.000 [stream_merge.c:150] inotify_add_watch failed: No such file or directory
```

### JSON 格式（`--log-format json`，機器可讀）

```json
{"level":"INFO","ts":1747065600123,"iso":"2026-05-11T23:00:00.123","session":"sess_001","src":"stream_merge.c:247","msg":"Stream session started: sess_001"}
{"level":"WARN","ts":1747065620500,"iso":"2026-05-11T23:00:20.500","session":"sess_001","src":"stream_merge.c:389","msg":"Gap detected at seq=10, last_seq=9, gap_duration=5.2s"}
```

JSON 格式的 `ts` 欄位為 Unix timestamp（毫秒），方便 `log-parse` 做數值範圍過濾（`ts>1747065600000`）。[^1]

***

## 五、Log Rotation 機制

當 log 檔案超過 `max_file_size` 時，`logger_rotate()` 執行以下步驟：

```
logger_rotate()
    │
    ├── close(current_fd)
    │
    ├── 搬移舊檔（從大到小依序 rename，避免覆蓋）
    │       stream.log.2 → stream.log.3   （若已有 .3 則刪除）
    │       stream.log.1 → stream.log.2
    │       stream.log   → stream.log.1
    │
    ├── open("stream.log", O_CREAT|O_WRONLY|O_TRUNC) → new_fd
    │
    └── 更新 internal fd，繼續寫入
```

**Rotation 檔案命名規則**：

| 檔案 | 內容 |
|---|---|
| `stream.log` | 當前最新 log |
| `stream.log.1` | 上一個 rotation |
| `stream.log.2` | 上上個 rotation |
| `stream.log.3` | 最舊（`max_rotations=3` 時的上限） |

***

## 六、各呼叫者的使用規範

### stream-merge 的初始化

```c
LoggerConfig lcfg = {
    .log_file      = config->log_file,   /* 來自 --log-file CLI 參數 */
    .min_level     = LOG_DEBUG,
    .format        = LOG_FMT_TEXT,
    .max_file_size = 10 * 1024 * 1024,   /* 10 MB 觸發 rotation */
    .max_rotations = 3,
    .session_id    = config->session_id
};
logger_init(&lcfg);
```

### 各模組的 log 使用約定

| 情境 | 使用的 level | 範例訊息 |
|---|---|---|
| 程式啟動 / 結束 | `LOG_INFO` | `"Stream session started: %s"` |
| 每個 chunk 到達 | `LOG_DEBUG` | `"chunk_%04d received (%d bytes)"` |
| clip 成功輸出 | `LOG_INFO` | `"clip_%d written to %s"` |
| gap 偵測 | `LOG_WARN` | `"Gap at seq=%d, duration=%.1fs"` |
| CRC 失敗 | `LOG_WARN` | `"CRC mismatch seq=%d"` |
| idle timeout | `LOG_WARN` | `"Idle timeout, flushing partial"` |
| I/O 系統呼叫失敗 | `LOG_ERROR` | `"write() failed: %s"` |
| 無法繼續執行的錯誤 | `LOG_FATAL` | `"inotify_add_watch failed: %s"` |

**約定**：`LOG_DEBUG` 訊息在 production 環境下透過 `min_level=LOG_INFO` 靜默，**無需修改程式碼**，只需更改 `LoggerConfig` 。[^1]

***

## 七、與 `log-parse` 的整合

`stream_logger` 的 JSON 格式輸出直接對應 `log-parse` 的過濾語法：

```bash
# 從 JSON 格式 log 中過濾所有 WARN 以上的訊息
cat stream.log | log-parse --filter level!=DEBUG --filter level!=INFO --format json

# 查詢特定時間段的 gap 事件
cat stream.log | log-parse --filter "msg~Gap detected" --filter "ts>1747065600000"
```

這讓 `stream_logger` 的輸出不僅是人類 debug 的工具，也是 `log-parse` 的**輸入資料來源**之一，形成閉環 。[^1]

***

## 八、錯誤處理規範

| 錯誤情境 | 處理方式 |
|---|---|
| `logger_init()` 時檔案無法開啟 | fallback 到 stderr，回傳 -1 但不中止程式 |
| `write()` 部分寫入（short write） | 重試剩餘 bytes，超過 3 次放棄並計入 `write_error_count` |
| rotation 期間 `rename()` 失敗 | 保留原檔繼續寫入，log 一條 `LOG_ERROR`，不中止 |
| `LOG_FATAL` 呼叫 | 強制 flush 所有待寫 buffer → `logger_close()` → `abort()` |

***

## 九、測試策略

| 測試情境 | 驗證目標 |
|---|---|
| 寫入 100 條不同 level 的 log | 檔案內容格式正確，level filter 有效 |
| 超過 `max_file_size` | rotation 正確產生 `.1`、`.2`、`.3` 檔 |
| 同時兩個 process 寫同一 log 檔 | `flock()` 確保無交錯（interleaved）行為 |
| JSON 格式輸出 | 每行均為合法 JSON，可被 `jq` 解析 |
| `log-parse` 過濾 JSON log | `level` 與 `ts` 欄位可正確被 filter 語法匹配 |

***

## 十、檔案對應

```
socket-data-analyzer-c/
├── lib/
│   ├── stream_logger.h    ← 本文件描述的公開介面
│   └── stream_logger.c    ← 實作
└── tests/
    └── test_stream_logger.sh
```

> **與 `libpipeline` 的關係**：`stream_logger` 作為獨立模組存放於 `lib/`，`libpipeline.c` 在需要輸出診斷訊息時透過 `#include "stream_logger.h"` 引入，兩者不互相循環依賴。

***

*本文件對應 `socket-data-analyzer-c` repo，`stream_logger` 模組。*
*v1.0 — 2026-05-11*

---

## References

1. [data_analyzer_archi.md](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/attachments/143763799/57352e78-c91d-4b6e-9a40-b61d6850d444/data_analyzer_archi.md?AWSAccessKeyId=ASIA2F3EMEYETY6V6S3T&Signature=w3G6IEAUnD3H%2BOZUxUcSFRrw2%2F4%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEE8aCXVzLWVhc3QtMSJHMEUCIQDMR8F0RJNgDZbn7DBO1uMfu%2FhBf3qp5yUc6NttyzSl7AIgN5j%2BaHgPKMPpRZjda1%2Fd4giAWeO9P9o7JfvxWTC9RkUq8wQIGBABGgw2OTk3NTMzMDk3MDUiDH%2BpxiyeP5KkK7IekSrQBJm8yuH4W2mq2GxXZQY9TyYN0iNGIGv1p%2FGfpHJ9RbKuSa5BaF9oeENFWyzFSJtRjznB17DzFtIkrc7S02b52w5IkWlcOxXZJAO3Ve4raH76GcU5f1nt40w2IQecmCsx0P44k8BLyyvYPRdxW2Qv1MsiiiUQaE4fd7RSWhaA%2FuSuMhgcfI89Y8NE%2BS5AF2vu90DOl2ff%2B%2Bo4io75bFUHFA7N9ylC18XDUMLZo7aIT%2FLgvDt8GkLdpMgsVqjDZlYAKCfd09Eg%2BRciT9fnBZxKwp3UtizvgKVeG6spkLFLErzoRMhfwdBBbsDW4QjUIvcd89%2BXqZJPmxAiCl6d7KDCTGXxecLe5Ln9Vhv2uBYnhrizNs47uc8ESZcTYTBIHmZK%2BtPp3IX9MWbyTIGRtIjYXs4jaR%2Ff47PromrmRVMQYZ%2FeB6BT%2Be3sqs6ut%2F1uwwsunJg3ctcBaAslcFxYwtTO2Fq1IloMn7zwLJHpm7hDRX2kBDvP5xoPyGGPnq9AWV9rdbxOimfEfETFE8jatUJeUxujvGvzXAklX9le72TSdA%2FAURm7Q%2F334pBnsEwXW2f1i3UJNs28FBDvC92HX%2FT5AQvt4v5oir8yL9txfJ7cuPJkC1WUM8HZpznIPpjkILQBg%2B9PkfvmVH07DjYFeHlNYQ8EhIoTBtpgSM05ZJHjkN5k0X6SATwGe6NPMz1niMCJcPP3rOuV%2F%2BQ3azRtwQNW1cqPDigFEfWJH61QjcHq0YyHiGuUY6nUNFknyz00WoxdD2Ez74yEUoospfGPl8hjZoowwNuH0AY6mAHQ1UIMwBi%2FhghISuUMx5HekJlTdk2r4jvlxmzyoE2b2v5UMYwntGoi9dkqV2nSstVhJhw5peD1QuTVlQfXsa9Wy5vLv%2F%2F8GKDriXThg2nZqt%2Fo%2B2mIQhl7EerkfdAx3Vf%2FBMk6dvhdf216X1lHmzCrjqvDWbrPftvatFiS2ClYQD6zxMVCOCU1ebiPiTofP4gw8Ip%2Bp2n6HA%3D%3D&Expires=1778514835) - # Data Analyzer — 獨立開發架構說明

> **版本**：v2.0
> **日期**：2026-04-29
> **定位**：Data Analyzer 是可獨立開發、測試的實時串流處...

2. [project_assignment.pdf](https://ppl-ai-file-upload.s3.amazonaws.com/web/direct-files/collection_f130a32f-3bc7-49c8-9eee-1fc964be7bbf/ee34f601-d9e2-467f-8a18-8a5f7e5febfa/project_assignment.pdf?AWSAccessKeyId=ASIA2F3EMEYETY6V6S3T&Signature=oRArNuNQucAfMfAh8vMItQWVvZg%3D&x-amz-security-token=IQoJb3JpZ2luX2VjEE8aCXVzLWVhc3QtMSJHMEUCIQDMR8F0RJNgDZbn7DBO1uMfu%2FhBf3qp5yUc6NttyzSl7AIgN5j%2BaHgPKMPpRZjda1%2Fd4giAWeO9P9o7JfvxWTC9RkUq8wQIGBABGgw2OTk3NTMzMDk3MDUiDH%2BpxiyeP5KkK7IekSrQBJm8yuH4W2mq2GxXZQY9TyYN0iNGIGv1p%2FGfpHJ9RbKuSa5BaF9oeENFWyzFSJtRjznB17DzFtIkrc7S02b52w5IkWlcOxXZJAO3Ve4raH76GcU5f1nt40w2IQecmCsx0P44k8BLyyvYPRdxW2Qv1MsiiiUQaE4fd7RSWhaA%2FuSuMhgcfI89Y8NE%2BS5AF2vu90DOl2ff%2B%2Bo4io75bFUHFA7N9ylC18XDUMLZo7aIT%2FLgvDt8GkLdpMgsVqjDZlYAKCfd09Eg%2BRciT9fnBZxKwp3UtizvgKVeG6spkLFLErzoRMhfwdBBbsDW4QjUIvcd89%2BXqZJPmxAiCl6d7KDCTGXxecLe5Ln9Vhv2uBYnhrizNs47uc8ESZcTYTBIHmZK%2BtPp3IX9MWbyTIGRtIjYXs4jaR%2Ff47PromrmRVMQYZ%2FeB6BT%2Be3sqs6ut%2F1uwwsunJg3ctcBaAslcFxYwtTO2Fq1IloMn7zwLJHpm7hDRX2kBDvP5xoPyGGPnq9AWV9rdbxOimfEfETFE8jatUJeUxujvGvzXAklX9le72TSdA%2FAURm7Q%2F334pBnsEwXW2f1i3UJNs28FBDvC92HX%2FT5AQvt4v5oir8yL9txfJ7cuPJkC1WUM8HZpznIPpjkILQBg%2B9PkfvmVH07DjYFeHlNYQ8EhIoTBtpgSM05ZJHjkN5k0X6SATwGe6NPMz1niMCJcPP3rOuV%2F%2BQ3azRtwQNW1cqPDigFEfWJH61QjcHq0YyHiGuUY6nUNFknyz00WoxdD2Ez74yEUoospfGPl8hjZoowwNuH0AY6mAHQ1UIMwBi%2FhghISuUMx5HekJlTdk2r4jvlxmzyoE2b2v5UMYwntGoi9dkqV2nSstVhJhw5peD1QuTVlQfXsa9Wy5vLv%2F%2F8GKDriXThg2nZqt%2Fo%2B2mIQhl7EerkfdAx3Vf%2FBMk6dvhdf216X1lHmzCrjqvDWbrPftvatFiS2ClYQD6zxMVCOCU1ebiPiTofP4gw8Ip%2Bp2n6HA%3D%3D&Expires=1778514835) - UNIX 系統程式設計 
期末專題說明文件 
 
學期：114 學年度第二學期 
專題佔總成績：30% 
課程代碼：54015

目錄 
一、專題目的與學習目標 
二、專題選項說明 
    選項 A...


#!/bin/bash
set -e

echo "========================================="
echo " Checking Prerequisites"
echo "========================================="
if ! command -v jq >/dev/null 2>&1; then
    echo "jq not found. Installing..."
    sudo apt-get install -y jq
fi
echo "jq: $(jq --version)"
echo ""

echo "========================================="
echo " System Environment"
echo "========================================="
uname -a
echo "CPU Model: $(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)"
echo "CPU Cores: $(nproc)"
echo "RAM Total: $(free -h | awk '/^Mem:/{print $2}')"
echo "Storage:   $(df -h . | awk 'NR==2{print $1, $2, "avail:", $4}')"
echo ""

echo "========================================="
echo " Phase 1: Generating Datasets"
echo "========================================="
rm -rf test_env && mkdir -p test_env
python3 scripts/gen_data.py small
python3 scripts/gen_data.py medium
python3 scripts/gen_data.py malformed

echo ""
echo "========================================="
echo " Phase 2: stream_merge Standalone Benchmark (Medium)"
echo "========================================="
FILE_SIZE=$(wc -c < test_env/bench_medium.bin)
MB_SIZE=$(awk "BEGIN {printf \"%.2f\", $FILE_SIZE/1048576}")

echo "Processing $MB_SIZE MB of binary stream..."

START=$(date +%s.%N)
/usr/bin/time -v ./build/stream_merge bench_medium test_env > test_env/output.jsonl 2>test_env/bench.log
END=$(date +%s.%N)
LATENCY=$(awk "BEGIN {printf \"%.3f\", $END - $START}")

THROUGHPUT=$(awk "BEGIN {printf \"%.2f\", $MB_SIZE / $LATENCY}")
CLIPS=$(wc -l < test_env/output.jsonl)
MAX_RSS=$(grep "Maximum resident" test_env/bench.log | awk '{print $NF}')

echo "Latency (Real Time): $LATENCY seconds"
echo "Throughput: $THROUGHPUT MB/s"
echo "Clips Emitted: $CLIPS"
echo "Max RSS: ${MAX_RSS} kbytes"
echo "See test_env/bench.log for stream_logger output."
echo "========================================="

echo ""
echo "========================================="
echo " Phase 3: log_parse --filter vs jq (Semantic JSON Filter)"
echo "========================================="
echo "Note: grep is shown as reference only -- it does not parse JSON and is NOT a fair baseline."
python3 scripts/gen_data.py jsonl
JSONL_FILE=test_env/bench_jsonl.jsonl
JSONL_SIZE=$(wc -c < "$JSONL_FILE")
JSONL_MB=$(awk "BEGIN {printf \"%.2f\", $JSONL_SIZE/1048576}")
echo "JSONL input: $JSONL_MB MB (50,000 records, 10,000 type=clip)"

START=$(date +%s.%N)
./build/log_parse --filter type=clip < "$JSONL_FILE" > test_env/lp_out.jsonl || true
END=$(date +%s.%N)
LP_TIME=$(awk "BEGIN {printf \"%.4f\", $END - $START}")
LP_TP=$(awk "BEGIN {printf \"%.2f\", $JSONL_MB / $LP_TIME}")
LP_LINES=$(wc -l < test_env/lp_out.jsonl)

if command -v jq >/dev/null 2>&1; then
    START=$(date +%s.%N)
    jq -c 'select(.type == "clip")' "$JSONL_FILE" > test_env/jq_out.jsonl || true
    END=$(date +%s.%N)
    JQ_TIME=$(awk "BEGIN {printf \"%.4f\", $END - $START}")
    JQ_TP=$(awk "BEGIN {printf \"%.2f\", $JSONL_MB / $JQ_TIME}")
    JQ_LINES=$(wc -l < test_env/jq_out.jsonl)
    JQ_RATIO=$(awk "BEGIN {printf \"%.1f\", ($LP_TP / $JQ_TP) * 100}")
    echo "log_parse --filter:  $LP_TP MB/s  ($LP_LINES matched lines)"
    echo "jq select():         $JQ_TP MB/s  ($JQ_LINES matched lines)  [semantic equivalent]"
    echo "log_parse throughput is $JQ_RATIO% of jq"
else
    echo "log_parse --filter:  $LP_TP MB/s  ($LP_LINES matched lines)"
    echo "jq: not installed, skipping semantic comparison"
fi

START=$(date +%s.%N)
grep '"type":"clip"' "$JSONL_FILE" > test_env/grep_out.jsonl || true
END=$(date +%s.%N)
GREP_TIME=$(awk "BEGIN {printf \"%.4f\", $END - $START}")
GREP_TP=$(awk "BEGIN {printf \"%.2f\", $JSONL_MB / $GREP_TIME}")
GREP_LINES=$(wc -l < test_env/grep_out.jsonl)
GREP_RATIO=$(awk "BEGIN {printf \"%.1f\", ($LP_TP / $GREP_TP) * 100}")
echo "grep (reference):    $GREP_TP MB/s  ($GREP_LINES matched lines)  [NOT a fair baseline -- no JSON parsing]"
echo "log_parse throughput is $GREP_RATIO% of grep (unfair comparison)"
echo "========================================="

echo ""
echo "========================================="
echo " Phase 4: End-to-End Pipeline (pipeline_dispatcher)"
echo "========================================="
rm -rf test_env && mkdir -p test_env
python3 scripts/gen_data.py medium
FILE_SIZE=$(wc -c < test_env/bench_medium.bin)
MB_SIZE=$(awk "BEGIN {printf \"%.2f\", $FILE_SIZE/1048576}")
DB_PATH=test_env/clips_e2e.db
echo "Processing $MB_SIZE MB through full pipeline (stream_merge | log_parse | clip_store)..."

START=$(date +%s.%N)
/usr/bin/time -v ./build/pipeline_dispatcher bench_medium test_env "$DB_PATH" 0 \
    2>test_env/e2e.log
END=$(date +%s.%N)
E2E_TIME=$(awk "BEGIN {printf \"%.3f\", $END - $START}")
E2E_TP=$(awk "BEGIN {printf \"%.2f\", $MB_SIZE / $E2E_TIME}")
E2E_CLIPS=$(wc -l < "$DB_PATH")
E2E_RSS=$(grep "Maximum resident" test_env/e2e.log | awk '{print $NF}')

echo "End-to-End Latency:    $E2E_TIME seconds"
echo "End-to-End Throughput: $E2E_TP MB/s"
echo "Clips stored in DB:    $E2E_CLIPS"
echo "Max RSS:               ${E2E_RSS} kbytes"
echo "========================================="

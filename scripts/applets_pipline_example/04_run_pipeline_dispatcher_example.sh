#!/bin/bash
set -e

# Change to the root of the workspace
WORKSPACE_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$WORKSPACE_ROOT"

# Ensure all binaries are built
make > /dev/null

echo "========================================================="
echo " Example 04: pipeline_dispatcher"
echo "========================================================="
echo " The 'pipeline_dispatcher' acts as the supervisor/orchestrator."
echo " It does NOT process data itself, but rather spawns"
echo " and pipes the three applets together:"
echo "   stream_merge | log_parse | clip_store"
echo "========================================================="
echo ""

TMP_DIR=$(mktemp -d)
SESSION="pd_example"
DB_PATH="$TMP_DIR/clips_output.db"

echo "[1] Creating temporary source directory: $TMP_DIR"
touch "$TMP_DIR/$SESSION.bin"
touch "$TMP_DIR/$SESSION.meta.jsonl"

echo "[2] Starting pipeline_dispatcher in background"
./.build/pipeline_dispatcher \
    --ttl 300 \
    --clip-secs 5 \
    --idle-secs 2 \
    --filter "type=clip" \
    "$SESSION" "$TMP_DIR" "$DB_PATH" &

PD_PID=$!

sleep 0.5

echo ""
echo "[3] Simulating incoming data chunks to the source directory..."

echo " -> Chunk 1 (10 bytes @ ts_ms=1000)"
printf '0123456789' >> "$TMP_DIR/$SESSION.bin"
echo '{"kind":"data","sequence":1,"offset":0,"length":10,"ts_ms":1000,"events":["motion"]}' >> "$TMP_DIR/$SESSION.meta.jsonl"
sleep 1

echo " -> Chunk 2 (10 bytes @ ts_ms=2500)"
printf 'abcdefghij' >> "$TMP_DIR/$SESSION.bin"
echo '{"kind":"data","sequence":2,"offset":10,"length":10,"ts_ms":2500,"events":["person"]}' >> "$TMP_DIR/$SESSION.meta.jsonl"

echo "[4] Waiting for idle timeout (2 seconds) to trigger the entire pipeline..."
sleep 3

echo "[5] Writing sentinel to terminate the pipeline"
touch "$TMP_DIR/.pipeline_end"

wait $PD_PID || true

echo ""
echo "[6] Resulting database file:"
if [ -f "$DB_PATH" ]; then
    cat "$DB_PATH"
else
    echo "(Database file not found!)"
fi

echo ""
echo "Cleaning up..."
rm -rf "$TMP_DIR"
echo "Done!"

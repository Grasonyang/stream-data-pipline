#!/bin/bash
set -e

# Change to the root of the workspace
WORKSPACE_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$WORKSPACE_ROOT"

# Ensure the binary is built
make .build/stream_merge > /dev/null

echo "========================================================="
echo " Example 01: stream_merge"
echo "========================================================="
echo " The 'stream_merge' applet reads chunks of data from a"
echo " .bin file and their corresponding metadata from a .jsonl"
echo " sidecar. It aggregates them into time-windowed clips,"
echo " saving the consolidated clip into a new .bin file and"
echo " outputting a JSON log record to stdout."
echo "========================================================="
echo ""

TMP_DIR=$(mktemp -d)
SESSION="sm_example"

echo "[1] Creating temporary source directory: $TMP_DIR"
touch "$TMP_DIR/$SESSION.bin"
touch "$TMP_DIR/$SESSION.meta.jsonl"

echo "[2] Starting stream_merge in background"
# --clip-secs defines the max duration of a clip
# --idle-secs defines how long to wait for new data before flushing a partial clip
./.build/stream_merge \
    --clip-secs 5 \
    --idle-secs 2 \
    "$SESSION" "$TMP_DIR" &
SM_PID=$!

sleep 0.5

echo "[3] Simulating incoming data chunks..."

echo " -> Chunk 1 (10 bytes @ ts_ms=1000)"
printf '0123456789' >> "$TMP_DIR/$SESSION.bin"
echo '{"kind":"data","sequence":1,"offset":0,"length":10,"ts_ms":1000,"events":["motion"]}' >> "$TMP_DIR/$SESSION.meta.jsonl"
sleep 1

echo " -> Chunk 2 (5 bytes @ ts_ms=3500) (within 5-sec window)"
printf 'abcde' >> "$TMP_DIR/$SESSION.bin"
echo '{"kind":"data","sequence":2,"offset":10,"length":5,"ts_ms":3500,"events":["person"]}' >> "$TMP_DIR/$SESSION.meta.jsonl"

echo "[4] Waiting for idle timeout (2 seconds) to trigger a partial clip flush..."
sleep 3

echo "[5] Writing sentinel to terminate stream_merge"
touch "$TMP_DIR/.pipeline_end"

wait $SM_PID || true

echo ""
echo "[6] Resulting files in $TMP_DIR:"
ls -l "$TMP_DIR" | grep "$SESSION"

echo ""
echo "Cleaning up..."
rm -rf "$TMP_DIR"
echo "Done!"

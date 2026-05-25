#!/bin/bash
set -e

# Change to the root of the workspace
WORKSPACE_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$WORKSPACE_ROOT"

# Ensure the binary is built
make .build/log_parse > /dev/null

echo "========================================================="
echo " Example 02: log_parse"
echo "========================================================="
echo " The 'log_parse' applet processes logs or JSON records"
echo " from stdin. It can extract fields using regex, filter"
echo " records using expressions (e.g., 'type=clip'), and"
echo " output them as JSON, CSV, or a simple count."
echo "========================================================="
echo ""

echo "[1] Example: Parsing JSON log records (Pass-through mode)"
echo "Input:"
cat << 'EOF'
  {"type":"clip","session_id":"sess_1","duration":5,"path":"/tmp/a.bin"}
  {"type":"heartbeat","session_id":"sess_1"}
  {"type":"clip","session_id":"sess_1","duration":10,"path":"/tmp/b.bin"}
EOF

echo ""
echo "Command:"
echo "  ./.build/log_parse --filter 'type=clip'"
echo "Output:"
cat << 'EOF' | ./.build/log_parse --filter 'type=clip'
{"type":"clip","session_id":"sess_1","duration":5,"path":"/tmp/a.bin"}
{"type":"heartbeat","session_id":"sess_1"}
{"type":"clip","session_id":"sess_1","duration":10,"path":"/tmp/b.bin"}
EOF
echo ""

echo "[2] Example: Extracting fields from plain text using Regex"
echo "Input:"
cat << 'EOF'
  1700000000 INFO  clip generated at /tmp/clips/a.mp4
  1700000001 DEBUG memory usage 40%
  1700000005 INFO  clip generated at /tmp/clips/b.mp4
EOF

echo ""
echo "Command:"
echo "  ./.build/log_parse \\"
echo "    --regex '^ *([0-9]+) [A-Z]+ +clip generated at (.+)$' \\"
echo "    --fields ts,path \\"
echo "    --format csv"
echo ""
echo "Output:"
cat << 'EOF' | ./.build/log_parse --regex '^ *([0-9]+) [A-Z]+ +clip generated at (.+)$' --fields ts,path --format csv
  1700000000 INFO  clip generated at /tmp/clips/a.mp4
  1700000001 DEBUG memory usage 40%
  1700000005 INFO  clip generated at /tmp/clips/b.mp4
EOF
echo ""
echo "Done!"

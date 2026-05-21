#!/bin/sh
set -eu

PIPELINE_DISPATCHER=$(realpath "${PIPELINE_DISPATCHER:-./build/pipeline_dispatcher}")
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

check_eq() {
    name=$1
    expected=$2
    actual=$3
    if [ "$expected" != "$actual" ]; then
        printf 'FAIL %s\nexpected: %s\nactual:   %s\n' "$name" "$expected" "$actual" >&2
        exit 1
    fi
}

# ── missing args ────────────────────────────────────────────────────────
set +e
"$PIPELINE_DISPATCHER" >"$TMP_DIR/missing.out" 2>"$TMP_DIR/missing.err"
rc=$?
set -e
check_eq "missing args exit"   "2" "$rc"
check_eq "missing args stdout" ""  "$(cat "$TMP_DIR/missing.out")"

# ── missing src dir → child failure ────────────────────────────────────
set +e
"$PIPELINE_DISPATCHER" sess_missing "$TMP_DIR/nope" "$TMP_DIR/missing.db" 300 \
    >"$TMP_DIR/fail.out" 2>"$TMP_DIR/fail.err"
rc=$?
set -e
check_eq "child failure exit" "254" "$rc"

# ── happy path: binary .bin + .meta.jsonl sidecar ──────────────────────
SESSION=sess_dispatch
: >"$TMP_DIR/$SESSION.bin"
: >"$TMP_DIR/$SESSION.meta.jsonl"

(
    cd /tmp
    "$PIPELINE_DISPATCHER" "$SESSION" "$TMP_DIR" "$TMP_DIR/clips.db" 300 \
        >"$TMP_DIR/ok.out" 2>"$TMP_DIR/ok.err"
) &
pid=$!
sleep 0.1

# One chunk with ts_ms=7000 → ts=7 in the clip record.
printf '\x00\x01\x02\x03\x04' >>"$TMP_DIR/$SESSION.bin"
printf '{"kind":"data","seq":1,"offset":0,"length":5,"ts_ms":7000}\n' \
    >>"$TMP_DIR/$SESSION.meta.jsonl"
touch "$TMP_DIR/.pipeline_end"
wait "$pid"

check_eq "dispatcher stdout" "" "$(cat "$TMP_DIR/ok.out")"
# Verify the DB key (session_id:ts) was written; path is synthetic so not checked.
check_eq "dispatcher db key" "sess_dispatch:7" \
    "$(cut -f1 "$TMP_DIR/clips.db")"

printf 'OK: all pipeline_dispatcher tests passed\n'

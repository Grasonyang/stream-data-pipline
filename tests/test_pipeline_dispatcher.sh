#!/bin/sh
set -eu

PIPELINE_DISPATCHER=$(realpath "${PIPELINE_DISPATCHER:-./.build/pipeline_dispatcher}")
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

# ── invalid options / missing src dir ──────────────────────────────────
set +e
"$PIPELINE_DISPATCHER" --ttl nope sess_bad "$TMP_DIR" "$TMP_DIR/bad.db" \
    >"$TMP_DIR/bad_ttl.out" 2>"$TMP_DIR/bad_ttl.err"
rc=$?
set -e
check_eq "invalid ttl exit" "1" "$rc"

set +e
"$PIPELINE_DISPATCHER" --clip-secs 0 sess_bad "$TMP_DIR" "$TMP_DIR/bad.db" \
    >"$TMP_DIR/bad_clip.out" 2>"$TMP_DIR/bad_clip.err"
rc=$?
set -e
check_eq "invalid clip exit" "1" "$rc"

set +e
"$PIPELINE_DISPATCHER" sess_missing "$TMP_DIR/nope" "$TMP_DIR/missing.db" \
    >"$TMP_DIR/fail.out" 2>"$TMP_DIR/fail.err"
rc=$?
set -e
check_eq "missing src exit" "1" "$rc"

# ── happy path: binary .bin + .meta.jsonl sidecar ──────────────────────
SESSION=sess_dispatch
: >"$TMP_DIR/$SESSION.bin"
: >"$TMP_DIR/$SESSION.meta.jsonl"

(
    cd /tmp
    "$PIPELINE_DISPATCHER" --ttl 300 --clip-secs 5 --idle-secs 10 \
        "$SESSION" "$TMP_DIR" "$TMP_DIR/clips.db" \
        >"$TMP_DIR/ok.out" 2>"$TMP_DIR/ok.err"
) &
pid=$!
sleep 0.1

# One chunk with ts_ms=7000 → ts=7 in the clip record.
printf '\x00\x01\x02\x03\x04' >>"$TMP_DIR/$SESSION.bin"
printf '{"kind":"data","sequence":1,"offset":0,"length":5,"ts_ms":7000}\n' \
    >>"$TMP_DIR/$SESSION.meta.jsonl"
touch "$TMP_DIR/.pipeline_end"
wait "$pid"

check_eq "dispatcher stdout" "" "$(cat "$TMP_DIR/ok.out")"
# Verify the DB key (session_id:ts) was written; path is synthetic so not checked.
check_eq "dispatcher db key" "sess_dispatch:7" \
    "$(cut -f1 "$TMP_DIR/clips.db")"

check_eq "child log contains name" "2" \
    "$(grep -c 'stream_merge pid=' "$TMP_DIR/ok.err" || true)"

printf 'OK: all pipeline_dispatcher tests passed\n'

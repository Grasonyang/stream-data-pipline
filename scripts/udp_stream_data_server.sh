#!/usr/bin/env bash
set -eu

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-19000}
ROOT_DIR=${ROOT_DIR:-/tmp/udp_stream_data}
DB_PATH=${DB_PATH:-$ROOT_DIR/clips.db}
CLIP_TTL=${CLIP_TTL:-300}
PIPELINE_DISPATCHER=${PIPELINE_DISPATCHER:-$(pwd)/build/pipeline_dispatcher}

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Runs a tiny UDP ingestor demo that writes session.bin/session.meta.jsonl and
starts pipeline_dispatcher for each session.

Options:
  --host <addr>           Bind address (default: ${HOST})
  --port <port>           UDP port (default: ${PORT})
  --root-dir <dir>        Session artifact root (default: ${ROOT_DIR})
  --db <path>             clip_store database path (default: ${DB_PATH})
  --ttl <seconds>         TTL passed to pipeline_dispatcher (default: ${CLIP_TTL})
  --dispatcher <path>     pipeline_dispatcher binary path
  -h, --help              Show this help

Protocol:
  STRT <session_id>
  DATASEQ <seq> <ts_ms> <base64_payload>
  END
  SHUTDOWN

Notes:
  - The server launches pipeline_dispatcher immediately after STRT so the
    applets can consume a growing .meta.jsonl file.
  - DATASEQ lets you force sequence gaps for stream_merge demos.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --host)
            HOST=$2
            shift 2
            ;;
        --port)
            PORT=$2
            shift 2
            ;;
        --root-dir)
            ROOT_DIR=$2
            shift 2
            ;;
        --db)
            DB_PATH=$2
            shift 2
            ;;
        --ttl)
            CLIP_TTL=$2
            shift 2
            ;;
        --dispatcher)
            PIPELINE_DISPATCHER=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            printf 'unknown option: %s\n' "$1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

mkdir -p "$ROOT_DIR"

export HOST PORT ROOT_DIR DB_PATH CLIP_TTL PIPELINE_DISPATCHER

python3 - <<'PY'
import base64
import json
import os
import pathlib
import socket
import subprocess
import sys


HOST = os.environ["HOST"]
PORT = int(os.environ["PORT"])
ROOT_DIR = pathlib.Path(os.environ["ROOT_DIR"])
DB_PATH = os.environ["DB_PATH"]
CLIP_TTL = os.environ["CLIP_TTL"]
PIPELINE_DISPATCHER = os.environ["PIPELINE_DISPATCHER"]


def log(message: str) -> None:
    print(f"[udp-server] {message}", file=sys.stderr, flush=True)


def ensure_dispatcher() -> None:
    if not os.path.exists(PIPELINE_DISPATCHER):
        raise SystemExit(f"dispatcher not found: {PIPELINE_DISPATCHER}")


def start_session(session_id: str):
    session_dir = ROOT_DIR / session_id
    session_dir.mkdir(parents=True, exist_ok=True)
    bin_path = session_dir / f"{session_id}.bin"
    meta_path = session_dir / f"{session_id}.meta.jsonl"
    bin_fp = open(bin_path, "ab", buffering=0)
    meta_fp = open(meta_path, "a", encoding="utf-8")
    proc = subprocess.Popen(
        [PIPELINE_DISPATCHER, session_id, str(session_dir), DB_PATH, CLIP_TTL],
        stdout=subprocess.DEVNULL,
    )
    log(f"started session={session_id} dispatcher_pid={proc.pid}")
    return {
        "id": session_id,
        "dir": session_dir,
        "bin_fp": bin_fp,
        "meta_fp": meta_fp,
        "proc": proc,
        "offset": 0,
    }


def write_chunk(state, seq: int, ts_ms: int, payload_b64: str) -> None:
    payload = base64.b64decode(payload_b64)
    offset = state["offset"]
    state["bin_fp"].write(payload)
    record = {
        "kind": "data",
        "sequence": seq,
        "offset": offset,
        "length": len(payload),
        "ts_ms": ts_ms,
    }
    state["meta_fp"].write(json.dumps(record, separators=(",", ":")) + "\n")
    state["meta_fp"].flush()
    state["offset"] += len(payload)
    log(f"session={state['id']} seq={seq} ts_ms={ts_ms} bytes={len(payload)}")


def end_session(state) -> None:
    sentinel = state["dir"] / ".pipeline_end"
    sentinel.touch()
    state["meta_fp"].close()
    state["bin_fp"].close()
    rc = state["proc"].wait(timeout=10)
    log(f"ended session={state['id']} dispatcher_rc={rc}")


def main() -> None:
    ensure_dispatcher()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((HOST, PORT))
    log(f"listening on udp://{HOST}:{PORT}")

    current = None
    while True:
        data, addr = sock.recvfrom(65535)
        line = data.decode("utf-8").strip()
        if not line:
            continue
        parts = line.split(" ", 3)
        cmd = parts[0]
        log(f"recv from {addr[0]}:{addr[1]} -> {line}")

        if cmd == "SHUTDOWN":
            if current is not None:
                end_session(current)
            log("shutdown requested")
            return

        if cmd == "STRT":
            if len(parts) != 2:
                log("invalid STRT packet")
                continue
            if current is not None:
                end_session(current)
            current = start_session(parts[1])
            continue

        if cmd == "END":
            if current is None:
                log("END ignored; no active session")
                continue
            end_session(current)
            current = None
            continue

        if cmd == "DATASEQ":
            if current is None:
                log("DATASEQ ignored; no active session")
                continue
            if len(parts) != 4:
                log("invalid DATASEQ packet")
                continue
            try:
                seq = int(parts[1])
                ts_ms = int(parts[2])
            except ValueError:
                log("invalid numeric field in DATASEQ")
                continue
            write_chunk(current, seq, ts_ms, parts[3])
            continue

        log(f"unknown command: {cmd}")


if __name__ == "__main__":
    main()
PY

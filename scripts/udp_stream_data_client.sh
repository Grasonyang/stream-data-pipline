#!/usr/bin/env bash
set -eu

HOST=${HOST:-127.0.0.1}
PORT=${PORT:-19000}
SESSION=${SESSION:-demo_udp_session}
MODE=demo

usage() {
    cat <<EOF
Usage: $0 [OPTIONS]

Sends demo UDP packets to udp_stream_data_server.sh.

Options:
  --host <addr>           Server host (default: ${HOST})
  --port <port>           Server port (default: ${PORT})
  --session <id>          Session id for demo packets (default: ${SESSION})
  --mode <demo|gap>       Send normal or gap demo (default: ${MODE})
  --shutdown              Ask the server to stop
  -h, --help              Show this help

Protocol sent by this client:
  STRT <session_id>
  DATASEQ <seq> <ts_ms> <base64_payload>
  END
EOF
}

SHUTDOWN=0

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
        --session)
            SESSION=$2
            shift 2
            ;;
        --mode)
            MODE=$2
            shift 2
            ;;
        --shutdown)
            SHUTDOWN=1
            shift 1
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

export HOST PORT SESSION MODE SHUTDOWN

python3 - <<'PY'
import base64
import os
import socket
import sys
import time


HOST = os.environ["HOST"]
PORT = int(os.environ["PORT"])
SESSION = os.environ["SESSION"]
MODE = os.environ["MODE"]
SHUTDOWN = os.environ["SHUTDOWN"] == "1"


def log(message: str) -> None:
    print(f"[udp-client] {message}", file=sys.stderr, flush=True)


def packet_payload(text: str) -> str:
    return base64.b64encode(text.encode("utf-8")).decode("ascii")


def send_line(sock: socket.socket, line: str) -> None:
    sock.sendto(line.encode("utf-8"), (HOST, PORT))
    log(f"sent -> {line}")
    time.sleep(0.05)


def send_demo(sock: socket.socket) -> None:
    send_line(sock, f"STRT {SESSION}")
    send_line(sock, f"DATASEQ 1 0 {packet_payload('AAAA')}")
    send_line(sock, f"DATASEQ 2 3000 {packet_payload('BBBB')}")
    send_line(sock, f"DATASEQ 3 6000 {packet_payload('CCCC')}")
    send_line(sock, "END")


def send_gap_demo(sock: socket.socket) -> None:
    send_line(sock, f"STRT {SESSION}")
    send_line(sock, f"DATASEQ 1 0 {packet_payload('AAAA')}")
    send_line(sock, f"DATASEQ 5 1000 {packet_payload('GGGG')}")
    send_line(sock, "END")


def main() -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if SHUTDOWN:
        send_line(sock, "SHUTDOWN")
        return
    if MODE == "demo":
        send_demo(sock)
        return
    if MODE == "gap":
        send_gap_demo(sock)
        return
    raise SystemExit(f"unknown mode: {MODE}")


if __name__ == "__main__":
    main()
PY

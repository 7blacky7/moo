#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="/tmp/moo_net_large_write"
SERVER_LOG="/tmp/moo_net_large_server.log"
CLIENT_LOG="/tmp/moo_net_large_client.log"
PORT=$((47000 + ($$ % 1000)))

cd "$ROOT"
compiler/target/release/moo-compiler compile \
  beispiele/netz_large_write_selftest.moos -o "$BIN"

: >"$SERVER_LOG"
: >"$CLIENT_LOG"
env NET_LARGE_ROLE=server NET_LARGE_PORT="$PORT" \
  timeout 40s "$BIN" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!
cleanup() {
  if kill -0 "$SERVER_PID" 2>/dev/null; then
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

env NET_LARGE_ROLE=client NET_LARGE_PORT="$PORT" \
  timeout 40s "$BIN" >"$CLIENT_LOG" 2>&1
wait "$SERVER_PID"
trap - EXIT

grep -q '^NET-LARGE CLIENT PASS$' "$CLIENT_LOG"
grep -q '^NET-LARGE SERVER PASS$' "$SERVER_LOG"
echo "NET-LARGE-WRITE PASS"

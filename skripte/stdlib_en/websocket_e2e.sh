#!/usr/bin/env bash
# P1-T506: E2E des englischen WebSocket-Clients (ws_connect, send_text, receive, close)
# gegen beispiele/domain/web/websocket_server.moos (nur IPv4, wie beispiele/websocket_client_selftest.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MOO="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
SERVER_BIN="$(mktemp -u /tmp/moo_ws_en_server.XXXXXX)"
CLIENT_BIN="$(mktemp -u /tmp/moo_ws_en_client.XXXXXX)"
PORT=$((48000 + ($$ % 900)))
SERVER_PID=""

cd "$ROOT"
"$MOO" compile beispiele/domain/web/websocket_server.moos -o "$SERVER_BIN"
"$MOO" compile skripte/stdlib_en/websocket_client_en.moos -o "$CLIENT_BIN"

cleanup() {
  if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
  rm -f "$SERVER_BIN" "$CLIENT_BIN"
}
trap cleanup EXIT

server_log="$(mktemp)"
client_log="$(mktemp)"
env WS_TEST_PORT="$PORT" WS_TEST_ONESHOT=1 timeout 30s "$SERVER_BIN" >"$server_log" 2>&1 &
SERVER_PID=$!
sleep 0.5
env WS_TEST_URL="ws://localhost:$PORT" timeout 30s "$CLIENT_BIN" >"$client_log" 2>&1
wait "$SERVER_PID"
SERVER_PID=""
grep -q '^WEBSOCKET-CLIENT PASS$' "$client_log"
grep -q '^WEBSOCKET-SERVER PASS$' "$server_log"
rm -f "$server_log" "$client_log"
echo "WEBSOCKET-EN-E2E PASS"

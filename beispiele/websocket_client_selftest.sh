#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER_BIN="/tmp/moo_websocket_server"
CLIENT_BIN="/tmp/moo_websocket_client"
BASE_PORT=$((48000 + ($$ % 900)))
SERVER_PID=""

cd "$ROOT"
compiler/target/release/moo-compiler compile \
  beispiele/domain/web/websocket_server.moos -o "$SERVER_BIN"
compiler/target/release/moo-compiler compile \
  beispiele/websocket_client_selftest.moos -o "$CLIENT_BIN"

cleanup() {
  if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill -TERM "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

run_case() {
  local label="$1" port="$2" url="$3"
  local server_log="/tmp/moo_websocket_server_${label}.log"
  local client_log="/tmp/moo_websocket_client_${label}.log"
  : >"$server_log"
  : >"$client_log"
  env WS_TEST_PORT="$port" WS_TEST_ONESHOT=1 \
    timeout 30s "$SERVER_BIN" >"$server_log" 2>&1 &
  SERVER_PID=$!
  sleep 0.5
  env WS_TEST_URL="$url" \
    timeout 30s "$CLIENT_BIN" >"$client_log" 2>&1
  wait "$SERVER_PID"
  SERVER_PID=""
  grep -q '^WEBSOCKET-CLIENT PASS$' "$client_log"
  grep -q '^WEBSOCKET-SERVER PASS$' "$server_log"
  echo "WEBSOCKET-${label} PASS"
}

run_case IPV4 "$BASE_PORT" "ws://localhost:$BASE_PORT"
IPV6_PORT=$((BASE_PORT + 1))
run_case IPV6 "$IPV6_PORT" "ws://[::1]:$IPV6_PORT"
trap - EXIT
echo "WEBSOCKET-E2E PASS"

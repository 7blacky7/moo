#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
URL="${WS_WSS_URL:-wss://ws.postman-echo.com/raw}"
cd "$ROOT"

run_backend() {
  local backend="$1"
  local bin="/tmp/moo_websocket_wss_${backend}"
  local log="/tmp/moo_websocket_wss_${backend}.log"
  (
    cd compiler
    MOO_TLS_BACKEND="$backend" mise exec -- cargo build --release \
      >"/tmp/moo_websocket_build_${backend}.log" 2>&1
  )
  compiler/target/release/moo-compiler compile \
    beispiele/websocket_wss_smoke.moos -o "$bin"
  env WS_WSS_URL="$URL" timeout 45s "$bin" >"$log" 2>&1
  grep -q '^WEBSOCKET-WSS PASS$' "$log"
  echo "WEBSOCKET-WSS-${backend} PASS"
}

run_backend openssl
run_backend mbedtls
(
  cd compiler
  MOO_TLS_BACKEND=openssl mise exec -- cargo build --release \
    >/tmp/moo_websocket_build_default_restore.log 2>&1
)
echo "WEBSOCKET-WSS-BACKENDS PASS"

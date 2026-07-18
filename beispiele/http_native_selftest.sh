#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="$ROOT/compiler/target/release/moo-compiler"
BIN="/tmp/moo_http_native_selftest"
OUT="/tmp/moo_http_native_selftest.out"

export PATH="$HOME/.cargo/bin:$PATH"
(
    cd "$ROOT/compiler"
    cargo build --release >/dev/null
)
"$COMPILER" compile "$ROOT/beispiele/http_native_selftest.moos" -o "$BIN" >/dev/null
timeout 20s stdbuf -o0 -e0 "$BIN" >"$OUT"
if ! grep -qx 'HTTP-NATIVE SELFTEST PASS' "$OUT"; then
    cat "$OUT" >&2
    rm -f "$BIN" "$OUT"
    exit 1
fi
cat "$OUT"
rm -f "$BIN" "$OUT"

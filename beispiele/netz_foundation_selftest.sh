#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="$ROOT/compiler/target/release/moo-compiler"
BIN="/tmp/moo_net_foundation_selftest"
OUT="/tmp/moo_net_foundation_selftest.out"
EXPECTED=$'echo:dns\necho:v6\n1\nudp:dns\n1'

export PATH="$HOME/.cargo/bin:$PATH"
(
    cd "$ROOT/compiler"
    cargo build --release >/dev/null
)

"$COMPILER" compile "$ROOT/beispiele/netz_foundation_selftest.moos" -o "$BIN" >/dev/null
timeout 15s stdbuf -o0 -e0 "$BIN" >"$OUT"
ACTUAL="$(cat "$OUT")"
rm -f "$BIN" "$OUT"

if [[ "$ACTUAL" != "$EXPECTED" ]]; then
    printf 'NET-FOUNDATION FAIL\nErwartet:\n%s\nBekommen:\n%s\n' "$EXPECTED" "$ACTUAL" >&2
    exit 1
fi
printf 'NET-FOUNDATION PASS\n'

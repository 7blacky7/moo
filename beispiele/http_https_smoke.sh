#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="$ROOT/compiler/target/release/moo-compiler"
SRC="$ROOT/beispiele/http_https_smoke.moos"
export PATH="$HOME/.cargo/bin:$PATH"

for backend in openssl mbedtls; do
    echo "HTTPS backend=$backend"
    (
        cd "$ROOT/compiler"
        MOO_TLS_BACKEND="$backend" cargo build --release >/dev/null
    )
    bin="/tmp/moo_https_smoke_${backend}"
    out="/tmp/moo_https_smoke_${backend}.out"
    "$COMPILER" compile "$SRC" -o "$bin" >/dev/null
    timeout 45s stdbuf -o0 -e0 "$bin" >"$out"
    if ! grep -qx 'HTTPS-SMOKE PASS' "$out"; then
        cat "$out" >&2
        rm -f "$bin" "$out"
        exit 1
    fi
    cat "$out"
    rm -f "$bin" "$out"
done

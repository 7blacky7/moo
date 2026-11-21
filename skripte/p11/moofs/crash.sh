#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/moofs
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/moofs_crash.moos -o "$out/crash"
uv run --no-project --offline python -B skripte/p11/moofs/crash.py "$out/crash" "$out"

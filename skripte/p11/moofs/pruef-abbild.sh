#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/moofs
mkdir -p "$out"
if [ ! -f "$out/abbild.img" ]; then
  mise -C skripte/p11/moofs run abbild
fi
"$compiler" compile beispiele/werkzeuge/pruefe_moofs.moos -o "$out/pruefe"
uv run --no-project --offline python -B skripte/p11/moofs/pruef-abbild.py "$out/pruefe" "$out"

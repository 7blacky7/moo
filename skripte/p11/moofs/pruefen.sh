#!/usr/bin/env bash
set -euo pipefail
bild="${1:?Aufruf: pruefen.sh BILD [RECORDKAPAZITAET]}"
records="${2:-4096}"
case "$bild" in /*) ;; *) bild="$PWD/$bild" ;; esac
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out=build/p11/moofs
mkdir -p "$out"
"$compiler" compile beispiele/werkzeuge/pruefe_moofs.moos -o "$out/pruefe"
MOO_MOOFS_PRUEF_BILD="$bild" MOO_MOOFS_PRUEF_RECORDS="$records" "$out/pruefe"

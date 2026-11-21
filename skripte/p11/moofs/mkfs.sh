#!/usr/bin/env bash
set -euo pipefail
if [ "$#" -lt 2 ] || [ "$#" -gt 4 ]; then
  echo 'Aufruf: mkfs.sh NEUES_BILD BYTES [QUELLE [DATEINAME]]' >&2
  exit 2
fi
case "$1" in /*) export MOO_MOOFS_BILD="$1" ;; *) export MOO_MOOFS_BILD="$PWD/$1" ;; esac
export MOO_MOOFS_BYTES="$2"
case "${3:-}" in "") export MOO_MOOFS_QUELLE= ;; /*) export MOO_MOOFS_QUELLE="$3" ;; *) export MOO_MOOFS_QUELLE="$PWD/$3" ;; esac
export MOO_MOOFS_NAME="${4:-daten.bin}"
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
mkdir -p build/p11/moofs
"$compiler" compile beispiele/werkzeuge/mkfs_moofs.moos -o build/p11/moofs/mkfs
exec build/p11/moofs/mkfs

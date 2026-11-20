#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
cpu="${1:-4}"
case "$cpu" in 1|4) ;; *) echo 'CPU-Anzahl: 1 oder 4' >&2; exit 2 ;; esac
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
export MOO_COMPILER="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
export MOO_UEFI_PRUEFUNG=kern
export MOO_UEFI_KVM="${MOO_UEFI_KVM:-1}"
export MOO_UEFI_DATEI_PROBE=1
export MOO_UEFI_BENUTZER=2
export MOO_UEFI_BENUTZER_SRC="$PWD/beispiele/moos/datei_probe.moos"
export MOO_UEFI_BENUTZER_ERWARTET="$PWD/beispiele/moos/datei_probe.expected"
export MOO_UEFI_SMP="$cpu"
export MOO_UEFI_OUT="$PWD/build/p11/vfs/datei-$cpu"
bash scripts/uefi-smoke.sh

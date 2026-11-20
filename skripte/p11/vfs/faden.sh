#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
export MOO_COMPILER="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
export MOO_UEFI_PRUEFUNG=kern
export MOO_UEFI_KVM="${MOO_UEFI_KVM:-1}"
export MOO_UEFI_BENUTZER=2
export MOO_UEFI_BENUTZER_SRC="$PWD/beispiele/moos/faden_probe.moos"
export MOO_UEFI_BENUTZER_ERWARTET="$PWD/skripte/p8/smp/benutzer_faden.expected"
export MOO_UEFI_SMP=4
export MOO_UEFI_WAIT=90
export MOO_UEFI_OUT="$PWD/build/p11/vfs/faden-regression"
bash scripts/uefi-smoke.sh

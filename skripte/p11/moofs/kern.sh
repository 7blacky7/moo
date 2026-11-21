#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
export MOO_COMPILER="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
if [ ! -f build/p11/moofs/abbild.img ] || [ ! -f build/p11/moofs/bitkipper.img ]; then
  mise -C skripte/p11/moofs run abbild
fi
export MOO_UEFI_PRUEFUNG=kern
export MOO_UEFI_SMP=4
export MOO_UEFI_KVM="${MOO_UEFI_KVM:-1}"
export MOO_UEFI_WAIT=90
"$MOO_COMPILER" compile compiler/tests/p11/moofs_kern_schreiben.moos -o build/p11/moofs/kern-pruefer
export MOO_MOOFS_KERN_PRUEFER="$PWD/build/p11/moofs/kern-pruefer"
read -r -a moofs_modes <<< "${MOO_MOOFS_MODI:-1 bitkipper schreiben}"
for mode in "${moofs_modes[@]}"; do
  export MOO_UEFI_MOOFS_PROBE="$mode"
  image=abbild
  [ "$mode" != bitkipper ] || image=bitkipper
  export MOO_UEFI_MOOFS_BILD="$PWD/build/p11/moofs/$image.img"
  export MOO_UEFI_OUT="$PWD/build/p11/moofs/kern-$mode"
  bash scripts/uefi-smoke.sh
done

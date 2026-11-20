#!/usr/bin/env bash
# Zusätzliche T18-Pfade: Ergebnisbesitz/Fehler und Kanalbedingungen im Ring3-Backend.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
out=${MOO_BENUTZER_REGRESSION_OUT:-$root/build/p10-t18/regression}
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
for fixture in warten_mehrfach spiel_kanal; do
  dir="$out/$fixture"
  mkdir -p "$dir"
  MOO_COMPILER="$compiler" MOO_UEFI_PRUEFUNG=kern MOO_UEFI_SMP=4 MOO_UEFI_KVM=1 \
    MOO_UEFI_BENUTZER=2 MOO_UEFI_BENUTZER_SRC="$root/skripte/p8/smp/benutzer_$fixture.moos" \
    MOO_UEFI_BENUTZER_ERWARTET="$root/compiler/tests/p2/rc_geteilt/$fixture.expected" \
    MOO_UEFI_WAIT=90 MOO_UEFI_OUT="$dir" bash scripts/uefi-smoke.sh >"$dir/smoke.log" 2>&1 ||
      { tail -80 "$dir/smoke.log" >&2; exit 1; }
  echo "PASS $fixture Ring3 -smp 4: .expected identisch, PFA-Bilanz"
done

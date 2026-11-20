#!/usr/bin/env bash
# P10-T18: unveränderte Pflichtfixture hosted, Kernel und Ring3.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
out=${MOO_BENUTZER_OUT:-$root/build/p10-t18/gate}
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
mkdir -p "$out/abi"
MOO_COMPILER="$compiler" MOO_UEFI_PRUEFUNG=kern MOO_UEFI_SMP=4 MOO_UEFI_KVM=1 \
  MOO_UEFI_BENUTZER=2 MOO_UEFI_BENUTZER_SRC="$root/beispiele/moos/faden_probe.moos" \
  MOO_UEFI_BENUTZER_ERWARTET="$root/skripte/p8/smp/benutzer_faden.expected" \
  MOO_UEFI_WAIT=90 MOO_UEFI_OUT="$out/abi" bash scripts/uefi-smoke.sh >"$out/abi/smoke.log" 2>&1 ||
    { tail -80 "$out/abi/smoke.log" >&2; exit 1; }
echo "PASS Ring3-ABI: Thread-Griffe, Futex-Rennen, Fehleradressen und FS-TLS"
MOO_COMPILER="$compiler" MOO_THREAD_FIXTURES=sperre_konto MOO_THREAD_CPUS="1 4" \
  MOO_THREAD_OUT="$out/vertrag" bash skripte/p8/smp/laufzeit.sh
for cpus in 1 4; do
  dir="$out/ring3-$cpus"
  mkdir -p "$dir"
  MOO_COMPILER="$compiler" MOO_UEFI_PRUEFUNG=kern MOO_UEFI_SMP="$cpus" MOO_UEFI_KVM=1 \
    MOO_UEFI_BENUTZER=2 MOO_UEFI_BENUTZER_SRC="$root/skripte/p8/smp/benutzer_sperre_konto.moos" \
    MOO_UEFI_BENUTZER_ERWARTET="$root/compiler/tests/p2/rc_geteilt/sperre_konto.expected" \
    MOO_UEFI_WAIT=90 MOO_UEFI_OUT="$dir" bash scripts/uefi-smoke.sh >"$dir/smoke.log" 2>&1 ||
      { tail -80 "$dir/smoke.log" >&2; exit 1; }
  grep -aEx "MOO-AP-ONLINE $((cpus-1))[[:space:]]*" "$dir/serial.log"
  grep -aEx "MOO-KERNE $cpus[[:space:]]*" "$dir/serial.log"
  echo "PASS sperre_konto Ring3 -smp $cpus: .expected identisch, PFA-Bilanz"
done

#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
out=${MOO_P11_SIMD_KERNEL_OUT:-$root/build/p11/simd-kern}
mkdir -p "$out"
# TCG ohne GFNI prüft FX-, XMM- und YMM-Zustand. Echte GFNI-Ausführung:
# mise run simd-winrm auf der vorhandenen N100-Testbox.
profile_liste=(fx skalar basis avx)
if [ "${MOO_SIMD_QEMU_GFNI:-0}" = 1 ]; then profile_liste+=(gfni128 gfni); fi
for cpus in 1 4; do
  for profile in "${profile_liste[@]}"; do
    [ "$cpus-$profile" != 4-fx ] || continue
    case "$profile" in
      fx) model='max,-xsave,-avx,-avx2,-gfni'; avx=0; gfni=0 ;;
      skalar) model='max,-ssse3,-avx,-avx2,-gfni'; avx=0; gfni=0 ;;
      basis) model='max,-avx,-avx2,-gfni'; avx=0; gfni=0 ;;
      avx) model='max,-gfni'; avx=1; gfni=0 ;;
      gfni128) model='max,-avx,-avx2,+gfni'; avx=0; gfni=1 ;;
      gfni) model='max,+gfni'; avx=1; gfni=1 ;;
    esac
    lauf="$out/$cpus-$profile"
    MOO_COMPILER="$compiler" MOO_UEFI_OUT="$lauf" MOO_UEFI_PRUEFUNG=kern \
      MOO_UEFI_SMP="$cpus" MOO_UEFI_SMP_STRESS=1 MOO_UEFI_CPU="$model" \
      bash scripts/uefi-smoke.sh >"$out/$cpus-$profile.log" 2>&1
    printf -v cpu_hex '%016X' "$cpus"
    printf -v fertig_hex '%016X' "$((cpus*2))"
    grep -aq "MOO-SIMD CPU-ANZAHL $cpu_hex" "$lauf/serial.log"
    grep -aq "MOO-SIMD AVX 000000000000000$avx" "$lauf/serial.log"
    grep -aq "MOO-SIMD GFNI 000000000000000$gfni" "$lauf/serial.log"
    grep -aq "MOO-SIMD FEHLER 0000000000000000" "$lauf/serial.log"
    grep -aq "MOO-SIMD GF-VERGLEICH 0000000000000001" "$lauf/serial.log"
    grep -aq "MOO-SIMD FERTIG $fertig_hex" "$lauf/serial.log"
    echo "PASS SIMD $cpus CPU(s), $profile: lebende Register über Timer-Präemption und GF bytegleich"
  done
done

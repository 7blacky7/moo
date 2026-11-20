#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
out=${MOO_P11_DISK_OUT:-$root/build/p11/datensicht}
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/datensicht.moos -o "$out/datensicht"
"$out/datensicht" >"$out/ausgabe"
diff -u compiler/tests/p11/datensicht.expected "$out/ausgabe"
echo "PASS CBW/CDB bytegleich, alle Endian-Feldtypen, ohne unsicher, unaligned, Grenze und u64-Ueberlauf"
for target in x86_64-bare x86_64-unknown-uefi aarch64-unknown-linux-gnu; do
  "$compiler" compile compiler/tests/p11/datensicht_probe.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
  mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
done
declare -A erwartet=(
  [flucht]="Rueckgabetyp puffer[T] ist nicht freigegeben"
  [zeiger]="keine Zeiger, bool"
  [bool]="keine Zeiger, bool"
  [typ]="erwartet u64, erhalten i64"
  [array]="Endian-Feldtypen gehören an benannte Strukturfelder"
  [liest]="@liest"
)
for fall in flucht zeiger bool typ array liest; do
  if "$compiler" compile "compiler/tests/p11/datensicht_$fall.moos" --emit-ir -o "$out/$fall.ll" >"$out/$fall.log" 2>&1; then
    echo "FAIL ungueltige Sicht akzeptiert: $fall" >&2
    exit 1
  fi
  grep -F "${erwartet[$fall]}" "$out/$fall.log"
  echo "PASS negativer Vertrag $fall"
done
# Vorhandene Byteoperationen nach der gemeinsamen Kernextraktion.
"$compiler" compile compiler/tests/regression/p2_byteansicht_grenzen.moos -o "$out/bytegrenzen"
"$out/bytegrenzen" >"$out/bytegrenzen.out"
diff -u compiler/tests/regression/p2_byteansicht_grenzen.expected "$out/bytegrenzen.out"
echo "PASS vorhandene Byteansicht-Grenzen"

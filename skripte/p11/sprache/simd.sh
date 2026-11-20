#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
compiler=${MOO_COMPILER:-$root/build/p2-t123/target/release/moo-compiler}
out=${MOO_P11_SIMD_OUT:-$root/build/p11/simd}
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/simd.moos -o "$out/probe"
"$out/probe" >"$out/ausgabe"
diff -u compiler/tests/p11/simd.expected "$out/ausgabe"
echo "PASS Integer-SIMD: Wrapping, Index, Shuffle, GF(0x11D), Ergebnisweitergabe und Cleanup"
for target in x86_64-unknown-linux-gnu x86_64-bare x86_64-unknown-uefi; do
  "$compiler" compile compiler/tests/p11/simd_probe.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
  mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
done
grep -E '^define .*@__moo_native.simd_ergebnis_weiter\(ptr .*sret' "$out/x86_64-bare.ll" >/dev/null
grep -E 'call void @__moo_native.__simd.skalar.simd_ergebnis_weiter\(ptr .*sret' "$out/x86_64-bare.ll" >/dev/null
echo "PASS identische sret-Speicher-ABI am Wrapper und an der ISA-Aufrufstelle"

# Unabhängiges C++-Orakel ruft echte exportierte Moo-Funktionen auf.
"$compiler" compile compiler/tests/p11/simd_orakel.moos --no-stdlib --target x86_64-unknown-linux-gnu --emit-ir -o "$out/orakel.ll"
mise exec -- opt -passes='default<O2>' -S "$out/orakel.ll" -o "$out/orakel.opt.ll"
# --no-stdlib enthält einen leeren Programmeinstieg; das Orakel liefert main.
mise exec -- llvm-extract --delete --func=main "$out/orakel.opt.ll" -o "$out/orakel.bc"
mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/orakel.bc" -o "$out/orakel.o"
mise exec -- clang++ -std=c++17 -O2 compiler/tests/p11/simd_orakel.cpp "$out/orakel.o" -o "$out/orakel"
"$out/orakel"
mise exec -- llc -O2 -filetype=asm "$out/x86_64-bare.ll" -o "$out/bare.s"

declare -A diagnose=(
  [mischtyp]="erwartet u8x16, erhalten u8x32"
  [index]="M-M001"
  [gf64]="simd_gf256_mal erwartet u8x16 oder u8x32"
  [floats]="GPR-/Speicher-ABI"
  [lokalfloat]="@simd ist ein Ganzzahlbereich"
  [ohne_sicherung]="benötigt @simd zur Zustandssicherung"
)
for fall in mischtyp index gf64 floats lokalfloat ohne_sicherung; do
  if "$compiler" compile "compiler/tests/p11/simd_$fall.moos" --no-stdlib --target x86_64-bare --emit-ir -o "$out/$fall.ll" >"$out/$fall.log" 2>&1; then
    echo "FAIL SIMD-Vertrag akzeptiert: $fall" >&2
    exit 1
  fi
  grep -F "${diagnose[$fall]}" "$out/$fall.log"
  echo "PASS negativer SIMD-Vertrag $fall"
done
"$compiler" compile compiler/tests/p11/simd_schatten.moos -o "$out/schatten"
"$out/schatten" >"$out/schatten.ausgabe"
diff -u compiler/tests/p11/simd_schatten.expected "$out/schatten.ausgabe"
echo "PASS eigene Typen und Funktionen behalten Vorrang"
mise exec -- llc -O2 -filetype=asm "$out/orakel.opt.ll" -o "$out/orakel.s"
awk '/^__moo_native.__simd.skalar.simd_orakel_32_auto:/{aktiv=1; gefunden=1} aktiv && /%[xyz]mm/{fehler++} aktiv && /^.Lfunc_end/{aktiv=0} END{print "SKALAR gefunden=" gefunden " SIMD-Register=" fehler+0; exit (!gefunden || fehler)}' "$out/orakel.s"
grep -q 'pshufb' "$out/orakel.s"
grep -q 'vpshufb' "$out/orakel.s"
grep -q 'gf2p8mulb' "$out/orakel.s"
grep -q 'gf2p8affineqb' "$out/orakel.s"
awk '/^__moo_native.__simd.gfni128.simd_orakel_32_gfni128:/{aktiv=1; gefunden=1} aktiv && /%[yz]mm/{fehler++} aktiv && /gf2p8mulb/{gfni++} aktiv && /^.Lfunc_end/{aktiv=0} END{print "GFNI128 gefunden=" gefunden " YMM-ZMM=" fehler+0 " GFNI-MUL=" gfni+0; exit (!gefunden || fehler || !gfni)}' "$out/orakel.s"
echo "PASS Skalar-ASM ohne SIMD-Register; PSHUFB und GFNI in separaten ISA-Varianten"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out="$PWD/build/p11/array-q/t26/faktor"
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/simd_faktor.moos --no-stdlib --target x86_64-unknown-linux-gnu --emit-ir -o "$out/host.ll"
mise exec -- opt -passes='default<O2>,verify' -S "$out/host.ll" -o "$out/host.opt.ll"
mise exec -- llvm-extract --delete --func=main "$out/host.opt.ll" -o "$out/host.bc"
mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/host.bc" -o "$out/host.o"
mise exec -- clang++ -std=c++17 -O2 -Wall -Wextra -Werror compiler/tests/p11/simd_faktor.cpp "$out/host.o" -o "$out/moo-p11-faktor"
"$out/moo-p11-faktor"
mise exec -- llc -O2 -filetype=asm "$out/host.bc" -o "$out/host.s"
awk '/^__moo_native.__simd.skalar.faktor_32_skalar:/{aktiv=1; gefunden=1} aktiv && /%[xyz]mm/{fehler++} aktiv && /^.Lfunc_end/{aktiv=0} END{exit (!gefunden || fehler)}' "$out/host.s"
awk '/^__moo_native.__simd.gfni128.faktor_32_gfni128:/{aktiv=1; gefunden=1} aktiv && /%[yz]mm/{fehler++} aktiv && /gf2p8affineqb/{gfni++} aktiv && /^.Lfunc_end/{aktiv=0} END{exit (!gefunden || fehler || !gfni)}' "$out/host.s"
awk '/^__moo_native.__simd.avx2.faktor_32_avx2:/{aktiv=1; gefunden=1} aktiv && /pshufb/{shuffle++} aktiv && /gf2p8/{fehler++} aktiv && /^.Lfunc_end/{aktiv=0} END{exit (!gefunden || fehler || !shuffle)}' "$out/host.s"
for target in x86_64-bare x86_64-unknown-uefi; do
  "$compiler" compile compiler/tests/p11/simd_faktor.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
  mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
done
for fall in typ vektor ohne; do
  for target in x86_64-unknown-linux-gnu x86_64-bare; do
    if [ "$fall" = ohne ] && [ "$target" = x86_64-unknown-linux-gnu ]; then
      "$compiler" compile compiler/tests/p11/simd_faktor_ohne.moos --no-stdlib --target "$target" --emit-ir -o "$out/host-skalar.ll"
      mise exec -- opt -passes=verify -disable-output "$out/host-skalar.ll"
      continue
    fi
    if "$compiler" compile "compiler/tests/p11/simd_faktor_$fall.moos" --no-stdlib --target "$target" --emit-ir -o "$out/neg.ll" > "$out/$fall-$target.log" 2>&1; then
      echo "FAIL GF factor accepted: $fall $target" >&2; exit 1
    fi
    rg -q 'M-C000' "$out/$fall-$target.log"
  done
done
echo 'GF factor: all pairs, profile ceilings, 5 negative, hosted scalar and 3 target IR gates OK'

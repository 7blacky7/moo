#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out="$PWD/build/p11/array-q/t26/simd-puffer"
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/simd_puffer.moos --no-stdlib --target x86_64-unknown-linux-gnu --emit-ir -o "$out/host.ll"
mise exec -- opt -passes='default<O2>,verify' -S "$out/host.ll" -o "$out/host.opt.ll"
mise exec -- llvm-extract --delete --func=main "$out/host.opt.ll" -o "$out/host.bc"
mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/host.bc" -o "$out/host.o"
gcc -std=c11 -O2 -Wall -Wextra -Werror compiler/tests/p11/simd_puffer.c "$out/host.o" -o "$out/moo-p11-simd-puffer"
"$out/moo-p11-simd-puffer"
mise exec -- llc -O2 -filetype=asm "$out/host.bc" -o "$out/host.s"
awk '/^__moo_native.__simd.skalar.simd_puffer_32_skalar:/{aktiv=1; gefunden=1} aktiv && /%[xyz]mm/{fehler++} aktiv && /^.Lfunc_end/{aktiv=0} END{exit (!gefunden || fehler)}' "$out/host.s"
awk '/^__moo_native.__simd.gfni128.simd_puffer_32_gfni128:/{aktiv=1; gefunden=1} aktiv && /%[yz]mm/{fehler++} aktiv && /^.Lfunc_end/{aktiv=0} END{exit (!gefunden || fehler)}' "$out/host.s"
for target in x86_64-bare x86_64-unknown-uefi; do
  "$compiler" compile compiler/tests/p11/simd_puffer.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
  mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
done
for fall in typ versatz liest schatten_typ; do
  for target in x86_64-unknown-linux-gnu x86_64-bare; do
    if "$compiler" compile "compiler/tests/p11/simd_puffer_$fall.moos" --no-stdlib --target "$target" --emit-ir -o "$out/neg.ll" > "$out/$fall-$target.log" 2>&1; then
      echo "FAIL SIMD buffer accepted: $fall $target" >&2; exit 1
    fi
    rg -q 'M-C000' "$out/$fall-$target.log"
  done
done
"$compiler" compile compiler/tests/p11/simd_puffer_host.moos -o "$out/cleanup"
"$out/cleanup" > "$out/cleanup.txt"
diff -u compiler/tests/p11/simd_puffer.expected "$out/cleanup.txt"
"$compiler" compile compiler/tests/p11/simd_puffer_schatten.moos -o "$out/schatten"
test "$("$out/schatten")" = "$(printf '6\n15')"
echo 'SIMD buffer: guardpages/aliases/bounds, 8 negatives, cleanup/reads/shadow and 3 target IR gates OK'

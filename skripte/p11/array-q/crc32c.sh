#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
export CARGO_TARGET_DIR=/home/blacky/dev/moo/build/p2-t123/target
compiler="${MOO_COMPILER:-$CARGO_TARGET_DIR/release/moo-compiler}"
out="$PWD/build/p11/array-q/t26/crc"
mkdir -p "$out"
"$compiler" compile compiler/tests/p11/crc32c_gate.moos --no-stdlib --target x86_64-unknown-linux-gnu --emit-ir -o "$out/host.ll"
for mode in auto fallback; do
  if [ "$mode" = fallback ]; then
    test "$(rg -c 'br i1 %available, label %fast, label %fallback' "$out/host.ll")" = 1
    sed 's/br i1 %available, label %fast, label %fallback/br i1 false, label %fast, label %fallback/' "$out/host.ll" > "$out/$mode.ll"
  else
    cp "$out/host.ll" "$out/$mode.ll"
  fi
  mise exec -- opt -passes='default<O2>,verify' -S "$out/$mode.ll" -o "$out/$mode.opt.ll"
  mise exec -- llvm-extract --delete --func=main "$out/$mode.opt.ll" -o "$out/$mode.bc"
  mise exec -- llc -O2 -relocation-model=pic -filetype=obj "$out/$mode.bc" -o "$out/$mode.o"
  gcc -std=c11 -O2 -Wall -Wextra -Werror compiler/tests/p11/crc32c_gate.c "$out/$mode.o" -o "$out/$mode"
  "$out/$mode"
done
mise exec -- llc -O2 -filetype=asm "$out/auto.bc" -o "$out/auto.s"
rg 'crc32q' "$out/auto.s" > "$out/crc32-instructions.txt"
if rg -i '(%[xyz]mm|xsave|xrstor)' "$out/auto.s"; then
  echo 'CRC gate: SIMD/XSTATE unexpectedly used' >&2; exit 1
fi
for target in x86_64-bare x86_64-unknown-uefi; do
  "$compiler" compile compiler/tests/p11/crc32c_gate.moos --no-stdlib --target "$target" --emit-ir -o "$out/$target.ll"
  mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
done
"$compiler" compile compiler/tests/p11/crc32c_kern.moos --no-stdlib --target aarch64-bare --emit-ir -o "$out/aarch64-bare.ll"
mise exec -- opt -passes=verify -disable-output "$out/aarch64-bare.ll"
for fall in typ zustand anzahl versatz; do
  for target in x86_64-unknown-linux-gnu x86_64-bare; do
    if "$compiler" compile "compiler/tests/p11/crc32c_$fall.moos" --no-stdlib --target "$target" --emit-ir -o "$out/neg.ll" > "$out/$fall-$target.log" 2>&1; then
      echo "FAIL CRC type accepted: $fall $target" >&2; exit 1
    fi
    rg -q 'M-C000' "$out/$fall-$target.log"
    rg -q 'CRC32C|crc32c_aktualisieren' "$out/$fall-$target.log"
  done
done
"$compiler" compile compiler/tests/p11/crc32c_schatten.moos -o "$out/schatten"
test "$("$out/schatten")" = 21
echo 'CRC32C auto/fallback, inbounds, shadowing, 8 negatives and 4 target IR gates OK'

#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-cross-float
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-pc-windows-msvc x86_64-pc-windows-gnu aarch64-unknown-linux-gnu; do
    timeout 120s "$compiler" compile compiler/tests/p2/ffi_libm_float.moos --target "$target" --emit-ir -o "$out/$target-moo.ll"
    mise exec -- clang --target="$target" -ffreestanding -O0 -S -emit-llvm skripte/p2/abi/float_cross_reference.c -o "$out/$target-c.ll"
    for ir in "$out/$target-moo.ll" "$out/$target-c.ll"; do
        mise exec -- opt -passes=verify -disable-output "$ir"
        grep -Eq 'call float @sinf\(float ' "$ir"
        grep -Eq 'call float @powf\(float ' "$ir"
        grep -Eq 'call double @pow\(double ' "$ir"
    done
    mise exec -- llc -filetype=obj "$out/$target-moo.ll" -o "$out/$target.o"
    mise exec -- llvm-nm "$out/$target.o" > "$out/$target.nm"
    for symbol in sinf powf pow; do grep -Eq " U ${symbol}$" "$out/$target.nm"; done
done
printf 'Float FFI Windows/AArch64 IR/Objekte PASS (kein Ziellauf)\n'

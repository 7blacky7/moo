#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-cross-mixed
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-pc-windows-msvc x86_64-pc-windows-gnu aarch64-unknown-linux-gnu; do
    timeout 120s "$compiler" compile compiler/tests/p2/ffi_mixed.moos --target "$target" --emit-ir -o "$out/$target-moo.ll"
    mise exec -- clang --target="$target" -ffreestanding -O0 -S -emit-llvm compiler/tests/p2/ffi_mixed.c -o "$out/$target-c.ll"
    for ir in "$out/$target-moo.ll" "$out/$target-c.ll"; do
        mise exec -- opt -passes=verify -disable-output "$ir"
    done
    for symbol in value f32 i32 i8 u8; do
        grep -Eq "call .* @p2_mixed_${symbol}\\(" "$out/$target-moo.ll"
        grep -Eq "^define .* @p2_mixed_${symbol}\\(" "$out/$target-c.ll"
    done
    mise exec -- llc -filetype=obj "$out/$target-moo.ll" -o "$out/$target.o"
    mise exec -- llvm-nm "$out/$target.o" > "$out/$target.nm"
    for symbol in value f32 i32 i8 u8; do grep -Eq " U p2_mixed_${symbol}$" "$out/$target.nm"; done
done
printf 'Gemischte FFI-Rueckgaben Windows/AArch64 Cross: PASS (kein Ziellauf)\n'

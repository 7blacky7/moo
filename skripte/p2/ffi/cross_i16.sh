#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-cross-i16
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-pc-windows-msvc x86_64-pc-windows-gnu aarch64-unknown-linux-gnu; do
    timeout 120s "$compiler" compile compiler/tests/p2/ffi_i16_roundtrip.moos --target "$target" --emit-ir -o "$out/$target-moo.ll"
    mise exec -- clang --target="$target" -ffreestanding -O0 -S -emit-llvm compiler/tests/p2/ffi_klein_roundtrip.c -o "$out/$target-c.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target-moo.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target-c.ll"
    for symbol in i16 u16; do
        grep -Eq "call i16 @p2_ffi_${symbol}_identity\\(i16 " "$out/$target-moo.ll"
        grep -Eq "^define .* i16 @p2_ffi_${symbol}_identity\\(i16 " "$out/$target-c.ll"
    done
    mise exec -- llc -filetype=obj "$out/$target-moo.ll" -o "$out/$target.o"
    mise exec -- llvm-nm "$out/$target.o" > "$out/$target.nm"
    for symbol in i16 u16; do grep -Eq " U p2_ffi_${symbol}_identity$" "$out/$target.nm"; done
done
printf 'i16/u16 FFI Windows/AArch64 IR/Objekte PASS (kein Ziellauf)\n'

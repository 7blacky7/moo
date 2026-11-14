#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-cross-i64
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-pc-windows-msvc x86_64-pc-windows-gnu aarch64-unknown-linux-gnu; do
    timeout 120s "$compiler" compile compiler/tests/p2/ffi_i64_roundtrip.moos --target "$target" --emit-ir -o "$out/$target-moo.ll"
    mise exec -- clang --target="$target" -ffreestanding -O0 -S -emit-llvm compiler/tests/p2/ffi_i64.c -o "$out/$target-c.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target-moo.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target-c.ll"
    for symbol in i64_identity u64_identity; do
        grep -Eq "call i64 @p2_${symbol}\\(i64 " "$out/$target-moo.ll"
        grep -Eq "^define .* i64 @p2_${symbol}\\(i64 " "$out/$target-c.ll"
    done
    grep -Eq 'call i32 @p2_u64_is_exact_literal\(i64 9007199254740993\)' "$out/$target-moo.ll"
    grep -Eq 'call i32 @p2_u64_is_max\(i64 -1\)' "$out/$target-moo.ll"
    mise exec -- llc -filetype=obj "$out/$target-moo.ll" -o "$out/$target.o"
    mise exec -- llvm-nm "$out/$target.o" > "$out/$target.nm"
    for symbol in i64_identity u64_identity; do grep -Eq " U p2_${symbol}$" "$out/$target.nm"; done
done
printf 'i64/u64 Windows/AArch64 IR/Objekte und exakte Literale: PASS (kein Ziellauf)\n'

#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-cross-u32
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-pc-windows-msvc x86_64-pc-windows-gnu aarch64-unknown-linux-gnu; do
    timeout 120s "$compiler" compile compiler/tests/p2/ffi_u32_roundtrip.moos --target "$target" --emit-ir -o "$out/$target-moo.ll"
    mise exec -- clang --target="$target" -ffreestanding -O0 -S -emit-llvm compiler/tests/p2/ffi_u32.c -o "$out/$target-c.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target-moo.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target-c.ll"
    grep -Eq 'call i32 @p2_u32_identity\(i32 ' "$out/$target-moo.ll"
    grep -Eq '^define .* i32 @p2_u32_identity\(i32 ' "$out/$target-c.ll"
    grep -Eq 'call i32 @p2_u32_mixed\(i32 .*float ' "$out/$target-moo.ll"
    grep -Eq '^define .* i32 @p2_u32_mixed\(i32 .*float ' "$out/$target-c.ll"
    grep -Eq 'call void @p2_u32_record\(i32 ' "$out/$target-moo.ll"
    grep -Eq '^define .*void @p2_u32_record\(i32 ' "$out/$target-c.ll"
    mise exec -- llc -filetype=obj "$out/$target-moo.ll" -o "$out/$target.o"
    mise exec -- llvm-nm "$out/$target.o" > "$out/$target.nm"
    grep -Eq ' U p2_u32_identity$' "$out/$target.nm"
    grep -Eq ' U p2_u32_mixed$' "$out/$target.nm"
    grep -Eq ' U p2_u32_record$' "$out/$target.nm"
done
printf 'u32 FFI Windows/AArch64 IR/Objekte PASS (kein Ziellauf)\n'

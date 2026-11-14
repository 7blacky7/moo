#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-cross-void
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-pc-windows-msvc x86_64-pc-windows-gnu aarch64-unknown-linux-gnu; do
    timeout 120s "$compiler" compile compiler/tests/p2/ffi_void.moos --target "$target" --emit-ir -o "$out/$target-moo.ll"
    mise exec -- clang --target="$target" -ffreestanding -O0 -S -emit-llvm skripte/p2/abi/void_cross_reference.c -o "$out/$target-c.ll"
    for ir in "$out/$target-moo.ll" "$out/$target-c.ll"; do
        mise exec -- opt -passes=verify -disable-output "$ir"
        grep -Eq 'call void @p2_void_mixed\(i32 .*float .*double .*i8 .*i8 ' "$ir"
    done
    mise exec -- llc -filetype=obj "$out/$target-moo.ll" -o "$out/$target.o"
    mise exec -- llvm-nm "$out/$target.o" > "$out/$target.nm"
    grep -Eq ' U p2_void_mixed$' "$out/$target.nm"
done
printf 'Void-Mischsignatur Windows/AArch64 IR/Objekte PASS (kein Ziellauf)\n'

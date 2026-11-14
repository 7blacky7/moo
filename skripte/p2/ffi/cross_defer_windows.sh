#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-cross-defer
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for target in x86_64-pc-windows-msvc x86_64-pc-windows-gnu; do
    timeout 120s "$compiler" compile compiler/tests/p2/defer_vertrag_fehler.moos --target "$target" --emit-ir -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
    mise exec -- llc -filetype=obj "$out/$target.ll" -o "$out/$target.obj"
    mise exec -- llvm-nm "$out/$target.obj" > "$out/$target.nm"
    grep -Eq ' U moo_try_take_error$' "$out/$target.nm"
    grep -Eq ' U moo_throw$' "$out/$target.nm"
    grep -Eq 'call void @moo_try_take_error\(ptr ' "$out/$target.ll"
    grep -Eq 'call void @moo_throw\(ptr ' "$out/$target.ll"
    mise exec -- clang --target="$target" -ffreestanding -O0 -S -emit-llvm skripte/p2/abi/defer_windows_abi.c -o "$out/$target-clang.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target-clang.ll"
    grep -Eq 'call void @moo_try_take_error\(ptr ' "$out/$target-clang.ll"
    grep -Eq 'call void @moo_throw\(ptr ' "$out/$target-clang.ll"
done
printf 'Windows x64 Defer/Error IR und COFF: PASS (kein Windows-Lauf)\n'

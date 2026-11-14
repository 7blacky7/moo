#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi/indirect-reference
mkdir -p "$out"
clang -std=c11 -Wall -Wextra -Werror -O0 -c skripte/p2/abi/indirect_reference.c -o "$out/callee.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -O0 skripte/p2/abi/indirect_reference_test.cpp "$out/callee.o" -o "$out/reference"
"$out/reference"
for target in x86_64-unknown-none x86_64-unknown-uefi aarch64-unknown-none; do
    clang --target="$target" -ffreestanding -std=c11 -O0 -S -emit-llvm skripte/p2/abi/indirect_reference.c -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
    mise exec -- llc -filetype=obj "$out/$target.ll" -o "$out/$target.o"
    if [[ "$target" == x86_64-unknown-none ]]; then
        grep -Eq 'call signext i8 %[^ (]+\(i8 [^)]*signext ' "$out/$target.ll"
        grep -Eq 'call zeroext i8 %[^ (]+\(i8 [^)]*zeroext ' "$out/$target.ll"
    else
        if grep -Eq 'call (signext|zeroext) i8 %' "$out/$target.ll"; then
            echo "Unerwartetes Narrow-Attribut für $target" >&2; exit 1
        fi
        [[ $(grep -Ec 'call i8 %[^ (]+\(i8 ' "$out/$target.ll") -eq 2 ]]
    fi
    grep -Eq 'call i64 %[^ (]+\(ptr .*ptr ' "$out/$target.ll"
    grep -Eq 'call void %[^ (]+\(i32 ' "$out/$target.ll"
    grep -Eq 'call i64 %[^ (]+\(ptr [^,]+, ptr [^,]+, i32 [^,]+, i64 .*' "$out/$target.ll"
done
printf 'C-Callsite-Referenz LLVM verify/Objekte PASS (kein Moo-Aufruf)\n'

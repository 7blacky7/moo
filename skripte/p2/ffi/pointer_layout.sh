#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi/pointer-layout
mkdir -p "$out"
clang -std=c11 -Wall -Wextra -Werror -O0 -c skripte/p2/abi/pointer_layout.c -o "$out/layout.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -Werror skripte/p2/abi/pointer_layout_test.cpp "$out/layout.o" -o "$out/reference"
"$out/reference"
for target in x86_64-unknown-none x86_64-unknown-uefi aarch64-unknown-none; do
    clang --target="$target" -ffreestanding -std=c11 -O0 -S -emit-llvm skripte/p2/abi/pointer_layout.c -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
    mise exec -- llc -filetype=obj "$out/$target.ll" -o "$out/$target.o"
    mise exec -- llvm-nm "$out/$target.o" > "$out/$target.nm"
    for symbol in size align status daten ausgabe; do
        grep -Eq " T p2_pointer_layout_${symbol}$" "$out/$target.nm"
    done
done
printf 'C pointer field target IR/objects PASS; kein Moo-Layoutvergleich\n'

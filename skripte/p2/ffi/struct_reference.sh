#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi/struct-reference
mkdir -p "$out"
clang -std=c11 -Wall -Wextra -Werror -O0 -c skripte/p2/abi/struct_by_value.c -o "$out/callee.o"
clang -std=c11 -Wall -Wextra -Werror -O0 -c skripte/p2/abi/struct_by_value_test.c -o "$out/caller.o"
clang "$out/callee.o" "$out/caller.o" -o "$out/reference"
"$out/reference"
clang -std=c11 -O0 -S -emit-llvm skripte/p2/abi/struct_by_value.c -o "$out/callee.ll"
clang -std=c11 -O0 -S -emit-llvm skripte/p2/abi/struct_by_value_test.c -o "$out/caller.ll"
printf 'C-Referenz-IR: %s/{callee,caller}.ll\n' "$out"
for target in x86_64-pc-windows-msvc x86_64-pc-windows-gnu aarch64-unknown-linux-gnu; do
    mise exec -- clang --target="$target" -ffreestanding -std=c11 -O0 -S -emit-llvm skripte/p2/abi/struct_by_value.c -o "$out/$target.ll"
    mise exec -- opt -passes=verify -disable-output "$out/$target.ll"
    mise exec -- llc -filetype=obj "$out/$target.ll" -o "$out/$target.o"
    mise exec -- llvm-nm "$out/$target.o" > "$out/$target.nm"
    for symbol in moo_p2_struct1 moo_p2_struct2 moo_p2_struct3 moo_p2_struct4 moo_p2_struct6 moo_p2_struct_mixed moo_p2_float4 moo_p2_packed moo_p2_pressure; do
        grep -Eq " T ${symbol}$" "$out/$target.nm"
    done
done
printf 'Windows-x64/AArch64 C-Referenz IR/Objekte PASS (kein Lauf)\n'

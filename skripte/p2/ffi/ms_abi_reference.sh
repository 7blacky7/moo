#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
[[ $(uname -m) == x86_64 ]] || { echo 'Referenz benötigt x86_64' >&2; exit 1; }
out=build/p2-ffi/ms-abi-reference
mkdir -p "$out"
for opt in 0 2; do
    clang -std=c11 -Wall -Wextra -Werror -O"$opt" -c skripte/p2/abi/ms_abi_reference.c -o "$out/caller-O$opt.o"
    ${CXX:-c++} -std=c++17 -Wall -Wextra -Werror -O"$opt" skripte/p2/abi/ms_abi_reference_test.cpp "$out/caller-O$opt.o" -o "$out/reference-O$opt"
    "$out/reference-O$opt"
done
clang -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -c skripte/p2/abi/ms_abi_reference.c -o "$out/caller-sanitize.o"
clang++ -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer skripte/p2/abi/ms_abi_reference_test.cpp "$out/caller-sanitize.o" -o "$out/reference-sanitize"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$out/reference-sanitize"
clang -std=c11 -O0 -S -emit-llvm skripte/p2/abi/ms_abi_reference.c -o "$out/caller.ll"
mise exec -- opt -passes=verify -disable-output "$out/caller.ll"
grep -Eq 'call win64cc i64 %[^ (]+\(ptr [^,]+, ptr [^,]+, i32 [^,]+, i64 .*' "$out/caller.ll"
printf 'Linux C/C++ ms_abi reference and LLVM callsite PASS; kein Moo-/Windows-/Firmwarelauf\n'

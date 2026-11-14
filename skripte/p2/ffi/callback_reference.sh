#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi/callback-reference
mkdir -p "$out"
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -c skripte/p2/abi/callback_reference.c -o "$out/callee.o"
clang -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -c skripte/p2/abi/callback_reference_test.c -o "$out/caller.o"
clang -fsanitize=address,undefined "$out/callee.o" "$out/caller.o" -o "$out/reference"
"$out/reference"

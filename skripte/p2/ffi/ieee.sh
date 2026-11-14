#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-ieee
mkdir -p "$out"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_float_ieee.c -o "$out/ieee.o"
ar rcs "$out/libp2_ieee.a" "$out/ieee.o"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_float_ieee.moos --lib-pfad "$out" -o "$out/probe"
result=$(timeout 30s "$out/probe")
[[ "$result" == $'1\n2\n3\n4\n5\n5\n7' ]] || { printf 'IEEE-Ausgabe unerwartet: %s\n' "$result" >&2; exit 1; }
printf 'Float-FFI IEEE-Randwerte: PASS\n'

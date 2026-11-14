#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-mixed
mkdir -p "$out"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_mixed.c -o "$out/mixed.o"
ar rcs "$out/libp2_mixed.a" "$out/mixed.o"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_mixed.moos --lib-pfad "$out" -o "$out/probe"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_mixed.moos --emit-ir -o "$out/probe.ll"
result=$(timeout 30s "$out/probe")
[[ "$result" == $'19\n19\n19\n-128\n255' ]] || { printf 'Gemischte Ausgabe unerwartet: %s\n' "$result" >&2; exit 1; }
grep -Eq 'call double @p2_mixed_value\(i32 .*float .*double .*i8 .*i8 ' "$out/probe.ll"
printf 'C-FFI gemischte Werte und f64-Rueckgabe: PASS\n'

#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-void
mkdir -p "$out"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_void.c -o "$out/void.o"
ar rcs "$out/libp2_void.a" "$out/void.o"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_void.moos --lib-pfad "$out" -o "$out/probe"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_void.moos --emit-ir -o "$out/probe.ll"
result=$(timeout 30s "$out/probe")
[[ "$result" == $'42\n19' ]] || { printf 'void-Ausgabe unerwartet: %s\n' "$result" >&2; exit 1; }
grep -Eq 'call void @p2_void_record\(i32 ' "$out/probe.ll"
grep -Eq 'call void @p2_void_mixed\(i32 ' "$out/probe.ll"
if timeout 120s "$compiler" compile compiler/tests/p2/ffi_void_mismatch.moos --emit-ir -o "$out/mismatch.ll" >"$out/mismatch.log" 2>&1; then
    printf 'C-Rückgabetypkonflikt unerwartet akzeptiert\n' >&2
    exit 1
fi
grep -Fq 'Argumentzahl oder Rueckgabetyp unpassend' "$out/mismatch.log"
printf 'C void-Aufruf und Rueckgabetyp-Negativfall: PASS\n'

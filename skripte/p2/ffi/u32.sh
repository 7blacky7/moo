#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-u32
mkdir -p "$out"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_u32.c -o "$out/ref.o"
ar rcs "$out/libp2_u32.a" "$out/ref.o"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_u32_roundtrip.moos --lib-pfad "$out" -o "$out/probe"
result=$(timeout 30s "$out/probe")
[[ "$result" == $'null\nmitte\nmax\nliteral\nmixed\nvoid' ]] || { printf 'u32-Ausgabe unerwartet: %s\n' "$result" >&2; exit 1; }
for name in high neg fraction nan text; do
    timeout 120s "$compiler" compile "compiler/tests/p2/ffi_u32_${name}.moos" --lib-pfad "$out" -o "$out/$name"
    if timeout 30s "$out/$name" >"$out/$name.log" 2>&1; then printf 'u32-Grenzfehler fehlt: %s\n' "$name" >&2; exit 1; fi
    grep -Fq 'Native Funktion erwartet eine u32-Ganzzahl' "$out/$name.log"
    if grep -Eq 'C_GUARD_AUFGERUFEN|DARF_NICHT_ERREICHT_WERDEN' "$out/$name.log"; then exit 1; fi
done
printf 'u32 C-Roundtrip und Grenzfehler: PASS\n'

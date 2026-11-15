#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-i16
mkdir -p "$out"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_klein_roundtrip.c -o "$out/ref.o"
ar rcs "$out/libp2_ffi_klein.a" "$out/ref.o"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_i16_roundtrip.moos --lib-pfad "$out" -o "$out/probe"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_i16_roundtrip.moos --emit-ir -o "$out/probe.ll"
result=$(timeout 30s "$out/probe")
[[ "$result" == $'-32768\n32767\n0\n65535' ]] || { printf 'i16/u16 Ausgabe unerwartet: %s\n' "$result" >&2; exit 1; }
grep -Eq 'call signext i16 @p2_ffi_i16_identity\(i16 signext ' "$out/probe.ll"
grep -Eq 'call zeroext i16 @p2_ffi_u16_identity\(i16 zeroext ' "$out/probe.ll"
for name in i16 u16; do
    timeout 120s "$compiler" compile "compiler/tests/p2/ffi_${name}_out.moos" --lib-pfad "$out" -o "$out/$name"
    # P2-T95: Abbruch nur mit MOO_STRENG=1; Standard laeuft mit Hinweis weiter.
    if MOO_STRENG=1 timeout 30s "$out/$name" >"$out/$name.log" 2>&1; then printf 'Bereichsfehler fehlt: %s\n' "$name" >&2; exit 1; fi
    grep -Fq "Native Funktion erwartet eine ${name}-Ganzzahl" "$out/$name.log"
    if grep -Fq DARF_NICHT_ERREICHT_WERDEN "$out/$name.log"; then exit 1; fi
done
printf 'i16/u16 C-Roundtrip und Bereichsfehler: PASS\n'

#!/usr/bin/env bash
set -euo pipefail
# macOS stellt GNU timeout gegebenenfalls als gtimeout bereit; ohne beides
# bleiben die begrenzten Roundtrip-Faelle durch das CI-Joblimit abgesichert.
mit_zeitlimit() {
    local dauer="$1"; shift
    if command -v timeout >/dev/null 2>&1; then timeout "$dauer" "$@"
    elif command -v gtimeout >/dev/null 2>&1; then gtimeout "$dauer" "$@"
    else "$@"
    fi
}
cd "$(dirname "$0")/../../.."
out=build/p2-ffi-klein-roundtrip
mkdir -p "$out"
mit_zeitlimit 30s "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_klein_roundtrip.c -o "$out/ffi_klein.o"
ar rcs "$out/libp2_ffi_klein.a" "$out/ffi_klein.o"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
mit_zeitlimit 120s "$compiler" compile compiler/tests/p2/ffi_klein_roundtrip.moos --lib-pfad "$out" -o "$out/probe"
mit_zeitlimit 120s "$compiler" compile compiler/tests/p2/ffi_klein_roundtrip.moos --emit-ir -o "$out/probe.ll"
result=$(mit_zeitlimit 30s "$out/probe")
[[ "$result" == $'-128\n127\n0\n255\nfalsch\nwahr' ]] || { printf 'Unerwartete C-Ausgabe: %s\n' "$result" >&2; exit 1; }
awk '/call signext i8 @p2_ffi_i8_identity\(i8 signext / {signed=1} /call zeroext i8 @p2_ffi_u8_identity\(i8 zeroext / {unsigned=1} END {if (!signed || !unsigned) exit 1}' "$out/probe.ll"
grep -Eq '^declare signext i16 @p2_ffi_i16_identity\(i16 signext\)' "$out/probe.ll"
grep -Eq '^declare zeroext i16 @p2_ffi_u16_identity\(i16 zeroext\)' "$out/probe.ll"
# P2-T19 Batch C: C _Bool als i1 zeroext (x86_64, Clang-belegt) an Deklaration und Aufruf.
grep -Eq '^declare zeroext i1 @p2_ffi_bool_not\(i1 zeroext\)' "$out/probe.ll"
grep -Eq 'call zeroext i1 @p2_ffi_bool_not\(i1 zeroext ' "$out/probe.ll"
for test in return_mismatch arg_mismatch; do
    if mit_zeitlimit 120s "$compiler" compile "compiler/tests/p2/ffi_klein_${test}.moos" --emit-ir -o "$out/${test}.ll" >"$out/${test}.log" 2>&1; then
        printf 'Typkonflikt unerwartet akzeptiert: %s\n' "$test" >&2
        exit 1
    fi
done
grep -Fq 'Argumentzahl oder Rueckgabetyp unpassend' "$out/return_mismatch.log"
grep -Fq "hat Typ I8, erwartet U8" "$out/arg_mismatch.log"
printf 'i8/u8/bool nativer C-Roundtrip und Typnegative: PASS\n'

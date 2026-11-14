#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-i64
mkdir -p "$out"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_i64.c -o "$out/ref.o"
ar rcs "$out/libp2_i64.a" "$out/ref.o"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_i64_roundtrip.moos --lib-pfad "$out" -o "$out/probe"
result=$(timeout 30s "$out/probe")
[[ "$result" == $'i64-exakt\ni64-min\nu64-hoch\n1\n1\ni64-hoch-exakt\nu64-hoch-exakt' ]] || { printf 'i64/u64 Ausgabe unerwartet: %s\n' "$result" >&2; exit 1; }
for name in i64_input_out u64_input_out i64_inexact u64_inexact i64_nearmin_inexact i64_fraction_input_out u64_fraction_input_out i64_nan_input_out u64_inf_input_out i64_low_input_out u64_high_input_out; do
    timeout 120s "$compiler" compile "compiler/tests/p2/ffi_${name}.moos" --lib-pfad "$out" -o "$out/$name"
    if timeout 30s "$out/$name" >"$out/$name.log" 2>&1; then printf 'Grenzfehler fehlt: %s\n' "$name" >&2; exit 1; fi
    grep -Fq 'Native Funktion' "$out/$name.log"
    if grep -Fq DARF_NICHT_ERREICHT_WERDEN "$out/$name.log"; then exit 1; fi
    case "$name" in
        *_input_out)
            if grep -Fq C_GUARD_AUFGERUFEN "$out/$name.log"; then
                printf 'Ungueltiges Argument erreichte C: %s\n' "$name" >&2; exit 1
            fi ;;
        *_inexact)
            grep -Fq C_RETURN_AUFGERUFEN "$out/$name.log" ;;
    esac
done
for name in result arg loop; do
    if timeout 120s "$compiler" compile "compiler/tests/p2/ffi_i64_defer_${name}.moos" --emit-ir -o "$out/defer_$name.ll" >"$out/defer_$name.log" 2>&1; then
        printf 'Wide-FFI nach aufraeumen unerwartet akzeptiert: %s\n' "$name" >&2
        exit 1
    fi
    grep -Fq 'Native 64-Bit-FFI bei vorhandenem aufräumen ist noch nicht unterstützt' "$out/defer_$name.log"
done
printf 'i64/u64 FFI Exaktheit und Grenzfehler: PASS\n'

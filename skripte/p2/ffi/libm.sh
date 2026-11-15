#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi-libm
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_libm_float.moos -o "$out/probe"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_libm_float.moos --emit-ir -o "$out/probe.ll"
result=$(timeout 30s "$out/probe")
[[ "$result" == $'0\n8\n8' ]] || { printf 'libm-Ausgabe unerwartet: %s\n' "$result" >&2; exit 1; }
grep -Eq 'call float @sinf\(float ' "$out/probe.ll"
grep -Eq 'call float @powf\(float ' "$out/probe.ll"
grep -Eq 'call double @pow\(double ' "$out/probe.ll"
for name in text overflow; do
    timeout 120s "$compiler" compile "compiler/tests/p2/ffi_float_${name}.moos" -o "$out/$name"
    # P2-T95: Abbruch nur mit MOO_STRENG=1; Standard laeuft mit Hinweis weiter.
    if MOO_STRENG=1 timeout 30s "$out/$name" >"$out/$name.out" 2>&1; then
        printf 'Float-Negativtest %s war unerwartet erfolgreich\n' "$name" >&2
        exit 1
    fi
    grep -Fq 'Native Funktion erwartet eine darstellbare Floatzahl' "$out/$name.out"
    if grep -Fq 'DARF_NICHT_ERREICHT_WERDEN' "$out/$name.out"; then
        printf 'Float-Negativtest %s lief nach Fehler weiter\n' "$name" >&2
        exit 1
    fi
done
printf 'libm f32/f64 aus Moo und Float-Negativfälle: PASS\n'

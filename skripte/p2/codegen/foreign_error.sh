#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-foreign-error
mkdir -p "$out"
timeout 30s "${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror -Icompiler/runtime -c compiler/tests/p2/m1_foreign_error.c -o "$out/error.o"
ar rcs "$out/libp2_m1_error.a" "$out/error.o"
timeout 120s compiler/target/release/moo-compiler compile compiler/tests/p2/m1_foreign_error.moos --lib-pfad "$out" -o "$out/probe"
set +e
# P2-T95: Abbruch nur mit MOO_STRENG=1; Standard laeuft mit Hinweis weiter.
MOO_STRENG=1 timeout 30s "$out/probe" > "$out/out.txt" 2> "$out/err.txt"
rc=$?
set -e
if [[ $rc -eq 0 || $rc -eq 124 || $rc -ge 128 ]]; then
    echo "FAIL: unerwarteter Exit $rc" >&2; exit 1
fi
if ! rg -q -F M1_FOREIGN_ERROR "$out/out.txt" "$out/err.txt"; then
    echo "FAIL: Foreign-Fehlerdiagnose fehlt" >&2; exit 1
fi
if rg -q -F NACH_FOREIGN_ERROR "$out/out.txt" "$out/err.txt"; then
    echo "FAIL: Auswertung nach Foreign-Fehler" >&2; exit 1
fi
echo "PASS: Foreign-Fehler beendet verschachtelte Auswertung"

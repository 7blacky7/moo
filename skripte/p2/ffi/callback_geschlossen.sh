#!/usr/bin/env bash
# P2-T21/T57 (Plan f4655d20 A4): fn by value bleibt in extern C abgelehnt
# (Negativfall, Meldung wortgleich); ptr[fn(...) -> R] ist der C-Funktionszeiger
# und wird als opaker ptr deklariert (Positivfall).
set -euo pipefail
out=build/p2-ffi-callback-closed
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
set +e
timeout 30s "$compiler" compile compiler/tests/p2/ffi_callback_funktion.moos --emit-ir -o "$out/funktion.ll" >"$out/funktion.log" 2>&1
rc=$?
set -e
if (( rc == 0 || rc == 124 || rc >= 128 )); then
    printf 'Callback-funktion: unerwarteter Exit %s\n' "$rc" >&2
    exit 1
fi
grep -Fq 'Aggregate und Funktionstypen by value sind nicht freigegeben' "$out/funktion.log"
timeout 30s "$compiler" compile compiler/tests/p2/ffi_callback_zeiger.moos --emit-ir -o "$out/zeiger.ll" >"$out/zeiger.log" 2>&1 \
    || { cat "$out/zeiger.log" >&2; echo 'Callback-zeiger: ptr[fn] abgelehnt' >&2; exit 1; }
grep -Fxq 'declare i32 @p2_callback(ptr)' "$out/zeiger.ll" \
    || { echo 'Callback-zeiger: declare-Zeile fehlt' >&2; exit 1; }
printf 'Callback-Deklaration: fn by value abgelehnt, ptr[fn] als ptr deklariert: PASS\n'

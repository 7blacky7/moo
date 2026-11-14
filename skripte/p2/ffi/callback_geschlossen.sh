#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-callback-closed
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for name in funktion zeiger; do
    set +e
    timeout 30s "$compiler" compile "compiler/tests/p2/ffi_callback_${name}.moos" --emit-ir -o "$out/$name.ll" >"$out/$name.log" 2>&1
    rc=$?
    set -e
    if (( rc == 0 || rc == 124 || rc >= 128 )); then
        printf 'Callback-%s: unerwarteter Exit %s\n' "$name" "$rc" >&2
        exit 1
    fi
    if [[ "$name" == funktion ]]; then
        grep -Fq 'Aggregate und Funktionstypen by value sind nicht freigegeben' "$out/$name.log"
    else
        grep -Fq 'Pointer braucht genau einen unterstuetzten Zieltyp' "$out/$name.log"
    fi
done
printf 'Callback-Deklaration bleibt geschlossen: PASS\n'

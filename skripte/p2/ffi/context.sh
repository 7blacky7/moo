#!/usr/bin/env bash
# P2 M1 / P2-T55: FFI-Kontexte. Seit P2-T55 (Plan f5b52a0b E1) ist die
# FFI-Einzelform hosted fangbar: in versuche: und aus Funktionen aufrufbar
# (Laufzeitnachweis). Direkter C-Aufruf aus reinem Koerper und nicht
# freigegebene Callback-Deklarationen bleiben Compilefehler.
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-m1-ffi
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for case_name in ffi_kontext_try ffi_kontext_funktion; do
    timeout 30s "$compiler" compile "compiler/tests/p2/$case_name.moos" -o "$out/$case_name" > "$out/$case_name.log" 2>&1 \
        || { cat "$out/$case_name.log" >&2; printf '%s: kompiliert nicht\n' "$case_name" >&2; exit 1; }
    actual="$(timeout 30s "$out/$case_name")"
    [[ "$actual" == 42 ]] || { printf '%s: Ausgabe unerwartet: %s\n' "$case_name" "$actual" >&2; exit 1; }
    printf '%s: PASS (Laufzeit)\n' "$case_name"
done
for case_name in ffi_ohne_unsicher ffi_callback_nicht_freigegeben; do
    case "$case_name" in
        ffi_ohne_unsicher) expected="Nicht-reines natives Aufrufziel 'abs'" ;;
        ffi_callback_nicht_freigegeben) expected='Aggregate und Funktionstypen by value sind nicht freigegeben' ;;
    esac
    set +e
    timeout 30s "$compiler" compile "compiler/tests/p2/$case_name.moos" --emit-ir -o "$out/$case_name.ll" > "$out/$case_name.log" 2>&1
    rc=$?
    set -e
    if (( rc == 0 || rc == 124 || rc >= 128 )); then
        printf '%s: unerwarteter Exit %s\n' "$case_name" "$rc" >&2
        exit 1
    fi
    if ! rg -F -q "$expected" "$out/$case_name.log"; then
        printf '%s: erwartete Diagnose fehlt\n' "$case_name" >&2
        exit 1
    fi
    printf '%s: PASS\n' "$case_name"
done

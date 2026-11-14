#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-m1-ffi
mkdir -p "$out"
for case_name in ffi_kontext_try ffi_kontext_funktion ffi_ohne_unsicher ffi_callback_nicht_freigegeben; do
    case "$case_name" in
        ffi_ohne_unsicher) expected="Nicht-reines natives Aufrufziel 'abs'" ;;
        ffi_callback_nicht_freigegeben) expected='Aggregate und Funktionstypen by value sind nicht freigegeben' ;;
        *) expected='Native FFI-Aufrufe sind derzeit nur in main ausserhalb von try unterstützt' ;;
    esac
    set +e
    timeout 30s "${P2_COMPILER:-compiler/target/release/moo-compiler}" compile "compiler/tests/p2/$case_name.moos" --emit-ir -o "$out/$case_name.ll" > "$out/$case_name.log" 2>&1
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

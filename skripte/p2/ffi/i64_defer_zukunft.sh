#!/usr/bin/env bash
set -euo pipefail
out=build/p2-ffi-i64-defer-zukunft
mkdir -p "$out"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_i64.c -o "$out/ref.o"
ar rcs "$out/libp2_i64.a" "$out/ref.o"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
blocked=0
for name in erfolg catch; do
    set +e
    timeout 120s "$compiler" compile "compiler/tests/p2/ffi_i64_defer_${name}.moos" --lib-pfad "$out" -o "$out/$name" >"$out/$name.log" 2>&1
    rc=$?
    set -e
    if (( rc == 124 || rc >= 128 )); then
        printf 'Compilerabbruch %s: %s\n' "$name" "$rc" >&2
        exit 1
    fi
    if (( rc != 0 )); then
        if grep -Fq 'Native 64-Bit-FFI bei vorhandenem aufräumen ist noch nicht unterstützt' "$out/$name.log"; then
            blocked=1
            continue
        fi
        cat "$out/$name.log" >&2
        exit 1
    fi
done
if (( blocked )); then
    printf 'BLOCKED: 64-Bit-FFI in Defer-Frames noch nicht angebunden\n'
    exit 2
fi
actual=$(timeout 30s "$out/erfolg")
[[ "$actual" == $'EXAKT\nENDE\nCLEANUP' ]] || { printf 'Unerwartete Erfolgsausgabe: %s\n' "$actual" >&2; exit 1; }
actual=$(timeout 30s "$out/catch")
[[ "$actual" == $'C_RETURN_AUFGERUFEN\nCATCH\nENDE\nCLEANUP' ]] || { printf 'Unerwartete Catchausgabe: %s\n' "$actual" >&2; exit 1; }
printf '64-Bit-FFI mit Defer/Catch: PASS\n'

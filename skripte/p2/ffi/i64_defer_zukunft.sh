#!/usr/bin/env bash
# P2-T55 (Plan f5b52a0b A5): 64-Bit-FFI in Funktionen mit aufraeumen und
# lokalem Catch. Bis P2-T55 Zukunftsgate (BLOCKED, RC 2), seit 33804 regulaer.
set -euo pipefail
out=build/p2-ffi-i64-defer-zukunft
mkdir -p "$out"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_i64.c -o "$out/ref.o"
ar rcs "$out/libp2_i64.a" "$out/ref.o"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
for name in erfolg catch; do
    timeout 120s "$compiler" compile "compiler/tests/p2/ffi_i64_defer_${name}.moos" --lib-pfad "$out" -o "$out/$name" >"$out/$name.log" 2>&1 \
        || { cat "$out/$name.log" >&2; exit 1; }
done
actual=$(timeout 30s "$out/erfolg")
[[ "$actual" == $'EXAKT\nENDE\nCLEANUP' ]] || { printf 'Unerwartete Erfolgsausgabe: %s\n' "$actual" >&2; exit 1; }
actual=$(timeout 30s "$out/catch")
[[ "$actual" == $'C_RETURN_AUFGERUFEN\nCATCH\nENDE\nCLEANUP' ]] || { printf 'Unerwartete Catchausgabe: %s\n' "$actual" >&2; exit 1; }
printf '64-Bit-FFI mit Defer/Catch: PASS\n'

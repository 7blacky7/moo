#!/usr/bin/env bash
# True T22 compile/link/run test; requires native calls and FFI implementation.
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-bindgen/integration
mkdir -p "$out"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 30s cc -std=c11 -O2 -Wall -Wextra -Werror -c compiler/tests/p2/bindgen_fixture.c -o "$out/fixture.o"
ar rcs "$out/libp2_fixture.a" "$out/fixture.o"
timeout 30s "$compiler" bindgen compiler/tests/p2/bindgen_fixture.h --prefix P2Pair --prefix p2_add > "$out/generated.moos"
awk '/^extern "C" funktion p2_add/ { print "@bibliothek(\"p2_fixture\")" } { print }' "$out/generated.moos" > "$out/smoke.moos"
cat >> "$out/smoke.moos" <<'MOO'
@nativ
funktion p2_probe() -> i32:
    unsicher:
        gib_zurück p2_add(2, 3)
zeige p2_probe()
MOO
timeout 120s "$compiler" compile "$out/smoke.moos" --lib-pfad "$out" -o "$out/smoke"
answer=$(timeout 30s "$out/smoke")
[[ $answer == 5 ]]
timeout 30s "$compiler" bindgen /usr/include/stdlib.h --prefix abs > "$out/libc.moos"
cat >> "$out/libc.moos" <<'MOO'
@nativ
funktion p2_libc_abs(wert: i32) -> i32:
    unsicher:
        gib_zurück abs(wert)
zeige p2_libc_abs(-42)
MOO
timeout 120s "$compiler" compile "$out/libc.moos" -o "$out/libc"
[[ $(timeout 30s "$out/libc") == 42 ]]
echo "P2 Bindgen C/Moo integration: PASS"

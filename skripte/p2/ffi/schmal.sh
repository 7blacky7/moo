#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-m1-ffi-schmal
mkdir -p "$out"
timeout 30s "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -c compiler/tests/p2/ffi_schmal.c -o "$out/ffi_schmal.o"
ar rcs "$out/libp2_ffi_schmal.a" "$out/ffi_schmal.o"
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_schmal.moos --lib-pfad "$out" -o "$out/probe"
timeout 120s "$compiler" compile compiler/tests/p2/ffi_schmal.moos --emit-ir -o "$out/probe.ll"
result=$(timeout 30s "$out/probe")
[[ "$result" == "-745" ]] || { printf 'Unerwartete C-Ausgabe: %s\n' "$result" >&2; exit 1; }
awk '
    /^define .*@("?__moo_native[.]schmal_probe"?)/ { inside=1; next }
    inside && /^}/ { inside=0 }
    inside && /call i32 @p2_ffi_schmal\(i8 signext .*i8 zeroext / { matched=1 }
    END { if (!matched) exit 1 }
' "$out/probe.ll"
printf 'i8/u8 C-ABI: PASS\n'

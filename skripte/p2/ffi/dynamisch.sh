#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-ffi-shared
mkdir -p "$out"
timeout 30s "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fPIC -shared compiler/tests/p2/ffi_schmal.c -o "$out/libp2_ffi_schmal.so"
timeout 120s "${P2_COMPILER:-compiler/target/release/moo-compiler}" compile compiler/tests/p2/ffi_schmal.moos --lib-pfad "$out" -o "$out/probe"
readelf -d "$out/probe" | rg -F -q 'Shared library: [libp2_ffi_schmal.so]'
result=$(LD_LIBRARY_PATH="$out${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" timeout 30s "$out/probe")
[[ "$result" == "-745" ]] || { printf 'Unerwartete C-Ausgabe: %s\n' "$result" >&2; exit 1; }
printf 'Dynamisch verlinkte C-Bibliothek: PASS\n'

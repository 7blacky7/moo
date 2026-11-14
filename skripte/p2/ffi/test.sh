#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=build/p2-m1-ffi
mkdir -p "$out"
expected=$(printf '42\n0\n2147483647')
for case_name in m1_ffi_abs m1_ffi_abs_spaet m1_ffi_abs_dynamisch; do
    timeout 120s "${P2_COMPILER:-compiler/target/release/moo-compiler}" compile "compiler/tests/p2/$case_name.moos" -o "$out/$case_name"
    timeout 120s "${P2_COMPILER:-compiler/target/release/moo-compiler}" compile "compiler/tests/p2/$case_name.moos" --emit-ir -o "$out/$case_name.ll"
    awk '
        /^define .*@("?__moo_native[.]abs_probe"?)/ { inside=1; signature=($0 ~ /i32.*i32/); next }
        inside && /^}/ { inside=0 }
        inside && /call i32 @abs\(i32 / { foreign_call=1 }
        END { if (!signature || !foreign_call) exit 1 }
    ' "$out/$case_name.ll"
    result=$(timeout 30s "$out/$case_name")
    [[ $result == "$expected" ]] || { printf '%s: unexpected output: %s\n' "$case_name" "$result" >&2; exit 1; }
    printf '%s: PASS\n' "$case_name"
done

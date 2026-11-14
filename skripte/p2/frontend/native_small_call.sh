#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/native_small
compiler=compiler/target/release/moo-compiler
src=compiler/tests/p2/native_klein_call.moos
out=build/p2-frontend/native_small/call
timeout 30s "$compiler" compile "$src" --emit-ir -o "$out.ll"
timeout 30s "$compiler" compile "$src" -o "$out"
timeout 30s "$out" > "$out.out"
cmp compiler/tests/p2/native_klein_call.expected "$out.out"
awk '
/^define .*@("?__moo_native[.]klein_s_plus"?)/ {inside=1; next}
/^define .*@("?__moo_native[.]klein_u_plus"?)/ {inside=2; next}
inside && /^}/ {inside=0}
inside == 1 && /call i8 @("?__moo_native[.]klein_s_id"?)/ {signedcall=1}
inside == 2 && /call i8 @("?__moo_native[.]klein_u_id"?)/ {unsignedcall=1}
END {if (!signedcall || !unsignedcall) exit 1}
' "$out.ll"
echo "PASS direkte i8/u8 Calls"

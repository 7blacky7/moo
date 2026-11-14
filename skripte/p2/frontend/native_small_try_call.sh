#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/native_small
compiler=compiler/target/release/moo-compiler
out=build/p2-frontend/native_small/try_call
timeout 30s "$compiler" compile compiler/tests/p2/native_klein_try_call.moos -o "$out"
timeout 30s "$out" > "$out.out" 2> "$out.err"
cmp compiler/tests/p2/native_klein_try_call.expected "$out.out"
echo "PASS Nullargument-Aufruf bei Erfolg, Fehler und Kurzschluss"

#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
if [ "$(uname -s)" != Linux ]; then
  echo "SKIP Return-Fehler-RSS: Linux-spezifisches wait4/ru_maxrss"
  exit 2
fi
mkdir -p build/p2-frontend/defer_return_error
out=build/p2-frontend/defer_return_error
compiler=compiler/target/release/moo-compiler
for n in probe 200k 800k; do
  timeout 30s "$compiler" compile "compiler/tests/p2/defer_return_error_${n}.moos" -o "$out/$n"
  timeout 120s "$out/$n" > "$out/$n.out" 2> "$out/$n.err"
  cmp "compiler/tests/p2/defer_return_error_${n}.expected" "$out/$n.out"
  echo "PASS Fehler-Catch $n"
done
timeout 30s "$compiler" compile compiler/tests/p2/defer_return_error_probe.moos --emit-ir -o "$out/probe.ll"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror compiler/tests/p2/defer_heap_rss.c -o "$out/measure"
timeout 120s "$out/measure" "$out/200k" "$out/800k"

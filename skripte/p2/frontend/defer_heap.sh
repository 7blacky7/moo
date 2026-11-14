#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
if [ "$(uname -s)" != Linux ]; then
  echo "SKIP Defer-RSS: Linux-spezifisches wait4/ru_maxrss"
  exit 2
fi
mkdir -p build/p2-frontend/defer_heap
out=build/p2-frontend/defer_heap
compiler=compiler/target/release/moo-compiler
for n in 200k 800k; do
  timeout 30s "$compiler" compile "compiler/tests/p2/defer_heap_${n}.moos" -o "$out/$n"
done
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror compiler/tests/p2/defer_heap_rss.c -o "$out/measure"
timeout 120s "$out/measure" "$out/200k" "$out/800k"

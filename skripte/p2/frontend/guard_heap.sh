#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
if [ "$(uname -s)" != Linux ]; then
  echo "SKIP Guard-Heap: Linux wait4/ru_maxrss"
  exit 2
fi
out=build/p2-frontend/guard_heap
mkdir -p "$out"
for n in 200k 800k; do
  timeout 30s compiler/target/release/moo-compiler compile "compiler/tests/p2/guard_heap_${n}.moos" -o "$out/$n"
  timeout 120s "$out/$n" > "$out/$n.out" 2> "$out/$n.err"
  cmp "compiler/tests/p2/guard_heap_${n}.expected" "$out/$n.out"
  echo "PASS Guard-Heap $n"
done
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror compiler/tests/p2/defer_heap_rss.c -o "$out/measure"
timeout 120s "$out/measure" "$out/200k" "$out/800k"

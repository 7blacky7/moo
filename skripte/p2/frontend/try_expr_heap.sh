#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
if [ "$(uname -s)" != Linux ]; then
  echo "SKIP Ausdrucks-RSS: Linux-spezifisches wait4/ru_maxrss"
  exit 2
fi
mkdir -p build/p2-frontend/try_expr_heap
out=build/p2-frontend/try_expr_heap
compiler=compiler/target/release/moo-compiler
for n in 200k 800k; do
  timeout 30s "$compiler" compile "compiler/tests/p2/try_expr_heap_${n}.moos" -o "$out/$n"
  timeout 120s "$out/$n" > "$out/$n.out" 2> "$out/$n.err"
  cmp "compiler/tests/p2/try_expr_heap_${n}.expected" "$out/$n.out"
  echo "PASS Fehler-Catch $n"
done
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror compiler/tests/p2/defer_heap_rss.c -o "$out/measure"
timeout 120s "$out/measure" "$out/200k" "$out/800k"

for n in 200k 800k; do
  timeout 30s "$compiler" compile "compiler/tests/p2/try_expr_heap_arg_${n}.moos" -o "$out/arg_$n"
  timeout 120s "$out/arg_$n" > "$out/arg_$n.out" 2> "$out/arg_$n.err"
  cmp "compiler/tests/p2/try_expr_heap_arg_${n}.expected" "$out/arg_$n.out"
  echo "PASS Argument-Catch ohne Seiteneffekt $n"
done
timeout 120s "$out/measure" "$out/arg_200k" "$out/arg_800k"
for n in 200k 800k; do
  timeout 30s "$compiler" compile "compiler/tests/p2/try_expr_heap_tief_${n}.moos" -o "$out/tief_$n"
  timeout 120s "$out/tief_$n" > "$out/tief_$n.out" 2> "$out/tief_$n.err"
  cmp "compiler/tests/p2/try_expr_heap_tief_${n}.expected" "$out/tief_$n.out"
  echo "PASS tiefer Listen-Catch ohne Seiteneffekt $n"
done
timeout 120s "$out/measure" "$out/tief_200k" "$out/tief_800k"
for n in 200k 800k; do
  timeout 30s "$compiler" compile "compiler/tests/p2/try_expr_heap_set_${n}.moos" -o "$out/set_$n"
  timeout 120s "$out/set_$n" > "$out/set_$n.out" 2> "$out/set_$n.err"
  cmp "compiler/tests/p2/try_expr_heap_set_${n}.expected" "$out/set_$n.out"
  echo "PASS Indexzuweisung-Catch ohne Seiteneffekt $n"
done
timeout 120s "$out/measure" "$out/set_200k" "$out/set_800k"

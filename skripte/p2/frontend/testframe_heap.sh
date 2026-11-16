#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
if [ "$(uname -s)" != Linux ]; then echo "SKIP Testframe-Heap: Linux wait4"; exit 2; fi
out=build/p2-frontend/testframe_heap
mkdir -p "$out"
for mode in kontrolle ok fail; do
  for size in 2k 8k; do
    case "$size" in 2k) count=2000 ;; 8k) count=8000 ;; esac
    timeout 30s compiler/target/release/moo-compiler compile "compiler/tests/p2/testframe_heap_${mode}_${size}.moos" -o "$out/${mode}_${size}"
    rc=0; timeout 120s "$out/${mode}_${size}" > "$out/${mode}_${size}.out" 2> "$out/${mode}_${size}.err" || rc=$?
    # P2-T117: FAIL in teste_alle() -> Exit 1, sonst 0.
    if [ "$mode" = fail ]; then marker='    FAIL'; want=1; else marker='    OK'; want=0; fi
    [ "$rc" -eq "$want" ] || { echo "FAIL Testframe $mode $size: Exit $rc statt $want"; exit 1; }
    found=$(rg -c -F -x "$marker" "$out/${mode}_${size}.out")
    test "$found" = "$count"
    rg -q -F -x 'ENDE' "$out/${mode}_${size}.out"
    echo "PASS Testframe $mode $size: $count Durchläufe"
  done
done
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror compiler/tests/p2/defer_heap_rss.c -o "$out/measure"
failed=0
for mode in kontrolle ok fail; do
  echo "RSS Testframe $mode (2k vs 8k)"
  if [ "$mode" = fail ]; then export MOO_RSS_EXIT=1; else export MOO_RSS_EXIT=0; fi
  if ! timeout 120s "$out/measure" "$out/${mode}_2k" "$out/${mode}_8k" | sed -e 's/200k=/2k=/g' -e 's/800k=/8k=/g'; then failed=1; fi
done
exit "$failed"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
out=build/p2-frontend/struktur_array_dyn
mkdir -p "$out/lib"
${CC:-cc} -std=c11 -D_GNU_SOURCE -O2 -Wall -Wextra -Werror -c compiler/tests/p2/struktur_array_dyn_orakel.c -o "$out/lib/orakel.o"
ar rcs "$out/lib/libstruktur_array_orakel.a" "$out/lib/orakel.o"
for name in struktur_array_dyn struktur_array_dyn_heap; do
  timeout 60s "$compiler" compile "compiler/tests/p2/$name.moos" --lib-pfad "$out/lib" -o "$out/$name" > "$out/$name.compile.log" 2>&1
  timeout 60s "$out/$name" > "$out/$name.out" 2> "$out/$name.err"
  cmp "compiler/tests/p2/$name.expected" "$out/$name.out"
  echo "PASS $name"
done
asan="$(gcc -print-file-name=libasan.so 2>/dev/null || true)"
if [ "$(uname -s)" = Linux ] && [ -f "$asan" ]; then
  for name in struktur_array_dyn struktur_array_dyn_heap; do
    timeout 90s env LD_PRELOAD="$asan" ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$out/$name" > "$out/$name.lsan.out" 2> "$out/$name.lsan.err"
    cmp "compiler/tests/p2/$name.expected" "$out/$name.lsan.out"
    echo "PASS $name: LSan"
  done
else
  echo "SKIP Array-LSan: Linux/libasan erforderlich"
fi

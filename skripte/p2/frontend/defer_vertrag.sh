#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/defer_vertrag
failed=0
mkdir -p build/p2-frontend/defer_vertrag/lib
${CXX:-c++} -std=c++17 -O2 -Wall -Wextra -Werror -c compiler/tests/p2/defer_vertrag_native_orakel.cpp -o build/p2-frontend/defer_vertrag/lib/orakel.o || exit 1
ar rcs build/p2-frontend/defer_vertrag/lib/libdefer_orakel.a build/p2-frontend/defer_vertrag/lib/orakel.o || exit 1
for name in lifo fehler ursprung bedingt schleife catch_return rekursion unbehandelt methoden methode_implizit nach_catch native native_ffi native_heap vorwaerts ausdruecke; do
  out="build/p2-frontend/defer_vertrag/$name"
  timeout 30s compiler/target/release/moo-compiler compile "compiler/tests/p2/defer_vertrag_${name}.moos" --lib-pfad build/p2-frontend/defer_vertrag/lib -o "$out" > "$out.compile.log" 2>&1
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "FAIL $name: Compile-Exit $rc"; failed=1; continue
  fi
  # P2-T117: ungefangene Fehler enden mit Exit 1 und Diagnose auf stderr.
  streng=0; [ "$name" = unbehandelt ] && streng=1
  MOO_STRENG=$streng timeout 10s "$out" > "$out.out" 2> "$out.err"
  rc=$?
  if [ "$name" = unbehandelt ]; then
    if [ "$rc" -ne 1 ] || ! rg -q -F 'Unbehandelter Fehler' "$out.err" || ! cmp -s "compiler/tests/p2/defer_vertrag_${name}.expected" "$out.out"; then
      echo "FAIL $name: Exit/Ausgabe (Exit $rc)"; failed=1
    else
      echo "PASS $name"
    fi
    continue
  fi
  if [ "$rc" -ne 0 ] || ! cmp -s "compiler/tests/p2/defer_vertrag_${name}.expected" "$out.out"; then
    echo "FAIL $name: Exit/Ausgabe (Exit $rc)"; failed=1
  else
    echo "PASS $name"
  fi
done

# Linux: derselbe erzeugte Code unter LeakSanitizer. Die C++-Gegenstelle
# allokiert nichts; native_heap stresst verworfene owning Ergebnisse.
asan="$(gcc -print-file-name=libasan.so 2>/dev/null || true)"
if [ "$(uname -s)" = Linux ] && [ -f "$asan" ]; then
  for name in native_ffi native_heap; do
    out="build/p2-frontend/defer_vertrag/$name"
    if LD_PRELOAD="$asan" ASAN_OPTIONS=detect_leaks=1 timeout 60s "$out" > "$out.lsan.out" 2> "$out.lsan.err" \
       && cmp -s "compiler/tests/p2/defer_vertrag_${name}.expected" "$out.lsan.out"; then
      echo "PASS $name: LSan"
    else
      echo "FAIL $name: LSan"; failed=1
    fi
  done
else
  echo "SKIP Defer-LSan: Linux/libasan erforderlich"
fi
exit "$failed"

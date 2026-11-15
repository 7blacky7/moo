#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/defer_vertrag
failed=0
for name in lifo fehler ursprung bedingt schleife catch_return rekursion unbehandelt methoden methode_implizit nach_catch; do
  out="build/p2-frontend/defer_vertrag/$name"
  timeout 30s compiler/target/release/moo-compiler compile "compiler/tests/p2/defer_vertrag_${name}.moos" -o "$out" > "$out.compile.log" 2>&1
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "FAIL $name: Compile-Exit $rc"; failed=1; continue
  fi
  # P2-T95: Abbruchvertrag nur noch mit MOO_STRENG=1 (Standard: Hinweis + Weiterlauf).
  streng=0; [ "$name" = unbehandelt ] && streng=1
  MOO_STRENG=$streng timeout 10s "$out" > "$out.out" 2> "$out.err"
  rc=$?
  if [ "$name" = unbehandelt ]; then
    if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ] || ! rg -q -F 'Unbehandelter Fehler' "$out.err" || ! cmp -s "compiler/tests/p2/defer_vertrag_${name}.expected" "$out.out"; then
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
exit "$failed"

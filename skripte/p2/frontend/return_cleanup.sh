#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/return
compiler=compiler/target/release/moo-compiler
failed=0
for name in unhandled outer ausdruck defer; do
  out="build/p2-frontend/return/$name"
  timeout 30s "$compiler" compile "compiler/tests/p2/try_return_${name}.moos" -o "$out" > "$out.compile.log" 2>&1
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "FAIL $name: Compile-Exit $rc"
    failed=1
    continue
  fi
  timeout 30s "$out" > "$out.out" 2> "$out.err"
  rc=$?
  if [ "$name" = unhandled ]; then
    if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ] || ! rg -q -F 'Unbehandelter Fehler' "$out.err"; then
      echo "FAIL $name: erwarteter unbehandelter Fehler fehlt (Exit $rc)"
      failed=1
      continue
    fi
  elif [ "$rc" -ne 0 ]; then
    echo "FAIL $name: Laufzeit-Exit $rc"
    failed=1
    continue
  fi
  if ! cmp -s "compiler/tests/p2/try_return_${name}.expected" "$out.out"; then
    echo "FAIL $name: Ausgabe stimmt nicht"
    failed=1
    continue
  fi
  echo "PASS $name"
done
exit "$failed"

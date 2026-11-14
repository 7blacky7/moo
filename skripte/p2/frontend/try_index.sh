#!/usr/bin/env bash
set -u
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/try_index
failed=0
for name in container index binary argument; do
  out="build/p2-frontend/try_index/$name"
  timeout 30s compiler/target/release/moo-compiler compile "compiler/tests/p2/try_index_${name}.moos" -o "$out" > "$out.compile.log" 2>&1
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "FAIL $name: Compile-Exit $rc"; failed=1; continue
  fi
  timeout 5s "$out" > "$out.out" 2> "$out.err"
  rc=$?
  if [ "$rc" -ne 0 ] || ! cmp -s "compiler/tests/p2/try_index_${name}.expected" "$out.out"; then
    echo "FAIL $name: Exit/Ausgabe (Exit $rc)"; failed=1
  else
    echo "PASS $name"
  fi
done
exit "$failed"

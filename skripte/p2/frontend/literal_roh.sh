#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/literal_roh
out=build/p2-frontend/literal_roh/positiv
compiler=compiler/target/release/moo-compiler
timeout 30s "$compiler" compile compiler/tests/p2/literal_roh_positiv.moos -o "$out"
timeout 30s "$out" > "$out.out"
cmp compiler/tests/p2/literal_roh_positiv.expected "$out.out"
echo "PASS binär, Kommazahl und Range"
for name in hex dez; do
  out="build/p2-frontend/literal_roh/$name"
  set +e
  timeout 30s "$compiler" compile "compiler/tests/p2/literal_roh_${name}.moos" --emit-ir -o "$out.ll" > "$out.log" 2>&1
  rc=$?
  set -e
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ] || ! rg -q -F -f "compiler/tests/p2/literal_roh_${name}.expected_error" "$out.log"; then
    echo "FAIL $name: erwarteter Rohtext/Exit fehlt (Exit $rc)"
    exit 1
  fi
  echo "PASS genauer Rohtext $name in Überlaufdiagnose"
done

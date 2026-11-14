#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/dynamisch
compiler=compiler/target/release/moo-compiler
dir=build/p2-frontend/dynamisch
timeout 30s "$compiler" compile compiler/tests/p2/m1_dyn_variablen.moos -o "$dir/gueltig"
timeout 30s "$dir/gueltig" > "$dir/gueltig.out" 2> "$dir/gueltig.err"
cmp compiler/tests/p2/m1_dyn_variablen.expected "$dir/gueltig.out"
echo "PASS dynamische i32-Werte und Grenzen"
for name in bruch gross text erstes_argument zweites_argument nan plus_inf minus_inf; do
  timeout 30s "$compiler" compile "compiler/tests/p2/m1_dyn_${name}.moos" -o "$dir/$name"
  set +e
  timeout 30s "$dir/$name" > "$dir/$name.out" 2> "$dir/$name.err"
  rc=$?
  set -e
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ]; then
    echo "FAIL $name: unerwarteter Exit $rc" >&2
    exit 1
  fi
  if ! rg -q -F 'Native Funktion erwartet eine i32-Ganzzahl' "$dir/$name.out" "$dir/$name.err"; then
    echo "FAIL $name: Fehlerdiagnose fehlt" >&2
    exit 1
  fi
  if rg -q -F -e 'NACH_FALSCH' -e 'FALSCHER_WERT' "$dir/$name.out" "$dir/$name.err"; then
    echo "FAIL $name: Seiteneffekt nach fehlerhaftem Argument" >&2
    exit 1
  fi
  echo "PASS $name"
done
# P2-T80: Kontextfaelle sind Positivtests: reine @nativ-Funktion mit dynamischem
# Argument in benannter Funktion (Direktform + Argumentposition) und als
# versuche:-Teilausdruck; ungueltiges Argument wird gefangen.
for name in funktion try_verschachtelt; do
  timeout 30s "$compiler" compile "compiler/tests/p2/m1_dyn_${name}.moos" -o "$dir/$name"
  timeout 30s "$dir/$name" > "$dir/$name.out" 2> "$dir/$name.err"
  cmp "compiler/tests/p2/m1_dyn_${name}.expected" "$dir/$name.out"
  if rg -q -F 'NACH_FALSCH' "$dir/$name.out"; then
    echo "FAIL $name: Seiteneffekt nach fehlerhaftem Argument" >&2
    exit 1
  fi
  echo "PASS Kontext $name (P2-T80: hosted @nativ in benannter Funktion / versuche:-Teilausdruck)"
done
# Fail-closed: aus Lambdas bleibt es ein Compilefehler mit Kontextdiagnose.
set +e
timeout 30s "$compiler" compile compiler/tests/p2/m1_dyn_lambda.moos -o "$dir/lambda" > "$dir/lambda.compile.log" 2>&1
rc=$?
set -e
if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ]; then
  echo "FAIL lambda: unerwarteter Compiler-Exit $rc" >&2
  exit 1
fi
if ! rg -q -F 'nicht in Lambdas/Methoden' "$dir/lambda.compile.log"; then
  echo "FAIL lambda: Kontextdiagnose fehlt" >&2
  exit 1
fi
echo "PASS Kontext lambda bleibt Compilefehler (fail-closed)"

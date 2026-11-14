#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/puffer_syntax
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
out=build/p2-frontend/puffer_syntax/legacy
timeout 30s "$compiler" compile compiler/tests/p2/puffer_syntax_legacy.moos -o "$out"
timeout 30s "$out" > "$out.out"
cmp compiler/tests/p2/puffer_syntax_legacy.expected "$out.out"
echo "PASS gewöhnlicher Index unter künftigem Konstruktornamen"
for name in funktion lambda wert typ_funktion typ_lambda typ_wert typ_forward typ_import en_funktion en_lambda en_wert; do
  out="build/p2-frontend/puffer_syntax/$name"
  timeout 30s "$compiler" compile "compiler/tests/p2/puffer_shadow_${name}.moos" -o "$out"
  timeout 30s "$out" > "$out.out"
  cmp "compiler/tests/p2/puffer_shadow_${name}.expected" "$out.out"
  echo "PASS Shadowing $name"
done
out=build/p2-frontend/puffer_syntax/collision_effect
timeout 30s "$compiler" compile compiler/tests/p2/puffer_syntax_collision_effect.moos -o "$out"
timeout 30s "$out" > "$out.out"
cmp compiler/tests/p2/puffer_syntax_collision_effect.expected "$out.out"
echo "PASS Index mit folgendem Seiteneffekt"
out=build/p2-frontend/puffer_syntax/collision
set +e
timeout 30s "$compiler" compile compiler/tests/p2/puffer_syntax_collision.moos -o "$out" > "$out.compile.log" 2>&1
rc=$?
set -e
echo "Kollisionsprobe Compile-Exit: $rc"
cat "$out.compile.log"
if [ "$rc" -eq 0 ]; then
  timeout 30s "$out"
elif [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ]; then
  exit 1
fi

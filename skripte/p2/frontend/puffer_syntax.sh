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
# P2-T29 S5 (Entscheid #38228): `x[i](f())` war still zwei Statements und
# wird jetzt als M-S003 gemeldet, ohne Binary und ohne Seiteneffekt.
out=build/p2-frontend/puffer_syntax/collision_effect
rm -f "$out"
set +e
timeout 30s "$compiler" compile compiler/tests/p2/puffer_syntax_collision_effect.moos -o "$out" > "$out.out" 2>&1
rc=$?
set -e
[ "$rc" -eq 1 ] || { cat "$out.out"; echo "FAIL collision_effect: RC $rc statt 1" >&2; exit 1; }
[ ! -e "$out" ] || { echo 'FAIL collision_effect: Binary trotz Syntaxfehler' >&2; exit 1; }
grep -qF -- "$(cat compiler/tests/p2/puffer_syntax_collision_effect.expected)" "$out.out" \
  || { cat "$out.out"; echo 'FAIL collision_effect: M-S003-Meldung fehlt' >&2; exit 1; }
grep -qF -- "setze f auf x[i]" "$out.out" || { cat "$out.out"; echo 'FAIL collision_effect: Vorschlag ohne Schreibweg' >&2; exit 1; }
echo "PASS Index mit folgender Klammer meldet M-S003 mit Schreibweg"
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

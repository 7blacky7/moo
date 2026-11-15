#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/native_small
compiler=compiler/target/release/moo-compiler
src=compiler/tests/p2/native_klein_positiv.moos
out=build/p2-frontend/native_small/positiv
timeout 30s "$compiler" compile "$src" --emit-ir -o "$out.ll"
timeout 30s "$compiler" compile "$src" -o "$out"
timeout 30s "$out" > "$out.out"
cmp compiler/tests/p2/native_klein_positiv.expected "$out.out"
awk '
/^define .*@("?__moo_native[.]klein_s_plus"?)/ {inside=1; sig=($0 ~ /i8.*i8/); next}
/^define .*@("?__moo_native[.]klein_u_plus"?)/ {inside=2; sig2=($0 ~ /i8.*i8/); next}
inside && /^}/ {inside=0}
inside == 1 && /add i8 / {signedadd=1}
inside == 2 && /add i8 / {unsignedadd=1}
inside && /add (nsw|nuw)/ {bad=1}
END {if (!sig || !sig2 || !signedadd || !unsignedadd || bad) exit 1}
' "$out.ll"
echo "PASS i8/u8 Grenzen, Wrap, Null und natives i8-IR"
out=build/p2-frontend/native_small/try
timeout 30s "$compiler" compile compiler/tests/p2/native_klein_try.moos -o "$out"
timeout 30s "$out" > "$out.out" 2> "$out.err"
cmp compiler/tests/p2/native_klein_try.expected "$out.out"
echo "PASS i8/u8 Catch und spätere Argumente"
failed=0
for name in i8_lo i8_hi u8_lo u8_hi i8_bruch i8_text i8_nan i8_inf i8_minus_inf u8_bruch u8_text u8_nan u8_inf u8_minus_inf; do
  ty=${name%%_*}
  out="build/p2-frontend/native_small/$name"
  if ! timeout 30s "$compiler" compile "compiler/tests/p2/native_klein_${name}.moos" -o "$out" > "$out.compile.log" 2>&1; then
    echo "FAIL $name: Kompilierung"; failed=1; continue
  fi
  set +e
  # P2-T95: Abbruchvertrag nur noch mit MOO_STRENG=1 (Standard: Hinweis + Weiterlauf).
  MOO_STRENG=1 timeout 30s "$out" > "$out.out" 2> "$out.err"
  rc=$?
  set -e
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ] ||
     ! rg -q -F "Native Funktion erwartet eine ${ty}-Ganzzahl" "$out.out" "$out.err" ||
     rg -q -F -e 'NACH_FALSCH' -e 'FALSCHER_WERT' "$out.out" "$out.err"; then
    echo "FAIL $name: Exit oder Diagnose ungültig (Exit $rc)"; failed=1
  else
    echo "PASS $name"
  fi
done
exit "$failed"

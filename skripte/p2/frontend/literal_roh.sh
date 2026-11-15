#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")/../../.."
mkdir -p build/p2-frontend/literal_roh
out=build/p2-frontend/literal_roh/positiv
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
timeout 30s "$compiler" compile compiler/tests/p2/literal_roh_positiv.moos -o "$out"
timeout 30s "$out" > "$out.out"
cmp compiler/tests/p2/literal_roh_positiv.expected "$out.out"
echo "PASS binär, Kommazahl und Range"

# P2-T5 (Plan b1f51d65 A1-A3): Oktal, Literal-Matrix; Warnungen muessen in
# stderr des Compilers stehen (Praefix ohne Ziffern, f32-Unterlauf).
for pair in "literal_oktal|Zahlliteral '0x' hat keine Ziffern" "literal_matrix|Floatliteral '1e-50' rundet auf null"; do
  name="${pair%%|*}"
  warnung="${pair#*|}"
  out="build/p2-frontend/literal_roh/$name"
  timeout 30s "$compiler" compile "compiler/tests/p2/${name}.moos" -o "$out" > "$out.compile.log" 2>&1
  if ! rg -q -F "$warnung" "$out.compile.log"; then
    echo "FAIL $name: Warnung fehlt: $warnung"
    exit 1
  fi
  timeout 30s "$out" > "$out.out"
  if ! cmp -s "compiler/tests/p2/${name}.expected" "$out.out"; then
    echo "FAIL $name: Ausgabe weicht ab"
    diff "compiler/tests/p2/${name}.expected" "$out.out" || true
    exit 1
  fi
  echo "PASS $name (Ausgabe und Warnung)"
done

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

for name in literal_grenze_i8 literal_grenze_u16 literal_grenze_i64 literal_grenze_u8_negativ literal_grenze_f32; do
  out="build/p2-frontend/literal_roh/$name"
  set +e
  timeout 30s "$compiler" compile "compiler/tests/p2/${name}.moos" --emit-ir -o "$out.ll" > "$out.log" 2>&1
  rc=$?
  set -e
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ] || ! rg -q -F -f "compiler/tests/p2/${name}.expected_error" "$out.log"; then
    echo "FAIL $name: erwartete Grenzdiagnose/Exit fehlt (Exit $rc)"
    cat "$out.log"
    exit 1
  fi
  echo "PASS $name"
done

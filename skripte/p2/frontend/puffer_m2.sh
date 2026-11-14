#!/usr/bin/env bash
# P2-T50: typisierte Puffer aus Moo (puffer_erstellen/buffer_create, Index get/set).
# Positivfaelle byteidentisch zu .expected; Compilefehler fuer ungueltige
# Elementtypen; fangbare Laufzeitfehler ohne Handler: RC != 0, nichts danach.
set -eu
cd "$(dirname "$0")/../../.."
d=build/p2-frontend/puffer_m2
t=compiler/tests/p2
mkdir -p "$d"
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"

for n in puffer_m2_positiv puffer_m2_english puffer_m2_alias puffer_m2_funktion puffer_m2_try_nest puffer_typen; do
  timeout 30s "$compiler" compile "$t/$n.moos" -o "$d/$n"
  timeout 30s "$d/$n" > "$d/$n.out" 2> "$d/$n.err"
  cmp "$t/$n.expected" "$d/$n.out"
  echo "PASS $n"
done

for n in puffer_m2_typ_unbekannt puffer_m2_typ_ungueltig; do
  if timeout 30s "$compiler" compile "$t/$n.moos" -o "$d/$n" > "$d/$n.log" 2>&1; then
    echo "FAIL $n: Compilefehler erwartet" >&2; exit 1
  fi
  grep -q 'Elementtyp' "$d/$n.log" || { cat "$d/$n.log" >&2; exit 1; }
  echo "PASS $n (Compilefehler)"
done

for n in puffer_m2_index_ende puffer_m2_index_negativ puffer_m2_laenge_negativ puffer_m2_laenge_bruch \
         puffer_m2_laenge_text puffer_m2_wert_overflow puffer_m2_wert_bruch; do
  timeout 30s "$compiler" compile "$t/$n.moos" -o "$d/$n"
  set +e
  timeout 30s "$d/$n" > "$d/$n.out" 2> "$d/$n.err"
  rc=$?
  set -e
  if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ] || [ "$rc" -ge 128 ]; then
    echo "FAIL $n: rc=$rc (erwartet fangbarer Moo-Fehler)" >&2; cat "$d/$n.err" >&2; exit 1
  fi
  if grep -q 'NACH_FALSCH' "$d/$n.out"; then echo "FAIL $n: Code nach dem Fehler lief" >&2; exit 1; fi
  grep -qi 'puffer' "$d/$n.err" "$d/$n.out" || { echo "FAIL $n: keine Pufferdiagnose" >&2; cat "$d/$n.err" >&2; exit 1; }
  echo "PASS $n (rc=$rc)"
done
echo "PASS P2-T50 Puffer aus Moo: 6 Positivfaelle, 2 Compilefehler, 7 Laufzeitfehler"

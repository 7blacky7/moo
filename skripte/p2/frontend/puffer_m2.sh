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

# P2-T50/T15 (Plan bb3878ef C1): Batch A dynamisch (länge, anhängen, ansicht) und
# Batch B nativ (puffer[T] je Zahlentyp, Grenzpruefung, Pin-Vertrag), byteidentisch.
# puffer_nativ_breit: Plan-Risiko R4 (i64/u64 ueber 2^53 nativ exakt, dyn. abgewiesen).
# puffer_struktur: P2-T48 D puffer[Struktur] (nativ fuellen/lesen, Ansicht, Struktur-ID).
# puffer_nativ_sicher: P2-T12 sichere Puffer-Natives ohne unsicher: (ersetzt S4-Negativfall).
for n in puffer_m3_dyn puffer_nativ puffer_nativ_breit puffer_struktur puffer_nativ_sicher; do
  timeout 30s "$compiler" compile "$t/$n.moos" -o "$d/$n"
  timeout 30s "$d/$n" > "$d/$n.out" 2> "$d/$n.err"
  cmp "$t/$n.expected" "$d/$n.out"
  if grep -q 'NACH_FALSCH' "$d/$n.out"; then echo "FAIL $n: Code nach dem Fehler lief" >&2; exit 1; fi
  echo "PASS $n"
done
for fall in 'puffer_nativ_neg_ausdruck:muss vorerst eine Variable sein' \
            'puffer_nativ_neg_rueckgabe:Rueckgabetyp puffer[T] ist nicht freigegeben' \
            'puffer_nativ_neg_bool:braucht einen Zahlen-Elementtyp' \
            'puffer_nativ_neg_einstieg:keine C-Exporte'; do
  n=${fall%%:*} text=${fall#*:}
  if timeout 30s "$compiler" compile "$t/$n.moos" -o "$d/$n" > "$d/$n.log" 2>&1; then
    echo "FAIL $n: Compilefehler erwartet" >&2; exit 1
  fi
  grep -qF "$text" "$d/$n.log" || { cat "$d/$n.log" >&2; exit 1; }
  echo "PASS $n (Compilefehler)"
done
# C2: Summenschleife nach O2 ohne Aufruf, f32-Load mit align 4, im Rohen-IR
# genau eine Grenzpruefung; IR verify.
timeout 30s "$compiler" compile "$t/puffer_nativ.moos" --emit-ir -o "$d/puffer_nativ.ll"
mise exec -- opt -passes=verify -disable-output "$d/puffer_nativ.ll"
summe_roh="$(sed -n '/define internal double @__moo_native.summe(/,/^}/p' "$d/puffer_nativ.ll")"
[ "$(grep -c 'sys.puffer.ausserhalb =' <<<"$summe_roh")" -eq 1 ] || { echo "FAIL summe: nicht genau eine Grenzpruefung" >&2; exit 1; }
mise exec -- llvm-extract -S --func=__moo_native.summe "$d/puffer_nativ.ll" -o "$d/summe.ll"
mise exec -- opt -passes='default<O2>' -S "$d/summe.ll" -o "$d/summe.o2.ll"
if grep -q ' call ' "$d/summe.o2.ll"; then echo "FAIL summe: Aufruf in der Schleife" >&2; exit 1; fi
grep -qE 'load float, ptr %[^,]+, align 4' "$d/summe.o2.ll" || { echo "FAIL summe: kein f32-Load mit align 4" >&2; exit 1; }
echo "PASS P2-T50/T15 Puffer: Batch A dyn + Batch B nativ (10 Typen), 4 Compilefehler, sicher ohne unsicher:, IR ohne Aufruf/align 4"

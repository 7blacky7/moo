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

# P2-T83 (Plan f3217c27): Tensor (puffer[f32]) und Frame (puffer[u8]) kopierfrei an
# native Kerne (moo_native_leihen/_zurueck), @liest, bf16-Tensor, Typfehler fangbar.
timeout 60s "$compiler" compile "$t/tensor_ansicht.moos" -o "$d/tensor_ansicht"
timeout 30s "$d/tensor_ansicht" > "$d/tensor_ansicht.out"
diff -u "$t/tensor_ansicht.expected" "$d/tensor_ansicht.out"
if timeout 30s "$compiler" compile "$t/tensor_ansicht_neg_liest.moos" --emit-ir -o "$d/neg_liest.ll" > "$d/neg_liest.log" 2>&1; then
  echo "FAIL tensor_ansicht_neg_liest: Compilefehler erwartet" >&2; exit 1
fi
grep -qF "ist nur-lesend, der Koerper schreibt aber Elemente" "$d/neg_liest.log" || { cat "$d/neg_liest.log" >&2; exit 1; }
# B3 (Review 6315dba0): @liest-Parameter weder an schreibbare Pufferparameter
# weitergeben noch aliasen; puffer_zeiger nur in unsicher: (R3). Positivfall:
# Weitergabe an @liest-Kern und puffer_zeiger in unsicher:, Tensor unveraendert.
for fall in 'tensor_ansicht_neg_liest_weitergabe:von '"'"'nullen_kern'"'"' weitergegeben, der nicht @liest(1) ist' \
            'tensor_ansicht_neg_liest_alias:wird als Wert weiterverwendet' \
            'tensor_ansicht_neg_liest_zeiger:puffer_zeiger(t) steht ausserhalb von unsicher:'; do
  n=${fall%%:*} text=${fall#*:}
  if timeout 30s "$compiler" compile "$t/$n.moos" --emit-ir -o "$d/$n.ll" > "$d/$n.log" 2>&1; then
    echo "FAIL $n: Compilefehler erwartet" >&2; exit 1
  fi
  grep -qF "$text" "$d/$n.log" || { cat "$d/$n.log" >&2; exit 1; }
  echo "PASS $n (Compilefehler)"
done
timeout 60s "$compiler" compile "$t/tensor_ansicht_liest_weitergabe.moos" -o "$d/liest_weitergabe"
timeout 30s "$d/liest_weitergabe" > "$d/liest_weitergabe.out"
diff -u "$t/tensor_ansicht_liest_weitergabe.expected" "$d/liest_weitergabe.out"
# Ohne Kopie: maximale RSS steigt durch 5 native Summen ueber 40 MB nicht (Delta < 10 MiB).
b="$(cd "$(dirname "$0")/../bench" && pwd)"
timeout 60s "$compiler" compile "$b/tensor_ansicht_rss_basis.moos" -o "$d/rss_basis"
timeout 60s "$compiler" compile "$b/tensor_ansicht_rss_nativ.moos" -o "$d/rss_nativ"
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror "$t/defer_heap_rss.c" -o "$d/rss_mess"
timeout 120s "$d/rss_mess" "$d/rss_basis" "$d/rss_nativ"
echo "PASS P2-T83 Tensor/Frame kopierfrei (f32/bf16/Frame), @liest-Schreib-/Weitergabe-/Alias-/Zeigerverbot, Typfehler fangbar, RSS ohne Kopie"

# P2-T90 (Review 263bb18d): ein von .hinzufügen/.append abgelehntes Heapargument
# (Text, Liste, Woerterbuch unter versuche) wird freigegeben. Leckprobe unter
# LSan: Programm ueber cc-Wrapper mit -fsanitize=address gelinkt (wie
# cpp_adapter griff-sanitize); ein verlorener Block beendet den Lauf mit RC != 0.
san="$d/t90-san"
mkdir -p "$san"
printf '#!/bin/sh\nexec "%s" -fsanitize=address "$@"\n' "$(mise exec -- which clang)" > "$san/cc"
chmod +x "$san/cc"
PATH="$PWD/$san:$PATH" timeout 120s "$compiler" compile "$t/nichts_hinzufuegen.moos" -o "$san/nichts_hinzufuegen" > "$san/compile.log" 2>&1
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 timeout 60s "$san/nichts_hinzufuegen" > "$san/nichts_hinzufuegen.out"
diff -u "$t/nichts_hinzufuegen.expected" "$san/nichts_hinzufuegen.out"
echo "PASS P2-T90 hinzufügen/append: abgelehnte Heapargumente freigegeben (LSan)"

# P2-T49 (Plan 38c59c64 C1-C3): Strukturwerte nativ <-> dynamisch (Box, Sicht,
# Konstruktor, zeige, Gleichheit) und fangbare Grenzfehler, byteidentisch und
# zusaetzlich unter LSan (gleicher cc-Wrapper); Compilefehler fuer nicht boxbare
# Strukturen (Ausrichtung, Zeigerfeld) und falsche Konstruktor-Stelligkeit.
for n in struktur_box struktur_box_fehler; do
  timeout 60s "$compiler" compile "$t/$n.moos" -o "$d/$n" > "$d/$n.log" 2>&1 || { cat "$d/$n.log" >&2; exit 1; }
  timeout 30s "$d/$n" > "$d/$n.out" 2> "$d/$n.err"
  diff -u "$t/$n.expected" "$d/$n.out"
  if grep -q 'NACH_FALSCH' "$d/$n.out"; then echo "FAIL $n: Code nach dem Fehler lief" >&2; exit 1; fi
  PATH="$PWD/$san:$PATH" timeout 120s "$compiler" compile "$t/$n.moos" -o "$san/$n" > "$san/$n.log" 2>&1
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 timeout 60s "$san/$n" > "$san/$n.out"
  diff -u "$t/$n.expected" "$san/$n.out"
  echo "PASS $n (+LSan)"
done
for fall in "struktur_box_neg_ausrichtung:Ausrichtung 32 von 'Weit' liegt ueber 16" \
            'struktur_box_neg_zeiger:Zeigerfelder sind in dynamischem Moo nicht erlaubt' \
            "struktur_box_neg_stelligkeit:Struktur 'Innen' erwartet 2 Werte"; do
  n=${fall%%:*} text=${fall#*:}
  if timeout 30s "$compiler" compile "$t/$n.moos" -o "$d/$n" > "$d/$n.log" 2>&1; then
    echo "FAIL $n: Compilefehler erwartet" >&2; exit 1
  fi
  grep -qF "$text" "$d/$n.log" || { cat "$d/$n.log" >&2; exit 1; }
  echo "PASS $n (Compilefehler)"
done
# C4: Leckstress 200k/800k Rundlaeufe (Box, nativer Rundlauf, Sicht, gefangener
# Grenzfehler); maximale RSS darf nicht mit der Rundenzahl wachsen (rss_mess).
timeout 60s "$compiler" compile "$b/struktur_box_rss_200k.moos" -o "$d/struktur_rss_200k" > "$d/struktur_rss.log" 2>&1
timeout 60s "$compiler" compile "$b/struktur_box_rss_800k.moos" -o "$d/struktur_rss_800k" >> "$d/struktur_rss.log" 2>&1
timeout 120s "$d/rss_mess" "$d/struktur_rss_200k" "$d/struktur_rss_800k"
echo "PASS P2-T49 Strukturwerte nativ <-> dynamisch: 2 Programme (+LSan), 3 Compilefehler, Leckstress 200k/800k"

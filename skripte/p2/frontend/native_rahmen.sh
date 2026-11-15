#!/usr/bin/env bash
# P2-T85 (Plan 983e8a9c D5): native Aufrufe aus Methoden und Lambdas.
# Positiv: Methode (Statement, Ausdruck, Systemkoerper in unsicher:, geerbt),
# Lambda (direkt, mit Capture/Trampolin, innerhalb versuche: definiert);
# Fehler im Aufrufer gefangen, kein Seiteneffekt nach dem Fehler. IR-Verify.
# Negativ (E5): nativer Aufruf als Teilausdruck im Lambda -> Compilefehler.
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
t=compiler/tests/p2
d=build/p2-frontend/native_rahmen
mkdir -p "$d"
timeout 60s "$compiler" compile "$t/native_rahmen.moos" -o "$d/native_rahmen"
timeout 30s "$d/native_rahmen" > "$d/native_rahmen.out"
diff -u "$t/native_rahmen.expected" "$d/native_rahmen.out"
timeout 60s "$compiler" compile "$t/native_rahmen.moos" --emit-ir -o "$d/native_rahmen.ll"
mise exec -- opt -passes=verify -disable-output "$d/native_rahmen.ll"
if timeout 30s "$compiler" compile "$t/native_rahmen_neg_lambda.moos" --emit-ir -o "$d/neg_lambda.ll" > "$d/neg_lambda.log" 2>&1; then
  echo "FAIL native_rahmen_neg_lambda: Compilefehler erwartet" >&2; exit 1
fi
grep -qF "nur als ganzer Lambda-Koerper erlaubt" "$d/neg_lambda.log" || { cat "$d/neg_lambda.log" >&2; exit 1; }
# D1/D2-Ergaenzung (Review c23f9a29): aufraeumen in Methode wie in Funktion
# (P2-T92: Pruefpunkt mit eigenem Ausgang je Stelle - nach dem Fehler laufen
# nur die registrierten Aktionen, kein weiterer Seiteneffekt, Aufrufer faengt),
# Konstruktor, selbst.puffer + Feldkette (C1),
# map/filter (E6), Lambda in guard-aktiver Funktion und im Statement-Ausdruck.
timeout 60s "$compiler" compile "$t/native_rahmen_d.moos" -o "$d/native_rahmen_d"
timeout 30s "$d/native_rahmen_d" > "$d/native_rahmen_d.out"
diff -u "$t/native_rahmen_d.expected" "$d/native_rahmen_d.out"
timeout 60s "$compiler" compile "$t/native_rahmen_d.moos" --emit-ir -o "$d/native_rahmen_d.ll"
mise exec -- opt -passes=verify -disable-output "$d/native_rahmen_d.ll"
# D3/E7: weiterhin gesperrt.
neg() {
  local name="$1" text="$2" extra="${3:-}"
  # shellcheck disable=SC2086
  if timeout 30s "$compiler" compile "$t/native_rahmen_neg_$name.moos" $extra --emit-ir -o "$d/neg_$name.ll" > "$d/neg_$name.log" 2>&1; then
    echo "FAIL native_rahmen_neg_$name: Compilefehler erwartet" >&2; exit 1
  fi
  grep -qF -- "$text" "$d/neg_$name.log" || { cat "$d/neg_$name.log" >&2; exit 1; }
}
# P2-T55 (Plan f5b52a0b A4): die FFI-Einzelform ist seit P2-T55 hosted fangbar
# und damit auch aus Methoden und Lambdas aufrufbar (vorher E7-Negativfall).
for name in ffi_methode ffi_lambda; do
  timeout 30s "$compiler" compile "$t/native_rahmen_neg_$name.moos" -o "$d/$name"
  [ "$(timeout 30s "$d/$name")" = 3 ] || { echo "FAIL native_rahmen_neg_$name: Ausgabe statt 3" >&2; exit 1; }
done
neg nativ_methode "Native Methode 'K.m' wird noch nicht unterstützt"
neg bare "Dynamische native Aufrufe sind im Bare-Profil noch nicht unterstützt" --no-stdlib
# D4: Leckstress 200k/800k Fehleraufrufe in Methode und Lambda (+ selbst.puffer).
timeout 60s "$compiler" compile "$t/native_rahmen_leck_200k.moos" -o "$d/leck_200k"
timeout 60s "$compiler" compile "$t/native_rahmen_leck_800k.moos" -o "$d/leck_800k"
[ "$(timeout 60s "$d/leck_200k")" = "$(printf '400000\n800000')" ] || { echo "FAIL leck_200k Ausgabe" >&2; exit 1; }
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror "$t/defer_heap_rss.c" -o "$d/rss_mess"
timeout 300s "$d/rss_mess" "$d/leck_200k" "$d/leck_800k"
echo "PASS P2-T85 native Aufrufe aus Methoden und Lambdas (Fehlervertrag wie Funktionen, aufraeumen, Konstruktor, selbst.puffer/Feldkette, map/filter, Lambda-Rahmenhygiene, IR verify, E5 + 2 E7-Negativfaelle, FFI-Einzelform aus Methode/Lambda (P2-T55), Leckstress 200k/800k)"

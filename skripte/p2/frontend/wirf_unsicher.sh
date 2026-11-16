#!/usr/bin/env bash
# P2-T93 (Befund dbea7b86): wirf verhaelt sich mit und ohne unsicher:-Block
# identisch. Gefangen: Matrix gegen .expected; die Paare 1/2, 3/4, 5/6, 7/8
# unterscheiden sich nur durch den unsicher:-Block und muessen dieselbe
# Ausgabe liefern. Die werfende Funktion bricht ab (kein NACH_G*/NACH_H5-8),
# aufraeumen laeuft genau einmal. Hinweis Fall 1/2: ein Aufrufer OHNE eigenen
# Handler (kein versuche:/aufraeumen) laeuft im Bestandsmodell nach dem Aufruf
# weiter (nichts, NACH_H1/H2) - fuer beide Varianten gleich. Ungefangen: RC 1,
# Diagnose, nichts danach. IR: opt verify.
set -euo pipefail
cd "$(dirname "$0")/../../.."
compiler="${P2_COMPILER:-compiler/target/release/moo-compiler}"
t=compiler/tests/p2
out=build/p2-frontend/wirf_unsicher
mkdir -p "$out"
timeout 60s "$compiler" compile "$t/wirf_unsicher.moos" -o "$out/wirf"
timeout 10s "$out/wirf" > "$out/wirf.out"
diff -u "$t/wirf_unsicher.expected" "$out/wirf.out"
if grep -qE 'NACH_(G_PLAIN|H3|H5|H6|H7|H8|T[0-9])' "$out/wirf.out"; then
    echo 'FEHLER: Code nach wirf lief' >&2; exit 1
fi
timeout 60s "$compiler" compile "$t/wirf_unsicher.moos" --emit-ir -o "$out/wirf.ll" > /dev/null
mise exec -- opt -passes=verify -disable-output "$out/wirf.ll"
timeout 60s "$compiler" compile "$t/wirf_unsicher_ungefangen.moos" -o "$out/ungefangen"
set +e
# P2-T117: ungefangen = Diagnose + RC 1, mit und ohne MOO_STRENG=1.
timeout 10s "$out/ungefangen" > "$out/ungefangen_standard.out" 2> "$out/ungefangen_standard.err"
rc_standard=$?
MOO_STRENG=1 timeout 10s "$out/ungefangen" > "$out/ungefangen.out" 2> "$out/ungefangen.err"
rc=$?
set -e
[ "$rc_standard" -eq 1 ] || { echo "FEHLER: ungefangen (Standard) RC $rc_standard statt 1" >&2; exit 1; }
grep -qF 'Unbehandelter Fehler' "$out/ungefangen_standard.err" || { echo 'FEHLER: Diagnose fehlt (Standard)' >&2; exit 1; }
if grep -q 'NACH_' "$out/ungefangen_standard.out"; then echo 'FEHLER: Code nach ungefangenem wirf lief (Standard)' >&2; exit 1; fi
[ "$rc" -eq 1 ] || { echo "FEHLER: ungefangen (MOO_STRENG=1) RC $rc statt 1" >&2; exit 1; }
grep -qF 'Unbehandelter Fehler' "$out/ungefangen.err" || { echo 'FEHLER: Diagnose fehlt' >&2; exit 1; }
if grep -q 'NACH_' "$out/ungefangen.out"; then echo 'FEHLER: Code nach ungefangenem wirf lief' >&2; exit 1; fi
echo 'PASS P2-T93 wirf mit/ohne unsicher: identisch (8 Faelle gefangen, ungefangen RC 1, IR verify)'

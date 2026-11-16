#!/usr/bin/env bash
# P2-T95 (Plan 04998ea4) mit Korrektur P2-T117 (Koordinator-Entscheid 16.11.2026):
# Fehler ohne versuche: im gesamten Aufrufpfad -> Diagnose auf stderr und IMMER
# Exit 1 (unabhaengig von MOO_STRENG). Fall B (aeusseres versuche: vorhanden) ->
# Hinweis an der Zwischenstelle, Weiterlauf, aeusseres versuche: faengt; nur
# MOO_STRENG=1 bricht dort zusaetzlich ab. Prueft stdout, stderr und RC je Fixture.
set -uo pipefail
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
d=skripte/p2/fehler_hinweis
out=build/p2-fehler-hinweis
mkdir -p "$out"
fehler=0

fail() { echo "FAIL $1"; fehler=1; }

# lauf <name> [STRENG]: Programm bauen und ausfuehren, RC in $rc.
lauf() {
  local name="$1" streng="${2:-0}"
  timeout 120s "$compiler" compile "$d/$name.moos" -o "$out/$name" > "$out/$name.compile.log" 2>&1 \
    || { fail "$name: Compilefehler"; cat "$out/$name.compile.log"; rc=99; return; }
  MOO_STRENG=$streng timeout 30s "$out/$name" > "$out/$name.$streng.out" 2> "$out/$name.$streng.err"
  rc=$?
}
err_hat() { rg -q -F -- "$2" "$out/$1.0.err" || { fail "$1: stderr ohne '$2'"; cat "$out/$1.0.err"; }; }
err_ohne() { if rg -q -F -- "$2" "$out/$1.0.err"; then fail "$1: stderr hat '$2'"; cat "$out/$1.0.err"; fi; }
out_ist() {
  printf '%b' "$2" > "$out/$1.erwartet"
  cmp -s "$out/$1.erwartet" "$out/$1.0.out" || { fail "$1: stdout"; diff "$out/$1.erwartet" "$out/$1.0.out"; }
}
anzahl() { rg -c -F -- "$2" "$out/$1.0.err" || echo 0; }

# A: ohne versuche: -> Abbruch mit Exit 1 und Diagnose, egal welcher MOO_STRENG-Wert
for wert in 0 1 2 true ja; do
  lauf ohne_versuche "$wert"
  [ "$rc" -eq 1 ] || fail "ohne_versuche MOO_STRENG=$wert: RC $rc statt 1"
  rg -q -F 'Unbehandelter Fehler: kaputt in g' "$out/ohne_versuche.$wert.err" \
    || fail "ohne_versuche MOO_STRENG=$wert: Diagnose fehlt"
  rg -q -F 'geworfen in g (ohne_versuche.moos:3)' "$out/ohne_versuche.$wert.err" \
    || fail "ohne_versuche MOO_STRENG=$wert: Wurfort fehlt"
  rg -q -F 'Abbruch mit Exit-Code 1' "$out/ohne_versuche.$wert.err" \
    || fail "ohne_versuche MOO_STRENG=$wert: Abbruchhinweis fehlt"
  [ -s "$out/ohne_versuche.$wert.out" ] && fail "ohne_versuche MOO_STRENG=$wert: lief weiter"
done

# B: aeusseres versuche:
lauf aussen_versuche
[ "$rc" -eq 0 ] || fail "aussen_versuche: RC $rc statt 0"
out_ist aussen_versuche 'f laeuft weiter\ngefangen: kaputt in g\nende main\n'
err_hat aussen_versuche "geworfen in g (aussen_versuche.moos:4)"
err_hat aussen_versuche "nicht behandelt in f beim Aufruf von g (aussen_versuche.moos:7) – f läuft weiter"
lauf aussen_versuche 1
[ "$rc" -eq 1 ] || fail "aussen_versuche STRENG: RC $rc statt 1"

# verschachtelt: genau ein Hinweis
lauf verschachtelt
[ "$rc" -eq 0 ] || fail "verschachtelt: RC $rc"
out_ist verschachtelt 'f weiter\nh weiter\ngefangen: tief\n'
[ "$(anzahl verschachtelt 'Hinweis: unbehandelter Fehler')" -eq 1 ] || fail "verschachtelt: nicht genau ein Hinweis"
err_hat verschachtelt 'nicht behandelt in f beim Aufruf von g'

# gefangen: kein Hinweis
lauf gefangen
[ "$rc" -eq 0 ] || fail "gefangen: RC $rc"
out_ist gefangen 'f faengt: kaputt in g\n2\n'
[ -s "$out/gefangen.0.err" ] && { fail "gefangen: stderr nicht leer"; cat "$out/gefangen.0.err"; }

# schleife ohne versuche: der erste Wurf beendet das Programm (keine Flut, RC 1)
lauf schleife
[ "$rc" -eq 1 ] || fail "schleife: RC $rc statt 1"
out_ist schleife ''
[ "$(anzahl schleife 'Unbehandelter Fehler')" -eq 1 ] || fail "schleife: nicht genau eine Diagnose"
err_hat schleife 'Unbehandelter Fehler: gerade 0'

# nichts gefunden: kein Hinweis
lauf nichts
[ "$rc" -eq 0 ] || fail "nichts: RC $rc"
out_ist nichts 'nicht gefunden\n'
[ -s "$out/nichts.0.err" ] && { fail "nichts: stderr nicht leer"; cat "$out/nichts.0.err"; }

# Modul: Wurfort nennt die Moduldatei
lauf modul
[ "$rc" -eq 0 ] || fail "modul: RC $rc"
err_hat modul 'geworfen in werfe_im_modul (modul_werfer.moos:3)'
err_hat modul "nicht behandelt in f beim Aufruf von werfe_im_modul (modul.moos:5)"

if [ "$fehler" -ne 0 ]; then echo "FAIL P2-T95 fehler_hinweis"; exit 1; fi
echo "PASS P2-T95/T117 fehler_hinweis: ohne versuche: RC 1 + Diagnose (alle MOO_STRENG-Werte), aeusseres versuche: Hinweis + Weiterlauf, verschachtelt einmal, gefangen still, Schleife bricht beim ersten Wurf ab, nichts kein Fehler, Moduldatei"

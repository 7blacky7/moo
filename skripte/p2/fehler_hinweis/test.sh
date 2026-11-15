#!/usr/bin/env bash
# P2-T95 (User-Entscheid 15.11.2026, Plan 04998ea4, Freigabe #38192):
# unbehandelte Fehler -> Hinweis auf stderr und Weiterlauf (RC unveraendert);
# Abbruch nur mit MOO_STRENG=1. Prueft stdout, stderr und RC je Fixture.
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

# A: ohne versuche:
lauf ohne_versuche
[ "$rc" -eq 0 ] || fail "ohne_versuche: RC $rc statt 0"
out_ist ohne_versuche 'f laeuft weiter\n1\nende main\n'
err_hat ohne_versuche 'Hinweis: unbehandelter Fehler „kaputt in g“'
err_hat ohne_versuche "geworfen in g (ohne_versuche.moos:3)"
err_hat ohne_versuche 'nicht behandelt: kein versuche: im Aufrufpfad – das Programm läuft weiter'
lauf ohne_versuche 1
[ "$rc" -eq 1 ] || fail "ohne_versuche STRENG: RC $rc statt 1"
rg -q -F 'Unbehandelter Fehler' "$out/ohne_versuche.1.err" || fail "ohne_versuche STRENG: Diagnose fehlt"
if rg -q -F 'f laeuft weiter' "$out/ohne_versuche.1.out"; then fail "ohne_versuche STRENG: lief weiter"; fi
# Nur exakt MOO_STRENG=1 ist opt-in (Review 0e909538): andere Werte = Standard.
for wert in 2 true ja; do
  lauf ohne_versuche "$wert"
  [ "$rc" -eq 0 ] || fail "ohne_versuche MOO_STRENG=$wert: RC $rc statt 0"
  rg -q -F 'Hinweis: unbehandelter Fehler „kaputt in g“' "$out/ohne_versuche.$wert.err" \
    || fail "ohne_versuche MOO_STRENG=$wert: Hinweis fehlt"
  rg -q -F 'ende main' "$out/ohne_versuche.$wert.out" || fail "ohne_versuche MOO_STRENG=$wert: lief nicht weiter"
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

# schleife: ein Hinweis + Zaehler, keine Flut
lauf schleife
[ "$rc" -eq 0 ] || fail "schleife: RC $rc"
out_ist schleife '5\nende main\n'
[ "$(anzahl schleife 'Hinweis: unbehandelter Fehler')" -eq 1 ] || fail "schleife: Flut"
err_hat schleife 'trat insgesamt 3 Mal auf'

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
echo "PASS P2-T95 fehler_hinweis: Hinweis + Weiterlauf (ohne/mit aeusserem versuche:), verschachtelt einmal, gefangen still, Schleife gezaehlt, nichts kein Fehler, Moduldatei, MOO_STRENG=1 RC 1, MOO_STRENG=2/true/ja Standard"

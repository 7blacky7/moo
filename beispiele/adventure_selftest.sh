#!/usr/bin/env bash
# ============================================================
# adventure_selftest.sh — Gameplay-Selftest fuer adventure.moo
#
# Spielt das Text-Adventure headless per deterministischer
# Befehlssequenz bis zum SIEG durch (Konsolen-Spiel => stdin-Pipe
# statt Fenster-Test-API) und prueft die Marker im Output.
#
# Aufruf:  bash beispiele/adventure_selftest.sh [moo-compiler-pfad]
# Exit 0 = PASS, 1 = FAIL
# ============================================================
set -u
MOO=${1:-./compiler/target/release/moo-compiler}

OUT=$(printf '%s\n' \
  "nimm fackel" \
  "benutze fackel" \
  "gehe norden" \
  "nimm schwert" \
  "benutze schwert" \
  "gehe sueden" \
  "gehe sueden" \
  "gehe westen" \
  "nimm amulett" \
  "gehe osten" \
  "gehe osten" \
  "gehe osten" \
  "benutze amulett" \
  "gehe osten" \
  "gehe unten" \
  "nimm schluessel" \
  "benutze schluessel" \
  "gehe osten" \
  "greife_an drache" \
  "greife_an drache" \
  "greife_an drache" \
  "greife_an drache" \
  "greife_an drache" \
  "nimm krone" \
  "n" \
  | timeout 120 "$MOO" run beispiele/adventure.moo 2>&1)

fehler=0
pruefe() {
  if echo "$OUT" | grep -qF "$2"; then
    echo "ASSERT OK   $1"
  else
    echo "ASSERT FAIL $1 (Marker '$2' fehlt)"
    fehler=1
  fi
}
pruefe_nicht() {
  if echo "$OUT" | grep -qF "$2"; then
    echo "ASSERT FAIL $1 (Marker '$2' unerwartet vorhanden)"
    fehler=1
  else
    echo "ASSERT OK   $1"
  fi
}

pruefe "ziel wird angezeigt"        "ZIEL: Finde die verlorene Krone"
pruefe "schwert ausgeruestet"       "Du nimmst schwert."
pruefe "waechter laesst durch"      "Der Waechter verneigt sich"
pruefe "schatzkammer geoeffnet"     "Du oeffnest Schatzkammer"
pruefe "drache besiegt"             "drache ist besiegt!"
pruefe "krone erscheint"            "Im Gold-Hort glitzert"
pruefe "SIEG erreicht"              "*** SIEG! Du haeltst die verlorene Krone"
pruefe_nicht "kein game over"       "Game Over"

if [ "$fehler" -eq 0 ]; then
  echo "SELFTEST_RESULT: PASS adventure_gameplay"
  exit 0
fi
echo "SELFTEST_RESULT: FAIL adventure_gameplay"
echo "---- Output (letzte 40 Zeilen) ----"
echo "$OUT" | tail -40
exit 1

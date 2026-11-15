#!/usr/bin/env bash
# Selbsttest fuer f001_scan.sh (fail-closed): Werkzeugfehler muessen RC 2 liefern, nie einen Nullbefund.
set -uo pipefail
cd "$(dirname "$0")/../../.."
scan=skripte/p2/fehler_hinweis/f001_scan.sh
real="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
fehler=0
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

erwarte() {  # erwarte <name> <rc-soll> <befehl...>
  local name="$1" soll="$2"; shift 2
  "$@" >"$work/out" 2>"$work/err"; local rc=$?
  if [ "$rc" -eq "$soll" ]; then echo "PASS $name (RC $rc)"; else echo "FAIL $name: RC $rc statt $soll"; head -n 5 "$work/err"; fehler=1; fi
}
enthaelt() {  # enthaelt <name> <text> <datei>
  if grep -q -F -- "$2" "$3"; then echo "PASS $1"; else echo "FAIL $1: '$2' fehlt"; fehler=1; fi
}

erwarte "fehlender Pfad" 2 bash "$scan" /nicht/vorhanden
enthaelt "fehlender Pfad: Meldung" "Pfad nicht gefunden" "$work/err"
erwarte "kein Pfad" 2 bash "$scan"
mkdir "$work/leer"
erwarte "Ordner ohne .moos" 2 bash "$scan" "$work/leer"
enthaelt "Ordner ohne .moos: Meldung" "keine .moos-Dateien" "$work/err"
MOO_COMPILER=/bin/true erwarte "Compiler /bin/true" 2 bash "$scan" skripte/p2/diagnose/fehlerpfad_kette.moos
MOO_COMPILER=/bin/false erwarte "Compiler /bin/false" 2 bash "$scan" skripte/p2/diagnose/fehlerpfad_kette.moos
MOO_COMPILER=/nicht/vorhanden erwarte "Compiler fehlt" 2 bash "$scan" skripte/p2/diagnose/fehlerpfad_kette.moos

# Echter Compiler: bekannter M-F001-Fall wird gefunden, Hinweise allein sind RC 0, --streng RC 1.
MOO_COMPILER="$real" erwarte "Fixture fehlerpfad_kette" 0 bash "$scan" skripte/p2/diagnose/fehlerpfad_kette.moos
enthaelt "Fixture: ein M-F001" "m_f001=1" "$work/out"
MOO_COMPILER="$real" erwarte "Fixture --streng" 1 bash "$scan" --streng skripte/p2/diagnose/fehlerpfad_kette.moos
MOO_COMPILER="$real" erwarte "sauberes Programm" 0 bash "$scan" skripte/p2/diagnose/fehlerpfad_still.moos
enthaelt "sauberes Programm: Nullbefund belegt" "dateien=1 m_f001=0 m_f002=0 pruefe_fehler=0 werkzeug_fehler=0" "$work/out"

# Echter Compilefehler bleibt sichtbar: RC 1, mit --erlaube-pruefe-fehler RC 0.
printf 'setze x auf\n' > "$work/kaputt.moos"
MOO_COMPILER="$real" erwarte "Syntaxfehler" 1 bash "$scan" "$work/kaputt.moos"
enthaelt "Syntaxfehler: sichtbar" "PRUEFE-RC1" "$work/out"
MOO_COMPILER="$real" erwarte "Syntaxfehler erlaubt" 0 bash "$scan" --erlaube-pruefe-fehler "$work/kaputt.moos"

[ "$fehler" -eq 0 ] && echo "PASS f001_scan Selbsttest" || { echo "FAIL f001_scan Selbsttest"; exit 1; }

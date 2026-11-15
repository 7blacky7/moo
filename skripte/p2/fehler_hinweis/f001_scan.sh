#!/usr/bin/env bash
# Listet alle M-F001/M-F002-Hinweise (unbehandelter wirf-Pfad / Builtin kann werfen) von
# `moo-compiler pruefe --typpruefung --diagnose=json`. FAIL-CLOSED: ein Nullbefund gilt nur, wenn
# Pfade, Compiler und jede einzelne Diagnose-JSON nachweislich in Ordnung waren.
#
# Aufruf (aus beliebigem Verzeichnis):
#   bash skripte/p2/fehler_hinweis/f001_scan.sh [--erlaube-pruefe-fehler] [--streng] <datei-oder-verzeichnis>...
# Ausgabe je Hinweis, tab-getrennt: code<TAB>datei<TAB>zeile<TAB>funktion<TAB>meldung
# Danach 'SUMME dateien=<n> m_f001=<a> m_f002=<b> pruefe_fehler=<k> werkzeug_fehler=<w>'.
# Dateien mit pruefe-RC 1 (echte Compilefehler, z. B. Kernel-Profile ohne --no-stdlib) erscheinen als
# 'PRUEFE-RC1<TAB>datei' und werden nie verschluckt.
#
# Exitcode:
#   0  Scan vollstaendig und ohne Beanstandung (Hinweise allein sind kein Fehler, ausser mit --streng)
#   1  pruefe-Fehler in Dateien (ausser mit --erlaube-pruefe-fehler) oder Hinweise mit --streng
#   2  Werkzeug-/Aufruffehler: fehlender Pfad, keine .moos-Dateien, jq/find/timeout fehlt, Compiler
#      fehlt/defekt/kennt M-F001 oder M-F002 nicht, Timeout, Signal, ungueltige oder fehlende
#      Diagnose-JSON einer Datei. Dann ist die Ausgabe KEIN gueltiger Nullbefund.
# Compiler ueberschreibbar mit MOO_COMPILER. Vorher/nachher: Ausgaben mit sort + diff vergleichen.
set -uo pipefail
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"

usage() { echo "Aufruf: $0 [--erlaube-pruefe-fehler] [--streng] <datei-oder-verzeichnis>..." >&2; exit 2; }
werkzeug() { echo "WERKZEUG-FEHLER: $*" >&2; }

erlaube_pruefe=false
streng=false
pfade=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --erlaube-pruefe-fehler) erlaube_pruefe=true ;;
    --streng) streng=true ;;
    --) shift; while [ "$#" -gt 0 ]; do pfade+=("$1"); shift; done; break ;;
    -*) werkzeug "unbekannte Option $1"; usage ;;
    *) pfade+=("$1") ;;
  esac
  shift
done
[ "${#pfade[@]}" -gt 0 ] || usage

# --- Voraussetzungen: jedes Versagen ist RC 2, nie ein Nullbefund ---
for cmd in jq find sort timeout mktemp; do
  command -v "$cmd" >/dev/null 2>&1 || { werkzeug "Programm fehlt: $cmd"; exit 2; }
done
fehlt=0
for p in "${pfade[@]}"; do
  [ -e "$p" ] || { werkzeug "Pfad nicht gefunden: $p"; fehlt=1; }
done
[ "$fehlt" -eq 0 ] || exit 2
[ -f "$compiler" ] && [ -x "$compiler" ] || { werkzeug "Compiler fehlt oder nicht ausfuehrbar: $compiler"; exit 2; }
for code in M-F001 M-F002; do
  probe="$(timeout 30s "$compiler" erklaere "$code" 2>&1)"; rc=$?
  if [ "$rc" -ne 0 ] || ! printf '%s' "$probe" | grep -q "$code"; then
    werkzeug "Compiler $compiler kennt $code nicht oder ist defekt (erklaere $code: RC $rc) - neu bauen"
    exit 2
  fi
done

tmp="$(mktemp)"; liste="$(mktemp)"
trap 'rm -f "$tmp" "$liste"' EXIT
find "${pfade[@]}" -type f -name '*.moos' -print0 > "$liste" || { werkzeug "find ist fehlgeschlagen"; exit 2; }
if [ ! -s "$liste" ]; then
  werkzeug "keine .moos-Dateien unter: ${pfade[*]}"
  exit 2
fi

dateien=0; m1=0; m2=0; pruefe_fehler=0; werkzeug_fehler=0
while IFS= read -r -d '' f; do
  dateien=$((dateien + 1))
  timeout 60s "$compiler" pruefe --typpruefung --diagnose=json "$f" >/dev/null 2>"$tmp"
  rc=$?
  if [ "$rc" -eq 124 ]; then
    werkzeug "Zeitueberschreitung (60 s): $f"; werkzeug_fehler=$((werkzeug_fehler + 1)); continue
  elif [ "$rc" -ge 126 ]; then
    werkzeug "Compiler nicht ausfuehrbar oder abgestuerzt (RC $rc): $f"; werkzeug_fehler=$((werkzeug_fehler + 1)); continue
  elif [ "$rc" -ne 0 ] && [ "$rc" -ne 1 ]; then
    werkzeug "unerwarteter Compiler-RC $rc: $f"; werkzeug_fehler=$((werkzeug_fehler + 1)); continue
  fi
  if ! jq -e '.schema == 1 and (.diagnostics | type == "array")' "$tmp" >/dev/null 2>&1; then
    werkzeug "keine gueltige Diagnose-JSON (RC $rc): $f -> $(head -c 160 "$tmp" | tr '\n' ' ')"
    werkzeug_fehler=$((werkzeug_fehler + 1)); continue
  fi
  if [ "$rc" -eq 1 ]; then
    pruefe_fehler=$((pruefe_fehler + 1))
    printf 'PRUEFE-RC1\t%s\n' "$f"
  fi
  a="$(jq -r '[.diagnostics[] | select(.code == "M-F001")] | length' "$tmp")"
  b="$(jq -r '[.diagnostics[] | select(.code == "M-F002")] | length' "$tmp")"
  m1=$((m1 + a)); m2=$((m2 + b))
  if [ $((a + b)) -gt 0 ]; then
    jq -r --arg f "$f" '.diagnostics[] | select(.code == "M-F001" or .code == "M-F002") | [.code, (.file // $f), .line, (.function // ""), .message] | @tsv' "$tmp"
  fi
done < "$liste"

printf 'SUMME dateien=%s m_f001=%s m_f002=%s pruefe_fehler=%s werkzeug_fehler=%s\n' "$dateien" "$m1" "$m2" "$pruefe_fehler" "$werkzeug_fehler"
[ "$dateien" -gt 0 ] || { werkzeug "keine Datei ausgewertet"; exit 2; }
[ "$werkzeug_fehler" -eq 0 ] || exit 2
if [ "$pruefe_fehler" -gt 0 ] && [ "$erlaube_pruefe" = false ]; then exit 1; fi
if [ "$streng" = true ] && [ $((m1 + m2)) -gt 0 ]; then exit 1; fi
exit 0

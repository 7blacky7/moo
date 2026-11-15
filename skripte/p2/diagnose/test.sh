#!/usr/bin/env bash
# P2-T97 (Plan 00fbd28f, Entscheid #38204): einheitliches Diagnoseformat.
# Prueft Kopfzeile datei:zeile:spalte + Code, Sammeln mehrerer Fehler eines
# Laufs, erwartet/gefunden/Vorschlag, Hinweise (RC 0), --diagnose=json (jq,
# englische Schluessel), pruefe/check ohne Ausgabedatei, erklaere/explain und
# den Altfall compiler/tests/p2/import_kern_unbekannt (Meldung ohne Praefix).
set -uo pipefail
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-compiler/target/release/moo-compiler}"
d=skripte/p2/diagnose
out=build/p2-diagnose
mkdir -p "$out"
fehler=0

fail() { echo "FAIL $1"; fehler=1; }
# pruef <name> [args]: Text-Lauf, stderr in $out/<name>.err, RC in $rc.
pruef() {
  local name="$1"; shift
  timeout 120s "$compiler" pruefe "$d/$name.moos" "$@" > "$out/$name.out" 2> "$out/$name.err"
  rc=$?
}
# pruef_json <name>: JSON-Lauf, stderr in $out/<name>.json.
pruef_json() {
  local name="$1"; shift
  timeout 120s "$compiler" pruefe "$d/$name.moos" --diagnose=json "$@" > "$out/$name.json.out" 2> "$out/$name.json"
  rc=$?
}
hat() { rg -q -F -- "$2" "$out/$1.err" || { fail "$1: stderr ohne '$2'"; cat "$out/$1.err"; }; }
json() { jq -e "$2" "$out/$1.json" > /dev/null || { fail "$1: JSON-Bedingung $2"; cat "$out/$1.json"; }; }

# 1. Mehrere Syntaxfehler eines Laufs, Zeile:Spalte, Block eines kaputten Kopfs ohne Folgefehler
pruef syntax_mehrfach
[ "$rc" -eq 1 ] || fail "syntax_mehrfach: RC $rc statt 1"
hat syntax_mehrfach "$d/syntax_mehrfach.moos:2:15: Fehler M-S001: Doppelpunkt ':' erwartet, aber Zeilenende gefunden."
hat syntax_mehrfach "    2 |     wenn x > 1"
hat syntax_mehrfach "      |               ^"
hat syntax_mehrfach "$d/syntax_mehrfach.moos:4:13: Fehler M-S001: 'auf' / 'to' erwartet, aber Zahl 5 gefunden."
hat syntax_mehrfach "erwartet: 'auf' / 'to' | gefunden: Zahl 5"
hat syntax_mehrfach "$d/syntax_mehrfach.moos:10:"
hat syntax_mehrfach "3 Fehler"
pruef_json syntax_mehrfach
json syntax_mehrfach '.schema == 1 and .errors == 3 and .hints == 0 and (.diagnostics | length) == 3'
json syntax_mehrfach '[.diagnostics[].line] == [2, 4, 10]'
json syntax_mehrfach '.diagnostics[0] | .code == "M-S001" and .severity == "error" and .column == 15 and .expected == "Doppelpunkt '"'"':'"'"'" and .found == "Zeilenende" and (.suggestion | test("muss ein"))'
json syntax_mehrfach '.diagnostics[0] | keys == ["code","column","end_column","expected","file","found","function","line","message","severity","suggestion"]'

# 2. Lexerfehler: alle Zeilen, mit Spalte
pruef lexer_zeichen
[ "$rc" -eq 1 ] || fail "lexer_zeichen: RC $rc statt 1"
hat lexer_zeichen "$d/lexer_zeichen.moos:2:15: Fehler M-L001: Unbekanntes Zeichen: '\$'"
hat lexer_zeichen "$d/lexer_zeichen.moos:3:"
hat lexer_zeichen "Fehler M-L002: String nicht geschlossen"
hat lexer_zeichen "2 Fehler"

# 3. Import: Modul definiert den Namen nicht (M-I002) mit Datei:Zeile
pruef import_unbekannt
[ "$rc" -eq 1 ] || fail "import_unbekannt: RC $rc statt 1"
hat import_unbekannt "$d/import_unbekannt.moos:1: Fehler M-I002: Modul './modul_diag' definiert 'fehlt' nicht"
# Altfall (Fixture in compiler/tests/p2): die Meldung
# ohne ihr altes Praefix 'datei:zeile: ' muss weiter erscheinen, jetzt mit Code.
alt=$(cat compiler/tests/p2/import_kern_unbekannt.expected_error)
ohne_praefix="${alt#*: }"
timeout 120s "$compiler" pruefe compiler/tests/p2/import_kern_unbekannt.moos > "$out/import_kern_unbekannt.out" 2> "$out/import_kern_unbekannt.err"
rc=$?
[ "$rc" -eq 1 ] || fail "import_kern_unbekannt: RC $rc statt 1"
hat import_kern_unbekannt "import_kern_unbekannt.moos:1: Fehler M-I002: $ohne_praefix"

# 4. Codegen: Funktion unbekannt mit Zeile und Vorschlag
pruef funktion_unbekannt
[ "$rc" -eq 1 ] || fail "funktion_unbekannt: RC $rc statt 1"
hat funktion_unbekannt "$d/funktion_unbekannt.moos:4: Fehler M-C001: Funktion 'rechen' nicht gefunden. Meintest du 'rechne'?"
pruef_json funktion_unbekannt
json funktion_unbekannt '.diagnostics[0] | .code == "M-C001" and .line == 4 and .found == "rechen" and .suggestion == "Meintest du '"'"'rechne'"'"'?"'

# 5. Hinweis: kein Fehler, RC 0, Programm wird erzeugt
pruef hinweis
[ "$rc" -eq 0 ] || fail "hinweis: RC $rc statt 0"
hat hinweis "Hinweis M-H001: 'zahl' ist vom Typ Zahl, aber '.append()' ist nicht fuer Zahl definiert"
hat hinweis "1 Hinweis"
pruef_json hinweis
json hinweis '.errors == 0 and .hints == 1 and .diagnostics[0].severity == "hint" and .diagnostics[0].code == "M-H001"'
# B5c: Ausdrucks-Statement traegt die eigene Zeile (vorher line null).
hat hinweis "$d/hinweis.moos:2: Hinweis M-H001:"
json hinweis '.diagnostics[0].line == 2'
timeout 120s "$compiler" compile "$d/hinweis.moos" -o "$out/hinweis_bin" > "$out/hinweis.compile.log" 2>&1 \
  || { fail "hinweis: compile RC $?"; cat "$out/hinweis.compile.log"; }
[ -x "$out/hinweis_bin" ] || fail "hinweis: keine Binary trotz nur Hinweis"

# 5b. B5b: Zeile aus dem Statement selbst (Zuweisung, Block-Inneres), Text + JSON
pruef zeile_zuweisung
[ "$rc" -eq 0 ] || fail "zeile_zuweisung: RC $rc statt 0"
hat zeile_zuweisung "$d/zeile_zuweisung.moos:2: Hinweis M-H001:"
hat zeile_zuweisung "    2 | setze x auf zahl.append(1)"
pruef_json zeile_zuweisung
json zeile_zuweisung '.hints == 1 and .diagnostics[0].line == 2'
pruef zeile_block
[ "$rc" -eq 1 ] || fail "zeile_block: RC $rc statt 1"
hat zeile_block "$d/zeile_block.moos:3: Fehler M-C000: Variable 'unbekannt_x' nicht gefunden"
hat zeile_block "    3 |     setze b auf unbekannt_x"
pruef_json zeile_block
json zeile_block '.errors == 1 and .diagnostics[0].line == 3'
# B5c: alle Stmt-Varianten mit Zeile, hier zeige und gib_zurueck.
pruef zeile_zeige
[ "$rc" -eq 1 ] || fail "zeile_zeige: RC $rc statt 1"
hat zeile_zeige "$d/zeile_zeige.moos:2: Fehler M-C000: Variable 'unbekannt_y' nicht gefunden"
hat zeile_zeige "    2 | zeige unbekannt_y"
pruef_json zeile_zeige
json zeile_zeige '.errors == 1 and .diagnostics[0].line == 2'
pruef zeile_rueckgabe
[ "$rc" -eq 1 ] || fail "zeile_rueckgabe: RC $rc statt 1"
hat zeile_rueckgabe "$d/zeile_rueckgabe.moos:3: Fehler M-C000: Variable 'unbekannt_r' nicht gefunden"
pruef_json zeile_rueckgabe
json zeile_rueckgabe '.errors == 1 and .diagnostics[0].line == 3 and .diagnostics[0].function == "f"'

# 5c. P2-T29 S5: ueberzaehlige Tokens nach einem Statement (vorher still
# als zweites Statement gelesen), auch im Block; Recovery bis Zeilenende.
pruef ueberzaehlig
[ "$rc" -eq 1 ] || fail "ueberzaehlig: RC $rc statt 1"
hat ueberzaehlig "$d/ueberzaehlig.moos:1:15: Fehler M-S003: Zeilenende erwartet, aber 'über' gefunden."
hat ueberzaehlig "$d/ueberzaehlig.moos:4:25: Fehler M-S003: Zeilenende erwartet, aber Zahl 5 gefunden."
hat ueberzaehlig "2 Fehler"
pruef_json ueberzaehlig
json ueberzaehlig '.errors == 2 and [.diagnostics[].line] == [1, 4] and (.diagnostics[0] | .code == "M-S003" and .expected == "Zeilenende" and .found == "'"'"'über'"'"'")'
timeout 30s "$compiler" erklaere M-S003 > "$out/erklaere_s003.out" 2>&1 || fail "erklaere M-S003: RC $?"
rg -q -F "M-S003 – Zeilenende erwartet" "$out/erklaere_s003.out" || fail "erklaere M-S003: Eintrag fehlt"

# 6. pruefe/check: ohne Fehler RC 0, keine Ausgabedatei, JSON immer genau ein Objekt
vorher=$(ls -A "$d" | sort | tr '\n' ' ')
pruef ok
[ "$rc" -eq 0 ] || fail "ok: RC $rc statt 0"
[ -s "$out/ok.err" ] && { fail "ok: stderr nicht leer"; cat "$out/ok.err"; }
rg -q -F "(keine Fehler)" "$out/ok.out" || fail "ok: Erfolgsmeldung fehlt"
timeout 120s "$compiler" check "$d/ok.moos" --diagnose=json > "$out/ok.json.out" 2> "$out/ok.json" || fail "ok: check-Alias RC $?"
[ "$(cat "$out/ok.json")" = '{"schema":1,"diagnostics":[],"errors":0,"hints":0}' ] || { fail "ok: JSON"; cat "$out/ok.json"; }
[ -s "$out/ok.json.out" ] && fail "ok: JSON-Modus schreibt auf stdout"
nachher=$(ls -A "$d" | sort | tr '\n' ' ')
[ "$vorher" = "$nachher" ] || fail "pruefe hat Dateien in $d erzeugt"

# 7. erklaere/explain und Formatfehler
timeout 30s "$compiler" erklaere M-S001 > "$out/erklaere.out" 2>&1 || fail "erklaere: RC $?"
rg -q -F "M-S001 – Token erwartet" "$out/erklaere.out" || fail "erklaere: Eintrag fehlt"
timeout 30s "$compiler" explain m-c001 > "$out/explain.out" 2>&1 || fail "explain: RC $?"
rg -q -F "M-C001 – Funktion unbekannt" "$out/explain.out" || fail "explain: Eintrag fehlt"
timeout 30s "$compiler" erklaere M-X999 > "$out/erklaere_unbekannt.out" 2> "$out/erklaere_unbekannt.err"
rc=$?
[ "$rc" -eq 1 ] || fail "erklaere unbekannt: RC $rc statt 1"
hat erklaere_unbekannt "Fehler M-W000: Unbekannter Diagnosecode 'M-X999'"
timeout 30s "$compiler" pruefe "$d/ok.moos" --diagnose=xml > /dev/null 2> "$out/format.err"
rc=$?
[ "$rc" -eq 2 ] || fail "--diagnose=xml: RC $rc statt 2"

if [ "$fehler" -ne 0 ]; then echo "FAIL P2-T97 diagnose"; exit 1; fi
echo "PASS P2-T97 diagnose: mehrere Syntax-/Lexerfehler mit Zeile:Spalte, Import M-I002 (inkl. Altfall), M-C001 mit Zeile+Vorschlag, Hinweis RC0, Stmt-Zeilen (B5b/B5c), JSON-Schema, pruefe/check, erklaere/explain"

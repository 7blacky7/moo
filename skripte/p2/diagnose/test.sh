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

# 9. P2-T99: derselbe Top-Level-Name aus zwei Dateien (M-I004 Fehler,
# M-I005 Hinweis bei Variablen); Rautenimport still; stdlib entdoppelt.
pruef kollision_nutzer
[ "$rc" -eq 1 ] || fail "kollision_nutzer: RC $rc statt 1"
hat kollision_nutzer "$d/kollision_nutzer.moos:2: Fehler M-I004: Funktion 'hilfe' ist schon im Modul $d/kollision_modul_a.moos:2 definiert (Funktion)"
hat kollision_nutzer "Vorschlag: Eigene Funktion umbenennen"
pruef kollision_selektiv
[ "$rc" -eq 1 ] || fail "kollision_selektiv: RC $rc statt 1"
hat kollision_selektiv "$d/kollision_selektiv.moos:2: Fehler M-I004: Funktion 'hilfe' ist schon im Modul $d/kollision_modul_a.moos:2 definiert"
pruef_json kollision_modul_modul
[ "$rc" -eq 1 ] || fail "kollision_modul_modul: RC $rc statt 1"
json kollision_modul_modul '.errors == 2 and .hints == 0 and ([.diagnostics[].code] | unique) == ["M-I004"]'
json kollision_modul_modul '[.diagnostics[] | (.file | split("/") | last) + ":" + (.line|tostring)] == ["kollision_modul_modul.moos:3", "kollision_modul_b.moos:2"]'
json kollision_modul_modul '.diagnostics[1].found == "skripte/p2/diagnose/kollision_modul_a.moos:2"'
pruef_json kollision_variable
[ "$rc" -eq 0 ] || fail "kollision_variable: RC $rc statt 0"
json kollision_variable '.errors == 0 and .hints == 1 and .diagnostics[0].code == "M-I005" and .diagnostics[0].line == 2'
pruef_json kollision_raute
[ "$rc" -eq 0 ] || fail "kollision_raute: RC $rc statt 0"
json kollision_raute '.errors == 0 and .hints == 0'
rm -f "$out/kollision.bin"
timeout 120s "$compiler" compile "$d/kollision_nutzer.moos" -o "$out/kollision.bin" > "$out/kollision_compile.out" 2> "$out/kollision_compile.err"
rc=$?
[ "$rc" -eq 1 ] || fail "compile kollision_nutzer: RC $rc statt 1"
[ ! -e "$out/kollision.bin" ] || fail "compile kollision_nutzer: Binary trotz M-I004"
timeout 120s "$compiler" pruefe compiler/tests/test_stdlib_voll.moos --diagnose=json > "$out/stdlib_voll.out" 2> "$out/stdlib_voll.json"
rc=$?
[ "$rc" -eq 0 ] || fail "test_stdlib_voll (mathe+primzahl): RC $rc statt 0"
json stdlib_voll '.errors == 0 and ([.diagnostics[] | select(.code == "M-I004" or .code == "M-I005")] | length) == 0'
# 9b. Review 9502d95c: eine zuerst gesehene Variable verdeckt die Funktions-
# Kollision zweier Module nicht (beide Importreihenfolgen); Struktur gegen
# Funktion zaehlt; primzahl gibt ggt/ist_primzahl/klemme aus mathe weiter.
for v in zuerst:kollision_modul_b.moos:kollision_modul_a.moos spaet:kollision_modul_a.moos:kollision_modul_b.moos; do
  name="kollision_variable_${v%%:*}"; rest="${v#*:}"; ort="${rest%%:*}"; anderer="${rest#*:}"
  pruef_json "$name"
  [ "$rc" -eq 1 ] || fail "$name: RC $rc statt 1"
  json "$name" '.errors == 1 and .hints == 1'
  json "$name" "[.diagnostics[] | .code + \"@\" + (.file | split(\"/\") | last) + \":\" + (.line|tostring)] | sort == [\"M-I004@$ort:2\", \"M-I005@$name.moos:3\"]"
  json "$name" "[.diagnostics[] | select(.code == \"M-I004\") | .found] == [\"$d/$anderer:2\"]"
  rm -f "$out/$name.bin"
  timeout 120s "$compiler" compile "$d/$name.moos" -o "$out/$name.bin" > /dev/null 2>&1
  [ ! -e "$out/$name.bin" ] || fail "$name: Binary trotz M-I004"
done
pruef kollision_struktur
[ "$rc" -eq 1 ] || fail "kollision_struktur: RC $rc statt 1"
hat kollision_struktur "$d/kollision_struktur.moos:2: Fehler M-I004: Funktion 'Stern' ist schon im Modul $d/kollision_modul_s.moos:2 definiert (Struktur)"
for name in primzahl_weitergabe primzahl_mit_mathe; do
  pruef_json "$name"
  [ "$rc" -eq 0 ] || fail "$name: RC $rc statt 0"
  json "$name" '.errors == 0 and .hints == 0'
done
rm -f "$out/primzahl_weitergabe.bin"
timeout 120s "$compiler" compile "$d/primzahl_weitergabe.moos" -o "$out/primzahl_weitergabe.bin" > /dev/null 2>&1 || fail "primzahl_weitergabe: compile RC $?"
[ "$(timeout 30s "$out/primzahl_weitergabe.bin" 2>&1 | tr '\n' ' ')" = "6 wahr 3 " ] || fail "primzahl_weitergabe: Ausgabe $(timeout 30s "$out/primzahl_weitergabe.bin" 2>&1 | tr '\n' ' ')"
# 9c. Review 2be97766: ein Zyklus selektiver Weitergaben ohne echte Definition
# ist M-I002; ein gueltiger Zyklus (Definition am Ende der Kette) bleibt still.
pruef_json zyklus_ohne_definition
[ "$rc" -eq 1 ] || fail "zyklus_ohne_definition: RC $rc statt 1"
json zyklus_ohne_definition '.errors >= 1 and ([.diagnostics[].code] | index("M-I002")) != null'
pruef_json zyklus_gueltig
[ "$rc" -eq 0 ] || fail "zyklus_gueltig: RC $rc statt 0"
json zyklus_gueltig '.errors == 0 and .hints == 0'
rm -f "$out/zyklus_gueltig.bin"
timeout 120s "$compiler" compile "$d/zyklus_gueltig.moos" -o "$out/zyklus_gueltig.bin" > /dev/null 2>&1 || fail "zyklus_gueltig: compile RC $?"
[ "$(timeout 30s "$out/zyklus_gueltig.bin" 2>&1 | tr '\n' ' ')" = "7 1 " ] || fail "zyklus_gueltig: Ausgabe $(timeout 30s "$out/zyklus_gueltig.bin" 2>&1 | tr '\n' ' ')"
timeout 30s "$compiler" erklaere M-I004 > "$out/erklaere_i4.out" 2>&1 || fail "erklaere M-I004: RC $?"
rg -q -F "M-I004 – Doppelte Definition aus zwei Dateien" "$out/erklaere_i4.out" || fail "erklaere M-I004: Eintrag fehlt"

if [ "$fehler" -ne 0 ]; then echo "FAIL P2-T97 diagnose"; exit 1; fi
echo "PASS P2-T97 diagnose: mehrere Syntax-/Lexerfehler mit Zeile:Spalte, Import M-I002 (inkl. Altfall), M-C001 mit Zeile+Vorschlag, Hinweis RC0, Stmt-Zeilen (B5b/B5c), JSON-Schema, pruefe/check, erklaere/explain; P2-T99 M-I004 (Benutzer/selektiv/Modul-Modul, compile ohne Binary), M-I005 Variable, Raute still, stdlib mathe+primzahl; Variable verdeckt nichts (beide Reihenfolgen), Struktur/Funktion, primzahl-Weitergabe; Weitergabe-Zyklus ohne Definition M-I002, gueltiger Zyklus RC 0"

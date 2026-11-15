#!/usr/bin/env bash
# P2-T29 S1 (Entwurf a393ce15): Typpruefung zur Compilezeit fuer dynamischen
# Code. M-T001 (Annotation bei Zuweisung, Konstante, Neuzuweisung,
# Standardwert), M-T003 (gib_zurueck gegen -> T), Datei je Modul, eigene
# Klasse vor eingebautem Typnamen, keine Meldung bei passenden Typen.
set -uo pipefail
cd "$(dirname "$0")/../../.."
compiler="${MOO_COMPILER:-${P2_COMPILER:-compiler/target/release/moo-compiler}}"
d=skripte/p2/typen
out=build/p2-typen
mkdir -p "$out"
fehler=0

fail() { echo "FAIL $1"; fehler=1; }
pruef() {
  local name="$1"; shift
  timeout 120s "$compiler" pruefe --typpruefung "$d/$name.moos" "$@" > "$out/$name.out" 2> "$out/$name.err"
  rc=$?
}
pruef_json() {
  local name="$1"; shift
  timeout 120s "$compiler" pruefe --typpruefung "$d/$name.moos" --diagnose=json "$@" > "$out/$name.json.out" 2> "$out/$name.json"
  rc=$?
}
hat() { rg -q -F -- "$2" "$out/$1.err" || { fail "$1: stderr ohne '$2'"; cat "$out/$1.err"; }; }
ohne() { if rg -q -F -- "$2" "$out/$1.err"; then fail "$1: stderr mit '$2'"; cat "$out/$1.err"; fi; }
json() { jq -e "$2" "$out/$1.json" > /dev/null || { fail "$1: JSON-Bedingung $2"; cat "$out/$1.json"; }; }

# 1. Passende Annotationen (inkl. Ableitung ueber Variable, Operator, unbekanntem Aufruf)
pruef ok
[ "$rc" -eq 0 ] || fail "ok: RC $rc statt 0"
ohne ok "M-T"
pruef_json ok
json ok '.errors == 0 and .hints == 0'

# 1b. Keine Fehlalarme (Review 08e9b482): spaetere Annotation, global mutiert, Zweig, Schleife
pruef fluss
[ "$rc" -eq 0 ] || fail "fluss: RC $rc statt 0"
ohne fluss "M-T"

# 2. Eigene Klasse Zahl geht vor dem eingebauten Typnamen; Objekt darf nichts sein
pruef eigene_klasse
[ "$rc" -eq 0 ] || fail "eigene_klasse: RC $rc statt 0"
ohne eigene_klasse "M-T"

# 3. M-T001: Zuweisung, Konstante, Neuzuweisung, Standardwert (alle in einem Lauf)
pruef zuweisung
[ "$rc" -eq 1 ] || fail "zuweisung: RC $rc statt 1"
hat zuweisung "$d/zuweisung.moos:2:10: Fehler M-T001: 'n' ist als Zahl annotiert, der Wert ist aber Text"
hat zuweisung "erwartet: Zahl | gefunden: Text"
hat zuweisung "Vorschlag: Wert anpassen oder die Annotation auf Text ändern"
hat zuweisung "$d/zuweisung.moos:3:17: Fehler M-T001: 'NAME' ist als Text annotiert, der Wert ist aber Zahl"
hat zuweisung "$d/zuweisung.moos:6: Fehler M-T001: 'z' ist als Zahl deklariert, die Zuweisung liefert aber Text"
hat zuweisung "$d/zuweisung.moos:8:15: Fehler M-T001: Standardwert von Parameter 'x' ist Text, annotiert ist Zahl"
hat zuweisung "4 Fehler"
ohne zuweisung "'ok'"
[ ! -e "$out/zuweisung" ] || fail "zuweisung: Ausgabedatei entstanden"
pruef_json zuweisung
json zuweisung '.errors == 4 and .hints == 0 and ([.diagnostics[].code] | unique) == ["M-T001"]'
json zuweisung '[.diagnostics[].line] == [2, 3, 6, 8]'
json zuweisung '.diagnostics[0] | .column == 10 and .end_column == 14 and .expected == "Zahl" and .found == "Text"'
json zuweisung '.diagnostics[3].function == "f"'

# 4. M-T003: falscher Rueckgabewert und gib_zurueck ohne Wert
pruef rueckgabe
[ "$rc" -eq 1 ] || fail "rueckgabe: RC $rc statt 1"
hat rueckgabe "$d/rueckgabe.moos:3: Fehler M-T003: 'name' gibt laut Signatur Text zurück, hier aber Zahl"
hat rueckgabe "$d/rueckgabe.moos:8: Hinweis M-T003: 'zahl_oder_nichts' gibt laut Signatur Zahl zurück, hier aber Nichts"
hat rueckgabe "1 Fehler, 1 Hinweis"
pruef_json rueckgabe
json rueckgabe '[.diagnostics[] | .code + ":" + (.line|tostring) + ":" + .function] == ["M-T003:3:name", "M-T003:8:zahl_oder_nichts"]'

# 5. Typfehler im importierten Modul: Meldung nennt die Moduldatei
pruef import_typ
[ "$rc" -eq 1 ] || fail "import_typ: RC $rc statt 1"
hat import_typ "$d/typ_modul.moos:3: Fehler M-T003: 'modul_wert' gibt laut Signatur Zahl zurück, hier aber Text"

# 6. compile bricht vor dem Codegen ab (kein Binary)
rm -f "$out/zuweisung.bin"
timeout 120s "$compiler" compile --typpruefung "$d/zuweisung.moos" -o "$out/zuweisung.bin" > "$out/compile.out" 2> "$out/compile.err"
rc=$?
[ "$rc" -eq 1 ] || fail "compile zuweisung: RC $rc statt 1"
[ ! -e "$out/zuweisung.bin" ] || fail "compile zuweisung: Binary trotz Typfehler"
hat compile "Fehler M-T001"

# 6b. Ohne --typpruefung (Standard) keine Typpruefung: RC 0, keine M-T-Meldung
timeout 120s "$compiler" pruefe "$d/zuweisung.moos" > "$out/aus.out" 2> "$out/aus.err"
rc=$?
[ "$rc" -eq 0 ] || fail "ohne --typpruefung: RC $rc statt 0"
ohne aus "M-T"

# 6c. S2: Aufrufe gegen Signaturen (M-T002 Fehler, nichts/M-T004 Hinweis)
pruef aufruf
[ "$rc" -eq 1 ] || fail "aufruf: RC $rc statt 1"
hat aufruf "$d/aufruf.moos:12: Fehler M-T002: Argument 1 ('breite') von 'flaeche' ist Text, erwartet Zahl"
hat aufruf "$d/aufruf.moos:13: Hinweis M-T004: 'flaeche' erwartet mindestens 2 Argumente, bekommt 1"
hat aufruf "$d/aufruf.moos:14: Hinweis M-T004: 'flaeche' erwartet höchstens 2 Argumente, bekommt 3"
hat aufruf "$d/aufruf.moos:16: Hinweis M-T002: Argument 1 ('name') von 'gruss' ist Nichts, erwartet Text"
hat aufruf "$d/aufruf.moos:18: Fehler M-T002: Argument 1 ('betrag') von 'Konto.einzahlen' ist Text, erwartet Zahl"
hat aufruf "2 Fehler, 3 Hinweise"
ohne aufruf "aufruf.moos:15:"
pruef aufruf_ok
[ "$rc" -eq 0 ] || fail "aufruf_ok: RC $rc statt 0"
ohne aufruf_ok "M-T"

# 7. Katalog
timeout 30s "$compiler" erklaere M-T001 > "$out/erklaere.out" 2>&1 || fail "erklaere M-T001: RC $?"
rg -q -F "M-T001 – Typ passt nicht zur Annotation" "$out/erklaere.out" || fail "erklaere M-T001: Eintrag fehlt"
timeout 30s "$compiler" erklaere M-T003 > "$out/erklaere3.out" 2>&1 || fail "erklaere M-T003: RC $?"
rg -q -F "M-T003 – Rueckgabetyp passt nicht" "$out/erklaere3.out" || fail "erklaere M-T003: Eintrag fehlt"

if [ "$fehler" -ne 0 ]; then echo "FAIL P2-T29 typen"; exit 1; fi
echo "PASS P2-T29 typen S1: M-T001 (Zuweisung, Konstante, Neuzuweisung, Standardwert), M-T003 (Wert, ohne Wert), Moduldatei, eigene Klasse vor Typname, compile ohne Binary, erklaere"

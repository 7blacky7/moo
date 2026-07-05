#!/usr/bin/env bash
# ============================================================
# Grimm-Maerchen-Korpus herunterladen + extrahieren (Plan-014 G1).
# Quelle: Project Gutenberg #77905 "Deutsche Maerchen gesammelt
# durch die Brueder Grimm" (Ausgabe Langewiesche-Brandt 1921,
# Text gemeinfrei — Jacob Grimm +1863, Wilhelm Grimm +1859).
# SHA256-Doppel-Pinning: Rohdatei UND extrahiertes Ergebnis wurden
# beim Erst-Download am 2026-07-05 gemessen und hier fixiert —
# jede spaetere Abweichung (Quelle geaendert, Extraktion driftet)
# bricht ab. Damit ist das Trainings-Gate von ki_sprachmodell.moo
# byte-genau reproduzierbar.
# Extraktion: PG-Boilerplate weg, CRLF->LF, Zeilen 57-1300 des Kerns
# (= 9 vollstaendige Maerchen: Marienkind, Die Wichtelmaenner,
# Der treue Johannes, Die drei Maennlein im Walde, Der Froschkoenig,
# Der Wolf und die sieben jungen Geisslein, Die Scholle,
# Der Hase und der Igel, Die Bremer Stadtmusikanten),
# [Illustration]-Zeilen raus, Sperrsatz-Marker ~ raus,
# Mehrfach-Leerzeilen kollabiert, Leerzeilen am Ende weg.
# Ergebnis: ~72k Zeichen (UTF-8-Codepoints), Vokabular 70 Zeichen.
# Ziel: beispiele/daten/maerchen.txt (NICHT im Git — siehe .gitignore).
# ============================================================
set -euo pipefail

URL="https://www.gutenberg.org/cache/epub/77905/pg77905.txt"
ROH_SOLL="3cc443c1f71bc23a6a33f566b8de3dca7c49604d81be1696d1aef5137c579e3f"
ZIEL_SOLL="3770d821afd03adb39f1a1e042b87f7f4a4fb35e3c5add6ed17811d63c941e6a"

ZIEL_DIR="$(dirname "$0")/../beispiele/daten"
ZIEL="$ZIEL_DIR/maerchen.txt"
mkdir -p "$ZIEL_DIR"

if [[ -f "$ZIEL" ]]; then
  ist="$(sha256sum "$ZIEL" | cut -d' ' -f1)"
  if [[ "$ist" == "$ZIEL_SOLL" ]]; then
    echo "OK (vorhanden + verifiziert): $ZIEL"
    exit 0
  fi
  echo "HINWEIS: $ZIEL vorhanden, aber SHA weicht ab — wird neu gebaut."
fi

ROH="$(mktemp)"
trap 'rm -f "$ROH"' EXIT

echo "Lade Grimm-Maerchen (PG #77905) ..."
# --http1.1: gutenberg.org bricht HTTP/2-Streams gelegentlich ab (curl rc=92).
curl -fsSL --http1.1 "$URL" -o "$ROH"

ist="$(sha256sum "$ROH" | cut -d' ' -f1)"
if [[ "$ist" != "$ROH_SOLL" ]]; then
  echo "FEHLER: SHA256 der Rohdatei stimmt nicht!" >&2
  echo "  erwartet: $ROH_SOLL" >&2
  echo "  gemessen: $ist" >&2
  exit 1
fi

# Kern = zwischen den ***-START/END-Markern (Zeilen 34-8907 der Rohdatei),
# dann der fixierte Maerchen-Bereich. cat -s kollabiert Leerzeilen-Bloecke,
# das sed-Finale entfernt Leerzeilen am Dateiende.
sed -n '34,8907p' "$ROH" \
  | tr -d '\r' \
  | sed -n '57,1300p' \
  | grep -v '\[Illustration' \
  | tr -d '~' \
  | cat -s \
  | sed -e :a -e '/^\s*$/{$d;N;ba' -e '}' \
  > "$ZIEL"

ist="$(sha256sum "$ZIEL" | cut -d' ' -f1)"
if [[ "$ist" != "$ZIEL_SOLL" ]]; then
  echo "FEHLER: SHA256 des extrahierten Korpus stimmt nicht!" >&2
  echo "  erwartet: $ZIEL_SOLL" >&2
  echo "  gemessen: $ist" >&2
  rm -f "$ZIEL"
  exit 1
fi

echo "Korpus bereit + verifiziert: $ZIEL ($(wc -c < "$ZIEL") Bytes)"
echo "Nutzung in moo:  siehe beispiele/ki_sprachmodell.moo"

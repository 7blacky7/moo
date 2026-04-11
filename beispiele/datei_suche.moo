# moo Dateisuche — durchsucht Dateien nach Text
# Starten: moo-compiler run beispiele/datei_suche.moo

zeige "=== moo Dateisuche ==="

setze suchtext auf eingabe("Suchtext: ")
setze dateiname auf eingabe("Datei: ")

setze inhalt auf datei_lesen(dateiname)
setze zeilen auf inhalt.teilen("\n")
setze treffer auf 0

setze nr auf 1
für zeile in zeilen:
    wenn zeile.text_enthält(suchtext):
        zeige nr + ": " + zeile
        setze treffer auf treffer + 1
    setze nr auf nr + 1

zeige ""
zeige text(treffer) + " Treffer gefunden."

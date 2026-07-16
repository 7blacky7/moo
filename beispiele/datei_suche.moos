# moo Dateisuche — sucht Text in einer Datei
# Starten: moo-compiler run beispiele/datei_suche.moo

zeige "=== moo Dateisuche ==="

setze dateiname auf eingabe("Datei: ")
setze suchtext auf eingabe("Suche: ")

setze inhalt auf datei_lesen(dateiname)
setze zeilen auf inhalt.teilen("\n")
setze treffer auf 0
setze nr auf 1

für zeile in zeilen:
    wenn zeile.str_contains(suchtext):
        zeige text(nr) + ": " + zeile
        setze treffer auf treffer + 1
    setze nr auf nr + 1

zeige ""
zeige text(treffer) + " Treffer."

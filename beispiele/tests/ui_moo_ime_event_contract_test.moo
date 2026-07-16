# P016-O4: toolkitfreier oeffentlicher IME-Ereignisvertrag.
# Reiner Wertvertrag; kein Fenster, Hostadapter, Fokus oder native Eingabe.
importiere ui_moo_kern

setze s auf {}
s["fehler"] = 0
s["checks"] = 0

funktion pruef(name, ok):
    s["checks"] = s["checks"] + 1
    wenn ok:
        zeige "PASS " + name
    sonst:
        s["fehler"] = s["fehler"] + 1
        zeige "FAIL " + name
    gib_zurück ok

# Exakt ein noch fehlender oeffentlicher API-Aufruf: dadurch ist RED eindeutig.
funktion ime(art, inhalt, auswahl_start, auswahl_ende, revision):
    gib_zurück uim_ime_ereignis(art, inhalt, auswahl_start, auswahl_ende, revision)

pruef("invalid-art-fail-closed", ime("update", "Moo", 0, 3, 1) == nichts)
pruef("invalid-text-type-fail-closed", ime("preedit", {}, 0, 0, 1) == nichts)
pruef("invalid-revision-zero-fail-closed", ime("preedit", "Moo", 0, 3, 0) == nichts)
pruef("invalid-revision-negative-fail-closed", ime("preedit", "Moo", 0, 3, -1) == nichts)
pruef("invalid-revision-fraction-fail-closed", ime("preedit", "Moo", 0, 3, 1.5) == nichts)
pruef("invalid-revision-type-fail-closed", ime("preedit", "Moo", 0, 3, "1") == nichts)
pruef("invalid-selection-type-fail-closed", ime("preedit", "Moo", "0", 3, 1) == nichts)
pruef("invalid-negative-selection-fail-closed", ime("preedit", "Moo", -1, 3, 1) == nichts)
pruef("invalid-reversed-selection-fail-closed", ime("preedit", "Moo", 2, 1, 1) == nichts)
pruef("invalid-out-of-range-fail-closed", ime("preedit", "Moo", 0, 4, 1) == nichts)

# UTF-8-Byteoffsets: "äb" hat drei Bytes; Offset 1 liegt im Fortsetzungsbyte.
pruef("invalid-start-continuation-byte-boundary", ime("preedit", "äb", 1, 3, 1) == nichts)
pruef("invalid-end-continuation-byte-boundary", ime("preedit", "äb", 0, 1, 1) == nichts)

setze preedit auf ime("preedit", "äb", 2, 3, 1)
pruef("preedit-schema-v1-exact", typ_von(preedit) == "Woerterbuch" und länge(preedit) == 6 und preedit.enthält("version") und preedit.enthält("art") und preedit.enthält("text") und preedit.enthält("selection_start") und preedit.enthält("selection_end") und preedit.enthält("revision"))
pruef("preedit-utf8-byte-values", preedit["version"] == 1 und preedit["art"] == "preedit" und preedit["text"] == "äb" und preedit["selection_start"] == 2 und preedit["selection_end"] == 3 und preedit["revision"] == 1)

# Commit und Cancel werden auf die kanonischen Protokollwerte gezwungen.
setze commit auf ime("commit", "ä", 0, 0, 2)
pruef("commit-forces-byte-end", typ_von(commit) == "Woerterbuch" und länge(commit) == 6 und commit["version"] == 1 und commit["art"] == "commit" und commit["text"] == "ä" und commit["selection_start"] == 2 und commit["selection_end"] == 2 und commit["revision"] == 2)

setze cancel auf ime("cancel", "ignoriert", 5, 6, 3)
pruef("cancel-forces-empty", typ_von(cancel) == "Woerterbuch" und länge(cancel) == 6 und cancel["version"] == 1 und cancel["art"] == "cancel" und cancel["text"] == "" und cancel["selection_start"] == 0 und cancel["selection_end"] == 0 und cancel["revision"] == 3)

preedit["art"] = "cancel"
setze preedit2 auf ime("preedit", "äb", 2, 3, 1)
pruef("fresh-copy", preedit2["art"] == "preedit" und preedit2["text"] == "äb")

pruef("commit-cancel-schema-exact", commit.enthält("version") und commit.enthält("art") und commit.enthält("text") und commit.enthält("selection_start") und commit.enthält("selection_end") und commit.enthält("revision") und cancel.enthält("version") und cancel.enthält("art") und cancel.enthält("text") und cancel.enthält("selection_start") und cancel.enthält("selection_end") und cancel.enthält("revision"))

wenn s["fehler"] == 0 und s["checks"] == 18:
    zeige "P016-O4-MOO-IME-EVENT-OK checks=18 offsets=utf8-bytes no_ui=1"
sonst:
    zeige "P016-O4-MOO-IME-EVENT-FEHLER checks=" + text(s["checks"]) + " fehler=" + text(s["fehler"])
    wirf "P016-O4 ui_moo IME-Ereignisvertrag verletzt"

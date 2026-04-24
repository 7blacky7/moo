# ============================================================
# beispiele/tests/ui_shortcuts_test.moo — visueller Test: Shortcuts
# (Plan-004 P4)
#
# ui_shortcuts_demo-Logik:
#   frame_001: initial
#   Aktion:    shortcut "Ctrl+S"  -> Speichern-Callback setzt Status
#   frame_002: nach Ctrl+S
#   Aktion:    shortcut "Escape"  -> darf Callback triggern, nicht ui_beenden
# ============================================================

importiere ui
importiere ui_aktionen


setze g auf {}


funktion log_status(t):
    wenn g.enthält("lblStatus"):
        ui_label_setze(g["lblStatus"], t)


funktion aktion_speichern():
    log_status("Aktion: Speichern (Ctrl+S)")


funktion aktion_escape():
    log_status("Aktion: Escape gedrueckt")


setze g["hFenster"] auf ui_fenster("Shortcuts-Test", 480, 240, 0, nichts)
setze g["lblHinweis"] auf ui_label(g["hFenster"], "Shortcuts-Test", 10, 10, 460, 24)
setze g["lblStatus"]  auf ui_label(g["hFenster"], "Bereit.", 10, 50, 460, 24)


# Aktionen registrieren (bindet Shortcuts ans Fenster)
ui_aktion(g, "speichern", "Speichern", "Ctrl+S", aktion_speichern)
ui_aktion(g, "escape",    "Escape",    "Escape", aktion_escape)


ui_widget_id_setze(g["hFenster"],  "root")
ui_widget_id_setze(g["lblHinweis"],"hinweis")
ui_widget_id_setze(g["lblStatus"], "status")


wenn datei_existiert("beispiele/snapshots/shortcuts") != wahr:
    datei_mkdir("beispiele/snapshots/shortcuts")


setze bericht auf {}
setze bericht["reports"] auf []
setze bericht["fertig"] auf falsch


funktion lauf_sequenz():
    setze aktionen auf []

    setze a1 auf {}
    setze a1["action"] auf "frame"
    setze a1["pfad"] auf "beispiele/snapshots/shortcuts/frame_001"
    aktionen.hinzufügen(a1)

    setze a2 auf {}
    setze a2["action"] auf "shortcut"
    setze a2["sequenz"] auf "Ctrl+S"
    aktionen.hinzufügen(a2)

    setze a3 auf {}
    setze a3["action"] auf "warte"
    setze a3["ms"] auf 150
    aktionen.hinzufügen(a3)

    setze a4 auf {}
    setze a4["action"] auf "frame"
    setze a4["pfad"] auf "beispiele/snapshots/shortcuts/frame_002"
    aktionen.hinzufügen(a4)

    setze a5 auf {}
    setze a5["action"] auf "shortcut"
    setze a5["sequenz"] auf "Escape"
    aktionen.hinzufügen(a5)

    setze a6 auf {}
    setze a6["action"] auf "warte"
    setze a6["ms"] auf 150
    aktionen.hinzufügen(a6)

    setze a7 auf {}
    setze a7["action"] auf "frame"
    setze a7["pfad"] auf "beispiele/snapshots/shortcuts/frame_003"
    aktionen.hinzufügen(a7)

    setze reports auf ui_test_sequenz(g["hFenster"], "beispiele/snapshots/shortcuts", aktionen)
    setze bericht["reports"] auf reports
    setze bericht["fertig"] auf wahr
    ui_beenden()


ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(500, lauf_sequenz)
ui_laufen()


wenn bericht["fertig"] != wahr:
    zeige "=== SHORTCUTS-TEST FEHLER (Sequenz nicht gelaufen) ==="
    beende(1)


setze reports auf bericht["reports"]
setze total auf länge(reports)
setze okc auf 0
setze frames auf []
setze i auf 0
solange i < total:
    setze r auf reports[i]
    wenn r["erfolg"] == wahr:
        setze okc auf okc + 1
    wenn r["action"] == "frame":
        frames.hinzufügen(r["target"])
    setze i auf i + 1

zeige "Shortcuts-Test: " + text(okc) + "/" + text(total) + " Aktionen OK"

setze json_ok auf wahr
setze i auf 0
setze nf auf länge(frames)
solange i < nf:
    setze basis auf frames[i]
    setze jp auf basis + ".json"
    setze pp auf basis + ".png"
    wenn datei_existiert(jp) != wahr:
        zeige "FEHLT: " + jp
        setze json_ok auf falsch
    sonst:
        wenn datei_existiert(pp) != wahr:
            zeige "FEHLT: " + pp
            setze json_ok auf falsch
        sonst:
            setze roh auf datei_lesen(jp)
            setze parsed auf json_parse(roh)
            wenn parsed == nichts:
                zeige "JSON UNGUELTIG: " + jp
                setze json_ok auf falsch
            sonst:
                wenn parsed.enthält("widget_tree") != wahr:
                    setze json_ok auf falsch
                zeige "OK: " + jp
    setze i auf i + 1

wenn okc == total:
    wenn json_ok == wahr:
        zeige "=== SHORTCUTS-TEST OK ==="
        beende(0)

zeige "=== SHORTCUTS-TEST FEHLER ==="
beende(1)

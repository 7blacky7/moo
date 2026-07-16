# ============================================================
# beispiele/tests/ui_canvas_test.moo — visueller Test: Canvas
# (Plan-004 P4)
#
# ui_canvas_mouse_demo-Logik (reduziert fuer Automation):
#   Leinwand 580x400 + on_maus-Callback (speichert Klick-Punkte)
#   frame_001: initial (leere Leinwand)
#   Aktion:    klick_xy(100,100) auf Fenster -> Leinwand-Treffer
#   frame_002: nach 1. Klick
#   Aktion:    klick_xy(200,150)
#   frame_003: nach 2. Klick
# ============================================================

importiere ui


setze g auf {}
setze g["punkte"] auf []


funktion auf_zeichne(lw, z):
    ui_zeichne_farbe(z, 30, 32, 38, 255)
    ui_zeichne_rechteck(z, 0, 0, 580, 400, wahr)

    ui_zeichne_farbe(z, 80, 160, 240, 255)
    für p in g["punkte"]:
        ui_zeichne_rechteck(z, p[0] - 8, p[1] - 8, 16, 16, wahr)

    ui_zeichne_farbe(z, 200, 200, 200, 255)
    ui_zeichne_text(z, 10, 20, "Canvas-Test", 12)
    gib_zurück wahr


funktion auf_maus(lw, x, y, taste):
    g["punkte"].hinzufügen([x, y])
    ui_label_setze(g["status"], "Klick: " + text(x) + "," + text(y) + " (anz=" + text(länge(g["punkte"])) + ")")
    ui_leinwand_anfordern(lw)
    gib_zurück wahr


setze g["hFenster"] auf ui_fenster("Canvas-Test", 600, 460, 0, nichts)
setze g["canvas"]   auf ui_leinwand(g["hFenster"], 10, 10, 580, 400, auf_zeichne)
setze g["status"]   auf ui_label(g["hFenster"], "Bereit.", 10, 420, 580, 22)

ui_leinwand_on_maus(g["canvas"], auf_maus)


ui_widget_id_setze(g["hFenster"], "root")
ui_widget_id_setze(g["canvas"],   "canvas")
ui_widget_id_setze(g["status"],   "status")


wenn datei_existiert("beispiele/snapshots/canvas") != wahr:
    datei_mkdir("beispiele/snapshots/canvas")


setze bericht auf {}
setze bericht["reports"] auf []
setze bericht["fertig"] auf falsch


funktion lauf_sequenz():
    setze aktionen auf []

    setze a1 auf {}
    setze a1["action"] auf "frame"
    setze a1["pfad"] auf "beispiele/snapshots/canvas/frame_001"
    aktionen.hinzufügen(a1)

    setze a2 auf {}
    setze a2["action"] auf "klick_xy"
    setze a2["x"] auf 100
    setze a2["y"] auf 100
    aktionen.hinzufügen(a2)

    setze a3 auf {}
    setze a3["action"] auf "warte"
    setze a3["ms"] auf 150
    aktionen.hinzufügen(a3)

    setze a4 auf {}
    setze a4["action"] auf "frame"
    setze a4["pfad"] auf "beispiele/snapshots/canvas/frame_002"
    aktionen.hinzufügen(a4)

    setze a5 auf {}
    setze a5["action"] auf "klick_xy"
    setze a5["x"] auf 200
    setze a5["y"] auf 150
    aktionen.hinzufügen(a5)

    setze a6 auf {}
    setze a6["action"] auf "warte"
    setze a6["ms"] auf 150
    aktionen.hinzufügen(a6)

    setze a7 auf {}
    setze a7["action"] auf "frame"
    setze a7["pfad"] auf "beispiele/snapshots/canvas/frame_003"
    aktionen.hinzufügen(a7)

    setze reports auf ui_test_sequenz(g["hFenster"], "beispiele/snapshots/canvas", aktionen)
    setze bericht["reports"] auf reports
    setze bericht["fertig"] auf wahr
    ui_beenden()


ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(500, lauf_sequenz)
ui_laufen()


wenn bericht["fertig"] != wahr:
    zeige "=== CANVAS-TEST FEHLER (Sequenz nicht gelaufen) ==="
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

zeige "Canvas-Test: " + text(okc) + "/" + text(total) + " Aktionen OK"

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
        zeige "=== CANVAS-TEST OK ==="
        beende(0)

zeige "=== CANVAS-TEST FEHLER ==="
beende(1)

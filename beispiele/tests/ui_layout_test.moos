# ============================================================
# beispiele/tests/ui_layout_test.moo — visueller Test: Layout
# (Plan-004 P4)
#
# Baut ein Layout aus ui_spalte/ui_zeile (analog ui_layout_demo,
# aber fuer Automation reduziert) und erzeugt drei Frames:
#   frame_001: initial (direkt nach Aufbau)
#   frame_002: nach programmatischer Relayout-Simulation
#              (Container-Groesse aendern + ui_layout_neu_berechnen)
#   frame_003: final (nach zusaetzlichem text_setze im Eingabefeld)
#
# Jedes Frame bekommt PNG + JSON-Sidecar. widget_tree im JSON.
# Exit 0 wenn alle 3 Frames valide, sonst 1.
# ============================================================

importiere ui
importiere ui_layout


setze g auf {}


funktion on_ok():
    ui_label_setze(g["lblStatus"], "OK geklickt")


setze FB auf 480
setze FH auf 300
setze g["hFenster"] auf ui_fenster("Layout-Test", FB, FH, 0, nichts)

# Haupt-Spalte mit Padding + Abstand
setze haupt auf ui_spalte(g["hFenster"], 0, 0, FB, FH)
ui_layout_padding(haupt, 12, 12, 12, 12)
ui_layout_abstand(haupt, 8)

# Header
setze g["lblTitel"] auf ui_layout_label(haupt, "Layout-Test", {"hoehe": 24, "fill_x": wahr})

# Eingabe
setze g["inpName"] auf ui_layout_eingabe(haupt, "Name", {"hoehe": 28, "fill_x": wahr})

# Status-Label (dehnbar)
setze g["lblStatus"] auf ui_layout_label(haupt, "Bereit.", {"fill_x": wahr, "gewicht": 1, "fill_y": wahr})

# OK-Knopf
setze g["btnOK"] auf ui_layout_knopf(haupt, "OK", on_ok, {"hoehe": 30, "fill_x": wahr})

ui_layout_neu_berechnen(haupt)


# IDs fuer stabile Widget-Suche
ui_widget_id_setze(g["hFenster"], "root")
ui_widget_id_setze(g["lblTitel"], "titel")
ui_widget_id_setze(g["inpName"],  "name")
ui_widget_id_setze(g["lblStatus"],"status")
ui_widget_id_setze(g["btnOK"],    "ok")


wenn datei_existiert("beispiele/snapshots/layout") != wahr:
    datei_mkdir("beispiele/snapshots/layout")


setze bericht auf {}
setze bericht["reports"] auf []
setze bericht["fertig"] auf falsch


funktion lauf_sequenz():
    setze aktionen auf []

    # 1) initial frame
    setze a1 auf {}
    setze a1["action"] auf "frame"
    setze a1["pfad"] auf "beispiele/snapshots/layout/frame_001"
    aktionen.hinzufügen(a1)

    # 2) Text setzen -> Relayout
    setze a2 auf {}
    setze a2["action"] auf "text"
    setze a2["target"] auf "name"
    setze a2["wert"] auf "Moritz"
    aktionen.hinzufügen(a2)

    setze a3 auf {}
    setze a3["action"] auf "warte"
    setze a3["ms"] auf 150
    aktionen.hinzufügen(a3)

    setze a4 auf {}
    setze a4["action"] auf "frame"
    setze a4["pfad"] auf "beispiele/snapshots/layout/frame_002"
    aktionen.hinzufügen(a4)

    # 3) Klick OK -> Status-Label Update
    setze a5 auf {}
    setze a5["action"] auf "klick"
    setze a5["target"] auf "ok"
    aktionen.hinzufügen(a5)

    setze a6 auf {}
    setze a6["action"] auf "warte"
    setze a6["ms"] auf 150
    aktionen.hinzufügen(a6)

    setze a7 auf {}
    setze a7["action"] auf "frame"
    setze a7["pfad"] auf "beispiele/snapshots/layout/frame_003"
    aktionen.hinzufügen(a7)

    setze reports auf ui_test_sequenz(g["hFenster"], "beispiele/snapshots/layout", aktionen)
    setze bericht["reports"] auf reports
    setze bericht["fertig"] auf wahr
    ui_beenden()


ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(500, lauf_sequenz)
ui_laufen()


wenn bericht["fertig"] != wahr:
    zeige "=== LAYOUT-TEST FEHLER (Sequenz nicht gelaufen) ==="
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

zeige "Layout-Test: " + text(okc) + "/" + text(total) + " Aktionen OK"

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
                    zeige "FEHLENDES FELD widget_tree: " + jp
                    setze json_ok auf falsch
                zeige "OK: " + jp
    setze i auf i + 1

wenn okc == total:
    wenn json_ok == wahr:
        zeige "=== LAYOUT-TEST OK ==="
        beende(0)

zeige "=== LAYOUT-TEST FEHLER ==="
beende(1)

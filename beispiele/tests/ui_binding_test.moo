# ============================================================
# beispiele/tests/ui_binding_test.moo — visueller Test: Binding
# (Plan-004 P4)
#
# ui_binding_demo-Logik (reduziert):
#   frame_001: initial
#   Aktion:    text_setze "name" -> "Moritz"
#   frame_002: nach text_setze
#   Aktion:    klick Knopf "speichern"
#   frame_003: nach klick (Status-Label geaendert via Callback)
# ============================================================

importiere ui


setze g auf {}


funktion on_speichern():
    setze n auf ui_eingabe_text(g["inpName"])
    ui_label_setze(g["lblStatus"], "gespeichert: " + n)


setze g["hFenster"] auf ui_fenster("Binding-Test", 420, 220, 0, nichts)
setze g["lblTitel"]    auf ui_label(g["hFenster"], "Binding-Test", 20, 20, 380, 24)
setze g["inpName"]     auf ui_eingabe(g["hFenster"], 20, 60, 380, 28, "", falsch)
setze g["btnSpeichern"] auf ui_knopf(g["hFenster"], "Speichern", 20, 110, 180, 32, on_speichern)
setze g["lblStatus"]   auf ui_label(g["hFenster"], "-", 20, 160, 380, 24)


ui_widget_id_setze(g["hFenster"],     "root")
ui_widget_id_setze(g["lblTitel"],     "titel")
ui_widget_id_setze(g["inpName"],      "name")
ui_widget_id_setze(g["btnSpeichern"], "speichern")
ui_widget_id_setze(g["lblStatus"],    "status")


wenn datei_existiert("beispiele/snapshots/binding") != wahr:
    datei_mkdir("beispiele/snapshots/binding")


setze bericht auf {}
setze bericht["reports"] auf []
setze bericht["fertig"] auf falsch


funktion lauf_sequenz():
    setze aktionen auf []

    setze a1 auf {}
    setze a1["action"] auf "frame"
    setze a1["pfad"] auf "beispiele/snapshots/binding/frame_001"
    aktionen.hinzufügen(a1)

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
    setze a4["pfad"] auf "beispiele/snapshots/binding/frame_002"
    aktionen.hinzufügen(a4)

    setze a5 auf {}
    setze a5["action"] auf "klick"
    setze a5["target"] auf "speichern"
    aktionen.hinzufügen(a5)

    setze a6 auf {}
    setze a6["action"] auf "warte"
    setze a6["ms"] auf 200
    aktionen.hinzufügen(a6)

    setze a7 auf {}
    setze a7["action"] auf "frame"
    setze a7["pfad"] auf "beispiele/snapshots/binding/frame_003"
    aktionen.hinzufügen(a7)

    setze reports auf ui_test_sequenz(g["hFenster"], "beispiele/snapshots/binding", aktionen)
    setze bericht["reports"] auf reports
    setze bericht["fertig"] auf wahr
    ui_beenden()


ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(500, lauf_sequenz)
ui_laufen()


wenn bericht["fertig"] != wahr:
    zeige "=== BINDING-TEST FEHLER (Sequenz nicht gelaufen) ==="
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

zeige "Binding-Test: " + text(okc) + "/" + text(total) + " Aktionen OK"

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
        zeige "=== BINDING-TEST OK ==="
        beende(0)

zeige "=== BINDING-TEST FEHLER ==="
beende(1)

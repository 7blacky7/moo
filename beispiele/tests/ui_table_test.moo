# ============================================================
# beispiele/tests/ui_table_test.moo — visueller Test: ListView
# (Plan-004 P4)
#
# ui_table_demo-Logik als Test:
#   frame_001: initial (7 Zeilen Dateidaten)
#   Aktion:    Sortiere Spalte 0 aufsteigend (Name)
#   frame_002: nach Sortierung
#   Aktion:    Zelle [1][1] setzen -> "999 MB"
#   frame_003: nach Zell-Update
#   Aktion:    Erste Zeile entfernen
#   frame_004: nach Zeilen-Entfernen
# ============================================================

importiere ui


setze g auf {}

setze datei_daten auf [
    ["dokument.pdf", "1.2 MB", "PDF"],
    ["bild.png",     "500 KB", "Bild"],
    ["video.mp4",    "50 MB",  "Video"],
    ["musik.mp3",    "4.5 MB", "Audio"],
    ["archiv.zip",   "10 MB",  "Archiv"],
    ["skript.moo",   "8 KB",   "Quellcode"],
    ["tabelle.csv",  "32 KB",  "CSV"]
]


setze g["hFenster"] auf ui_fenster("Table-Test", 540, 360, 0, nichts)
setze g["liste"] auf ui_liste(g["hFenster"], ["Name", "Groesse", "Typ"], 10, 10, 520, 260)

setze cfg auf {}
setze cfg["breiten"] auf [260, 140, 120]
setze cfg["sortierbar"] auf [wahr, wahr, wahr]
ui_liste_konfiguriere(g["liste"], cfg)
ui_liste_fuellen(g["liste"], datei_daten)

setze g["lblStatus"] auf ui_label(g["hFenster"], "Bereit.", 10, 280, 520, 22)


funktion sortiere_name():
    ui_liste_sortiere(g["liste"], 0, wahr)
    ui_label_setze(g["lblStatus"], "Sortiert: Name")

funktion zelle_setzen():
    ui_liste_zelle_setze(g["liste"], 1, 1, "999 MB")
    ui_label_setze(g["lblStatus"], "Zelle [1][1] -> 999 MB")

funktion zeile_entfernen():
    ui_liste_entferne(g["liste"], 0)
    ui_label_setze(g["lblStatus"], "Erste Zeile entfernt")


setze g["btnSort"] auf ui_knopf(g["hFenster"], "Sort",   10,  310, 100, 28, sortiere_name)
setze g["btnCell"] auf ui_knopf(g["hFenster"], "Zelle",  120, 310, 100, 28, zelle_setzen)
setze g["btnDel"]  auf ui_knopf(g["hFenster"], "Entf.",  230, 310, 100, 28, zeile_entfernen)


ui_widget_id_setze(g["hFenster"], "root")
ui_widget_id_setze(g["liste"],    "liste")
ui_widget_id_setze(g["lblStatus"],"status")
ui_widget_id_setze(g["btnSort"],  "sort")
ui_widget_id_setze(g["btnCell"],  "zelle")
ui_widget_id_setze(g["btnDel"],   "entf")


wenn datei_existiert("beispiele/snapshots/table") != wahr:
    datei_mkdir("beispiele/snapshots/table")


setze bericht auf {}
setze bericht["reports"] auf []
setze bericht["fertig"] auf falsch


funktion lauf_sequenz():
    setze aktionen auf []

    setze a1 auf {}
    setze a1["action"] auf "frame"
    setze a1["pfad"] auf "beispiele/snapshots/table/frame_001"
    aktionen.hinzufügen(a1)

    setze a2 auf {}
    setze a2["action"] auf "klick"
    setze a2["target"] auf "sort"
    aktionen.hinzufügen(a2)

    setze a3 auf {}
    setze a3["action"] auf "warte"
    setze a3["ms"] auf 150
    aktionen.hinzufügen(a3)

    setze a4 auf {}
    setze a4["action"] auf "frame"
    setze a4["pfad"] auf "beispiele/snapshots/table/frame_002"
    aktionen.hinzufügen(a4)

    setze a5 auf {}
    setze a5["action"] auf "klick"
    setze a5["target"] auf "zelle"
    aktionen.hinzufügen(a5)

    setze a6 auf {}
    setze a6["action"] auf "warte"
    setze a6["ms"] auf 150
    aktionen.hinzufügen(a6)

    setze a7 auf {}
    setze a7["action"] auf "frame"
    setze a7["pfad"] auf "beispiele/snapshots/table/frame_003"
    aktionen.hinzufügen(a7)

    setze a8 auf {}
    setze a8["action"] auf "klick"
    setze a8["target"] auf "entf"
    aktionen.hinzufügen(a8)

    setze a9 auf {}
    setze a9["action"] auf "warte"
    setze a9["ms"] auf 150
    aktionen.hinzufügen(a9)

    setze a10 auf {}
    setze a10["action"] auf "frame"
    setze a10["pfad"] auf "beispiele/snapshots/table/frame_004"
    aktionen.hinzufügen(a10)

    setze reports auf ui_test_sequenz(g["hFenster"], "beispiele/snapshots/table", aktionen)
    setze bericht["reports"] auf reports
    setze bericht["fertig"] auf wahr
    ui_beenden()


ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(500, lauf_sequenz)
ui_laufen()


wenn bericht["fertig"] != wahr:
    zeige "=== TABLE-TEST FEHLER (Sequenz nicht gelaufen) ==="
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

zeige "Table-Test: " + text(okc) + "/" + text(total) + " Aktionen OK"

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
        zeige "=== TABLE-TEST OK ==="
        beende(0)

zeige "=== TABLE-TEST FEHLER ==="
beende(1)

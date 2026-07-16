# ============================================================
# beispiele/ui_automation_demo.moo — Automation + Frame-Metadaten
#                                    (Plan-004 P3)
#
# Oeffnet ein kleines Fenster (Eingabe "name" + Knopf "speichern" +
# Label "status") und fuehrt nach kurzer Realisierung eine Sequenz aus:
#
#   1) text  "Moritz" in Eingabe "name"
#   2) warte 200ms
#   3) frame -> beispiele/snapshots/auto_01.{png,json}
#   4) klick Knopf "speichern" (setzt Status-Label via Callback)
#   5) warte 200ms
#   6) frame -> beispiele/snapshots/auto_02.{png,json}
#
# Pruefung (Demo selbst, exit-code-driven):
#   - Jede Aktion im Sequenz-Report muss erfolg==wahr haben.
#   - Jede .json-Datei muss existieren und via json_parse geparst werden
#     koennen (entspricht python3 json.loads).
#
# Exit 0: alle Frames JSON-valide und alle Aktionen erfolgreich.
# Exit 1: irgendwas ist schief gegangen.
#
# Externe Verifikation:
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       run beispiele/ui_automation_demo.moo
#   python3 -c "import json; json.load(open('beispiele/snapshots/auto_01.json'))"
#   python3 -c "import json; json.load(open('beispiele/snapshots/auto_02.json'))"
# ============================================================

importiere ui


setze g auf {}


# Callback fuer Knopf "speichern": setzt das Status-Label.
funktion on_speichern():
    setze n auf ui_eingabe_text(g["inpName"])
    ui_label_setze(g["lblStatus"], "gespeichert: " + n)


setze g["hFenster"] auf ui_fenster("Automation Demo", 420, 220, 0, nichts)
setze g["lblTitel"]    auf ui_label(g["hFenster"], "Automation-Demo", 20, 20, 380, 24)
setze g["inpName"]     auf ui_eingabe(g["hFenster"], 20, 60, 380, 28, "Dein Name", falsch)
setze g["btnSpeichern"] auf ui_knopf(g["hFenster"], "Speichern", 20, 110, 180, 32, on_speichern)
setze g["lblStatus"]   auf ui_label(g["hFenster"], "-", 20, 160, 380, 24)


ui_widget_id_setze(g["hFenster"],     "root")
ui_widget_id_setze(g["lblTitel"],     "titel")
ui_widget_id_setze(g["inpName"],      "name")
ui_widget_id_setze(g["btnSpeichern"], "speichern")
ui_widget_id_setze(g["lblStatus"],    "status")


wenn datei_existiert("beispiele/snapshots") != wahr:
    datei_mkdir("beispiele/snapshots")


# Sequenz-Report wird im Timer-Callback befuellt.
setze bericht auf {}
setze bericht["reports"] auf []
setze bericht["fertig"] auf falsch


funktion lauf_sequenz():
    setze aktionen auf []

    setze a1 auf {}
    setze a1["action"] auf "text"
    setze a1["target"] auf "name"
    setze a1["wert"] auf "Moritz"
    aktionen.hinzufügen(a1)

    setze a2 auf {}
    setze a2["action"] auf "warte"
    setze a2["ms"] auf 200
    aktionen.hinzufügen(a2)

    setze a3 auf {}
    setze a3["action"] auf "frame"
    setze a3["pfad"] auf "beispiele/snapshots/auto_01"
    aktionen.hinzufügen(a3)

    setze a4 auf {}
    setze a4["action"] auf "klick"
    setze a4["target"] auf "speichern"
    aktionen.hinzufügen(a4)

    setze a5 auf {}
    setze a5["action"] auf "warte"
    setze a5["ms"] auf 200
    aktionen.hinzufügen(a5)

    setze a6 auf {}
    setze a6["action"] auf "frame"
    setze a6["pfad"] auf "beispiele/snapshots/auto_02"
    aktionen.hinzufügen(a6)

    setze reports auf ui_test_sequenz(g["hFenster"], "beispiele/snapshots", aktionen)
    setze bericht["reports"] auf reports
    setze bericht["fertig"] auf wahr
    ui_beenden()


ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(500, lauf_sequenz)
ui_laufen()


# --- Auswertung -------------------------------------------------
wenn bericht["fertig"] != wahr:
    zeige "=== DEMO ENDE FEHLER (Sequenz nicht gelaufen) ==="
    beende(1)


setze reports auf bericht["reports"]
setze total auf länge(reports)
setze okc auf 0
setze frames_pfade auf []

setze i auf 0
solange i < total:
    setze r auf reports[i]
    wenn r["erfolg"] == wahr:
        setze okc auf okc + 1
    wenn r["action"] == "frame":
        frames_pfade.hinzufügen(r["target"])
    setze i auf i + 1

zeige "Sequenz: " + text(okc) + "/" + text(total) + " Aktionen erfolgreich"


# JSON-Validitaet aller Frames pruefen (pure moo, entspricht python3 json.loads).
setze json_ok auf wahr
setze i auf 0
setze nf auf länge(frames_pfade)
solange i < nf:
    setze basis auf frames_pfade[i]
    setze json_pfad auf basis + ".json"
    setze png_pfad auf basis + ".png"
    wenn datei_existiert(json_pfad) != wahr:
        zeige "FEHLT: " + json_pfad
        setze json_ok auf falsch
    sonst:
        wenn datei_existiert(png_pfad) != wahr:
            zeige "FEHLT: " + png_pfad
            setze json_ok auf falsch
        sonst:
            setze roh auf datei_lesen(json_pfad)
            setze parsed auf json_parse(roh)
            wenn parsed == nichts:
                zeige "JSON UNGUELTIG: " + json_pfad
                setze json_ok auf falsch
            sonst:
                # Pflichtfelder fuer Frame-Sidecar (Plan-004 P3).
                wenn parsed.enthält("action") != wahr:
                    zeige "FEHLENDES FELD 'action': " + json_pfad
                    setze json_ok auf falsch
                wenn parsed.enthält("widget_tree") != wahr:
                    zeige "FEHLENDES FELD 'widget_tree': " + json_pfad
                    setze json_ok auf falsch
                zeige "OK: " + json_pfad
    setze i auf i + 1


wenn okc == total:
    wenn json_ok == wahr:
        zeige "=== DEMO ENDE OK ==="
        beende(0)


zeige "=== DEMO ENDE FEHLER ==="
beende(1)

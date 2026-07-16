# ============================================================
# beispiele/ui_snapshot_demo.moo — Snapshot + Sidecar (Plan-004 P2)
#
# Oeffnet ein kleines Fenster mit 4 Widgets (Label / Eingabe /
# Knopf / Checkbox), setzt IDs, zeigt das Fenster non-blocking,
# wartet kurz via Timer und ruft dann:
#
#     ui_test_snapshot_mit_sidecar(fenster, "beispiele/snapshots/demo1")
#
# Das erzeugt:
#     beispiele/snapshots/demo1.png   (Screenshot, Runtime)
#     beispiele/snapshots/demo1.json  (Sidecar, stdlib)
#
# Exit-Code 0: Snapshot + JSON erfolgreich.
# Exit-Code 1: irgendwas ist schief gegangen.
#
# Verifikation (shell):
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       run beispiele/ui_snapshot_demo.moo
#   python3 -c "import json; json.load(open('beispiele/snapshots/demo1.json'))"
#   ls -l beispiele/snapshots/demo1.png
# ============================================================

importiere ui


# Wir merken uns den Status in einem globalen Dict, damit der
# Timer-Callback darauf zugreifen kann.
setze status auf {}
setze status["ok"] auf falsch

setze g auf {}
setze g["hFenster"] auf ui_fenster("Snapshot Demo", 420, 220, 0, nichts)
setze g["lblTitel"] auf ui_label(g["hFenster"], "Snapshot-Demo", 20, 20, 380, 24)
setze g["inpName"] auf ui_eingabe(g["hFenster"], 20, 60, 380, 28, "Dein Name", falsch)
setze g["btnSpeichern"] auf ui_knopf(g["hFenster"], "Speichern", 20, 110, 180, 32, nichts)
setze g["chkAktiv"] auf ui_checkbox(g["hFenster"], "Aktiv", 220, 110, 180, 24, wahr, nichts)


# IDs setzen (Sidecar-JSON erwartet stabile IDs fuer KI-Review).
ui_widget_id_setze(g["hFenster"],     "root")
ui_widget_id_setze(g["lblTitel"],     "titel")
ui_widget_id_setze(g["inpName"],      "name")
ui_widget_id_setze(g["btnSpeichern"], "speichern")
ui_widget_id_setze(g["chkAktiv"],     "aktiv")


# Zielverzeichnis sicherstellen.
wenn datei_existiert("beispiele/snapshots") != wahr:
    datei_mkdir("beispiele/snapshots")


# Timer-Callback: nach 500ms Snapshot nehmen und Loop beenden.
funktion do_snapshot():
    setze ok auf ui_test_snapshot_mit_sidecar(g["hFenster"], "beispiele/snapshots/demo1")
    setze status["ok"] auf ok
    wenn ok == wahr:
        zeige "SNAPSHOT OK -> beispiele/snapshots/demo1.{png,json}"
    sonst:
        zeige "SNAPSHOT FEHLGESCHLAGEN"
    ui_beenden()


ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(500, do_snapshot)
ui_laufen()


# Nach dem Event-Loop: Ergebnis auswerten.
wenn status["ok"] == wahr:
    zeige "=== DEMO ENDE OK ==="
    beende(0)
sonst:
    zeige "=== DEMO ENDE FEHLER ==="
    beende(1)

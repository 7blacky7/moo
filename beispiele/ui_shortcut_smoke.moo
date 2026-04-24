# ============================================================
# ui_shortcut_smoke.moo — automatischer ALIVE-Test fuer
# moo_ui_shortcut_bind (Linux/GTK).
#
# Bindet Ctrl+S ans Fenster. Nach 500ms Timeout wird ein
# externer xdotool-Prozess Ctrl+S an das Fenster senden.
# Nach 2500ms beendet ein Timer das Programm.
# Erfolg: stdout enthaelt "SHORTCUT_FIRED" VOR "TIMEOUT_EXIT".
# ============================================================

importiere ui

setze g auf {}

funktion on_save():
    zeige "SHORTCUT_FIRED"

funktion on_timeout():
    zeige "TIMEOUT_EXIT"
    ui_beenden()

setze flags auf 0
g["hFenster"] = ui_fenster("shortcut-smoke", 400, 100, flags, nichts)
g["lblInfo"]  = ui_label(g["hFenster"], "Warte auf Ctrl+S ...", 10, 10, 380, 30)

zeige "[smoke] Fenster bereit"

ui_shortcut_bind(g["hFenster"], "Ctrl+S", on_save)
zeige "[smoke] Ctrl+S gebunden"

ui_timer_hinzu(2500, on_timeout)

ui_zeige(g["hFenster"])
ui_laufen()
zeige "[smoke] beendet"

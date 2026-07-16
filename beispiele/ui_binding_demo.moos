# ============================================================
# ui_binding_demo.moo — Showcase fuer stdlib/ui_bind (P4)
#
# Zeigt BEIDE Wege parallel:
#   1) Deklarativ via ui_bind_text / ui_bind_checked / ui_bind_wert
#      plus ui_state_setze / ui_state_on_change.
#   2) Manuell via ui_checkbox-Callback + ui_state_setze.
#
# Oberflaeche:
#   - Benutzername (Eingabe, ui_bind_text → bidirektional)
#   - "Aktiv" Checkbox (ui_bind_checked + manueller Callback schreibt
#     zurueck via ui_state_setze)
#   - Lautstaerke-Slider (ui_bind_wert + manueller Callback)
#   - "State anzeigen"-Knopf: zieht Widgets→State und druckt state
#   - "Reset"-Knopf: setzt State programmatisch (triggert Widgets
#     und Watcher — beweist State→Widget + Watcher-Pfad)
#
# Fuer Alive-Test:
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       compile beispiele/ui_binding_demo.moo
#   ./beispiele/ui_binding_demo
# ============================================================

importiere ui
importiere ui_bind


# Der State — ein schlichtes Dict. ui_bind initialisiert darin
# automatisch __bindings und __watchers.
setze state auf {
    "name": "Alice",
    "aktiv": wahr,
    "lautstaerke": 42
}

# GUI-Container.
setze g auf {}


# ---------- Callbacks ---------------------------------------

# Manueller Checkbox-Callback: schreibt Widget-Wert zurueck in State.
# Der Watcher sieht die Aenderung und aktualisiert das Status-Label.
funktion on_aktiv_toggle():
    setze v auf ui_checkbox_wert(g["chkAktiv"])
    ui_state_setze(state, "aktiv", v)


# Manueller Slider-Callback: schreibt Widget-Wert zurueck.
funktion on_slider_move():
    setze v auf ui_slider_wert(g["sldLaut"])
    ui_state_setze(state, "lautstaerke", v)


# Knopf "Anzeigen": zieht Widgets→State und druckt den State.
funktion on_anzeigen():
    ui_state_sync_von_widgets(state)
    ui_state_zeige(state)
    setze zusammenfassung auf "Name=" + state["name"] + " Aktiv=" + state["aktiv"] + " Laut=" + state["lautstaerke"]
    ui_label_setze(g["lblStatus"], zusammenfassung)


# Knopf "Reset": setzt den State programmatisch. ui_state_setze
# pusht an alle gebundenen Widgets und feuert Watchers.
funktion on_reset():
    ui_state_setze(state, "name", "Default")
    ui_state_setze(state, "aktiv", falsch)
    ui_state_setze(state, "lautstaerke", 0)


# Watcher: feuert bei jedem ui_state_setze("aktiv", ...).
# Signatur (state, wert) — 2 Args, vom Compiler erlaubt.
funktion watcher_aktiv(zustand, wert):
    wenn wert:
        ui_label_setze(g["lblStatus"], "Watcher: aktiv=WAHR")
    sonst:
        ui_label_setze(g["lblStatus"], "Watcher: aktiv=FALSCH")


funktion watcher_lautstaerke(zustand, wert):
    ui_label_setze(g["lblLaut"], "Lautstaerke: " + wert)


# ---------- UI-Aufbau ---------------------------------------

setze g["hFenster"] auf ui_fenster("ui_bind Demo — P4", 480, 360, 0, nichts)

ui_label(g["hFenster"], "Name:", 20, 20, 80, 24)
setze g["inpName"] auf ui_eingabe(g["hFenster"], 110, 20, 300, 28, "", falsch)

setze g["chkAktiv"] auf ui_checkbox(g["hFenster"], "Aktiv", 20, 60, 200, 24, wahr, on_aktiv_toggle)

ui_label(g["hFenster"], "Lautstaerke:", 20, 100, 110, 24)
setze g["sldLaut"] auf ui_slider(g["hFenster"], 0, 100, 42, 140, 100, 270, 24, on_slider_move)
setze g["lblLaut"] auf ui_label(g["hFenster"], "Lautstaerke: 42", 20, 140, 400, 24)

setze g["lblStatus"] auf ui_label(g["hFenster"], "Bereit.", 20, 180, 440, 24)

setze g["btnAnzeigen"] auf ui_knopf(g["hFenster"], "State anzeigen", 20, 230, 200, 32, on_anzeigen)
setze g["btnReset"]    auf ui_knopf(g["hFenster"], "Reset",          240, 230, 200, 32, on_reset)


# ---------- Bindings anmelden -------------------------------

# Deklarativ: Widget <-> state.
ui_bind_text(g["inpName"], state, "name")
ui_bind_checked(g["chkAktiv"], state, "aktiv")
ui_bind_wert(g["sldLaut"], state, "lautstaerke")

# Watchers zusaetzlich — feuern bei jedem ui_state_setze.
ui_state_on_change(state, "aktiv", watcher_aktiv)
ui_state_on_change(state, "lautstaerke", watcher_lautstaerke)


# Los.
ui_zeige(g["hFenster"])
ui_laufen()

# ============================================================
# ui_settings_demo.moo — Showcase fuer setup_settings_panel
#
# Tab-basiertes Einstellungen-Fenster mit 3 Kategorien.
# Jeder Tab bekommt ein paar Demo-Widgets.
# ============================================================

importiere ui
importiere ui_komponenten

setze g auf {}


funktion cb_speichern():
    ui_label_setze(g["lblStatus"], "Einstellungen gespeichert")

funktion cb_abbrechen():
    ui_beenden()


ui_baue(g, "Einstellungen", 560, 420, [
    [setup_settings_panel, [["Allgemein", "Netzwerk", "Darstellung"], 540]],
    [setup_action_buttons, [[
        ["Speichern",  cb_speichern],
        ["Abbrechen",  cb_abbrechen],
    ]]],
    [setup_status_bar, ["Bereit", "3 Kategorien"]],
])


# Tabs mit Inhalten fuellen
setze tabA auf g["tab_Allgemein"]
ui_checkbox(tabA, "Autostart aktivieren",     20, 20, 300, 22, wahr,   ui_default_noop)
ui_checkbox(tabA, "Update-Check beim Start",  20, 48, 300, 22, falsch, ui_default_noop)
ui_label(tabA,    "Sprache:",                 20, 82, 100, 20)
ui_dropdown(tabA, ["Deutsch", "English", "Francais"], 130, 80, 180, 26, ui_default_noop)

setze tabN auf g["tab_Netzwerk"]
ui_label(tabN,    "Proxy-Host:", 20, 20, 100, 20)
ui_eingabe(tabN,  130, 18, 280, 26, "proxy.local", falsch)
ui_label(tabN,    "Port:",       20, 56, 100, 20)
ui_eingabe(tabN,  130, 54, 100, 26, "8080", falsch)
ui_checkbox(tabN, "SSL verwenden", 20, 90, 300, 22, wahr, ui_default_noop)

setze tabD auf g["tab_Darstellung"]
ui_label(tabD,    "Schriftgroesse:", 20, 20, 140, 20)
ui_slider(tabD,   8, 24, 12, 170, 20, 220, 24, ui_default_noop)
ui_checkbox(tabD, "Dunkles Theme",   20, 56, 300, 22, falsch, ui_default_noop)
ui_checkbox(tabD, "Animationen",     20, 84, 300, 22, wahr,   ui_default_noop)


ui_zeige(g["hFenster"])
ui_laufen()

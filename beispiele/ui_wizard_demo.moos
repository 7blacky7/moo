# ============================================================
# ui_wizard_demo.moo — Showcase fuer setup_wizard_pages
#
# 4-Schritt-Wizard fuer eine fiktive Installation.
# Zeigt: Tabs pro Seite + Fortschrittsbalken + Navigations-Buttons.
# ============================================================

importiere ui
importiere ui_komponenten

setze g auf {}
setze aktuelle_seite auf 0
konstante GESAMT_SEITEN auf 4


funktion fortschritt_aktualisieren():
    setze anteil auf (aktuelle_seite + 1) / GESAMT_SEITEN
    ui_fortschritt_setze(g["wizardFortschritt"], anteil)
    ui_tabs_auswahl_setze(g["wizardSeiten"], aktuelle_seite)

funktion cb_zurueck():
    wenn aktuelle_seite > 0:
        setze aktuelle_seite auf aktuelle_seite - 1
        fortschritt_aktualisieren()

funktion cb_weiter():
    wenn aktuelle_seite < GESAMT_SEITEN - 1:
        setze aktuelle_seite auf aktuelle_seite + 1
        fortschritt_aktualisieren()
    sonst:
        ui_info(g["hFenster"], "Fertig", "Installation abgeschlossen!")
        ui_beenden()

funktion cb_abbrechen():
    ui_beenden()


ui_baue(g, "Installation", 560, 460, [
    [setup_wizard_pages, [["Willkommen", "Lizenz", "Optionen", "Fertig"], wahr, wahr]],
])


# Seiten-Inhalt fuellen (die Tab-Handles werden in
# wizardSeitenHandles als Liste abgelegt).
setze handles auf g["wizardSeitenHandles"]

ui_label(handles[0], "Willkommen zur Installation",       20, 20, 500, 28)
ui_label(handles[0], "Dieser Assistent fuehrt Sie durch", 20, 60, 500, 22)
ui_label(handles[0], "die Einrichtung. Klicken Sie auf",  20, 84, 500, 22)
ui_label(handles[0], "'Weiter' um fortzufahren.",         20, 108, 500, 22)

ui_label(handles[1], "Lizenzvereinbarung",                20, 20, 500, 28)
ui_textbereich(handles[1], 20, 56, 500, 160)
ui_checkbox(handles[1], "Ich akzeptiere die Lizenz",      20, 230, 500, 22, falsch, ui_default_noop)

ui_label(handles[2], "Installations-Optionen",            20, 20, 500, 28)
ui_checkbox(handles[2], "Desktop-Verknuepfung anlegen",   20, 56, 500, 22, wahr, ui_default_noop)
ui_checkbox(handles[2], "Autostart aktivieren",           20, 84, 500, 22, falsch, ui_default_noop)
ui_checkbox(handles[2], "Beispiele mit installieren",     20, 112, 500, 22, wahr, ui_default_noop)

ui_label(handles[3], "Bereit zur Installation",           20, 20, 500, 28)
ui_label(handles[3], "Alle noetigen Informationen liegen vor.", 20, 60, 500, 22)
ui_label(handles[3], "Klicken Sie auf 'Weiter' zum Abschluss.", 20, 84, 500, 22)


# Buttons umbinden (wir ueberschreiben die Defaults aus
# setup_wizard_pages mit eigenen Knoepfen, da das Setup keine
# Callbacks annimmt):
ui_knopf(g["hFenster"], "< Zurueck",  10,  g["hoehe"] - 60, 100, 30, cb_zurueck)
ui_knopf(g["hFenster"], "Weiter >",   120, g["hoehe"] - 60, 100, 30, cb_weiter)
ui_knopf(g["hFenster"], "Abbrechen",  440, g["hoehe"] - 60, 110, 30, cb_abbrechen)


fortschritt_aktualisieren()

ui_zeige(g["hFenster"])
ui_laufen()

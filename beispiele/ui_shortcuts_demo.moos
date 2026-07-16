# ============================================================
# ui_shortcuts_demo.moo — Aktionen, Toolbar, Menue, Shortcuts
#
# Zeigt:
#   - ui_aktion(g, name, titel, shortcut, callback)  registriert
#     eine Aktion UND bindet den Shortcut ans Fenster.
#   - ui_toolbar(parent, x, y, b, h, aktionsliste)   baut eine
#     Knopf-Toolbar.
#   - ui_menue_aktion(menue, aktion)                 haengt einen
#     Menueeintrag mit Label inkl. Shortcut-Hinweis ein.
#   - Status-Leiste zeigt die letzte ausgefuehrte Aktion.
#
# ALIVE-Test:
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       compile beispiele/ui_shortcuts_demo.moo
#   ./beispiele/ui_shortcuts_demo
#   → Fenster mit Toolbar, Menue 'Datei', Status-Leiste erscheint.
#     Ctrl+S / Ctrl+O / Esc feuern die Callbacks, Status-Leiste
#     zeigt zuletzt getriggerte Aktion.
#
# Hinweis: Dot-Access auf g[...] vermeiden — Bracket-Pflicht.
# ============================================================

importiere ui
importiere ui_aktionen


setze g auf {}


funktion log_status(text):
    wenn g.enthält("lblStatus"):
        ui_label_setze(g["lblStatus"], text)
    zeige "[shortcuts_demo] " + text


funktion aktion_speichern():
    log_status("Aktion: Speichern (Ctrl+S)")

funktion aktion_oeffnen():
    log_status("Aktion: Oeffnen (Ctrl+O)")

funktion aktion_neu():
    log_status("Aktion: Neu (Ctrl+N)")

funktion aktion_schliessen():
    log_status("Aktion: Schliessen (Esc) — Fenster wird beendet")
    ui_beenden()


# -----------------------------------------------------------
# Setup-Funktion fuer Aktionen + Toolbar + Menue + Statusbar.
# (g, p) Signatur wegen indirektem Aufruf-Limit.
# -----------------------------------------------------------
funktion setup_shortcuts_app(g, p):
    setze f auf g["hFenster"]

    # 1) Aktionen registrieren — bindet Shortcuts automatisch.
    ui_aktion(g, "neu",         "Neu",        "Ctrl+N", aktion_neu)
    ui_aktion(g, "oeffnen",     "Oeffnen",    "Ctrl+O", aktion_oeffnen)
    ui_aktion(g, "speichern",   "Speichern",  "Ctrl+S", aktion_speichern)
    ui_aktion(g, "schliessen",  "Schliessen", "Escape", aktion_schliessen)

    # 2) Menue aufbauen — dieselben Aktionen.
    setze leiste auf ui_menueleiste(f)
    setze datei_menue auf ui_menue(leiste, "Datei")
    setze reg auf g["aktionen"]

    ui_menue_aktion(datei_menue, reg["neu"])
    ui_menue_aktion(datei_menue, reg["oeffnen"])
    ui_menue_aktion(datei_menue, reg["speichern"])
    ui_menue_trenner(datei_menue)
    ui_menue_aktion(datei_menue, reg["schliessen"])

    # 3) Toolbar — direkt aus der Aktionsliste, mit Separator vor
    # "Schliessen".
    setze toolbar_aktionen auf [
        reg["neu"],
        reg["oeffnen"],
        reg["speichern"],
        {"name": "_separator"},
        reg["schliessen"],
    ]
    setze tb auf ui_toolbar(f, 10, 10, 0, 30, toolbar_aktionen)
    g["toolbar"] = tb

    # 4) Hinweis-Label + Status-Leiste.
    g["lblHinweis"] = ui_label(f, "Probiere: Ctrl+N, Ctrl+O, Ctrl+S, Esc — oder Toolbar/Menue.", 10, 60, 600, 24)
    g["lblStatus"]  = ui_label(f, "Bereit. (Keine Aktion ausgefuehrt)", 10, 250, 600, 20)

    gib_zurück wahr


ui_baue(g, "Shortcuts-Demo — ui_aktion + Toolbar + Menue", 640, 290, [
    [setup_shortcuts_app, []],
], wahr, falsch, nichts)

zeige "[shortcuts_demo] Fenster aufgebaut."
zeige "[shortcuts_demo] Aktionen: " + länge(ui_aktionen_liste(g))

ui_zeige(g["hFenster"])
ui_laufen()

# ============================================================
# ui_modular_app.moo — Showcase fuer modulare UI-Komposition
#
# Uebersetzung von AutoIt-GUIBuilder examples/Advanced/
# Demo_Modular_Main.au3 nach moo.
#
# Enthaelt in einem Fenster:
#   - App-Header (Titel + Version + Trenner)
#   - User-Form (Name, Email, Kategorie)
#   - Notes-Area (mehrzeiliger Text)
#   - Action-Buttons (Speichern/Laden/Neu/Hilfe/Ueber)
#   - Status-Bar
#
# Der "Ueber"-Button oeffnet ein zweites Fenster mit setup_about_info.
#
# Kompilieren & Starten wie ui_login_demo.moo.
# ============================================================

importiere ui


# --- Zweites Fenster (Ueber-Dialog) ---

funktion zeige_ueber():
    setze a auf {}
    ui_baue(a, "Ueber moo", 420, 300, [
        [setup_about_info, [
            "moo UI-Showcase",
            "1.0.0",
            "Ein Demo fuer die modulare UI-Komposition.\n" +
            "Inspiriert vom AutoIt GUIBuilder.\n\n" +
            "(c) 2026 Moritz Kolar",
        ]],
    ])
    ui_zeige_nebenbei(a.hFenster)


# --- Haupt-Fenster ---

setze g auf {}


# --- Callbacks (binden via Closure auf `g`) ---

setze cb_speichern auf () => {
    setze name auf ui_eingabe_text(g.inpName)
    ui_label_setze(g.lblStatus, "Gespeichert: " + name)
}

setze cb_laden auf () => {
    ui_label_setze(g.lblStatus, "Laden — noch nicht implementiert")
}

setze cb_neu auf () => {
    ui_eingabe_setze(g.inpName, "")
    ui_eingabe_setze(g.inpEmail, "")
    ui_label_setze(g.lblStatus, "Neu — Felder geleert")
}

setze cb_hilfe auf () => {
    ui_info(g.hFenster, "Hilfe", "Trage Name und E-Mail ein, dann auf Speichern.")
}

setze cb_ueber auf () => { zeige_ueber() }


# --- Fenster bauen ---

ui_baue(g, "moo — Modulare App", 620, 520, [
    [setup_app_header,     ["Moo Admin", "v1.0.0"]],
    [setup_user_form,      ["Name:", "E-Mail:", "Kategorie:", 280]],
    [setup_notes_area,     ["Notizen:", 280, 160]],
    [setup_action_buttons, [[
        ["Speichern", cb_speichern],
        ["Laden",     cb_laden],
        ["Neu",       cb_neu],
        ["Hilfe",     cb_hilfe],
        ["Ueber",     cb_ueber],
    ]]],
    [setup_status_bar,     ["Bereit", "0 Eintraege"]],
], resizable = wahr)


# --- Event-Loop ---

ui_zeige(g.hFenster)
ui_laufen()

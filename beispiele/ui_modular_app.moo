# ============================================================
# ui_modular_app.moo — Showcase fuer modulare UI-Komposition
#
# Uebersetzung von AutoIt-GUIBuilder examples/Advanced/
# Demo_Modular_Main.au3 nach moo.
#
# ⚠ Bracket-Syntax bei Dict-Zugriffen (siehe ui_login_demo).
# ============================================================

importiere ui


setze g auf {}


funktion zeige_ueber():
    setze a auf {}
    ui_baue(a, "Ueber moo", 420, 300, [
        [setup_about_info, [
            "moo UI-Showcase",
            "1.0.0",
            "Ein Demo fuer die modulare UI-Komposition. Inspiriert vom AutoIt GUIBuilder. (c) 2026 Moritz Kolar",
        ]],
    ])
    ui_zeige_nebenbei(a["hFenster"])


funktion cb_speichern():
    setze name auf ui_eingabe_text(g["inpName"])
    ui_label_setze(g["lblStatus"], "Gespeichert: " + name)

funktion cb_laden():
    ui_label_setze(g["lblStatus"], "Laden — noch nicht implementiert")

funktion cb_neu():
    ui_eingabe_setze(g["inpName"], "")
    ui_eingabe_setze(g["inpEmail"], "")
    ui_label_setze(g["lblStatus"], "Neu — Felder geleert")

funktion cb_hilfe():
    ui_info(g["hFenster"], "Hilfe", "Trage Name und E-Mail ein, dann auf Speichern.")

funktion cb_ueber():
    zeige_ueber()


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
], wahr)


ui_zeige(g["hFenster"])
ui_laufen()

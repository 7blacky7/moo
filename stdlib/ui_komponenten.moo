# ============================================================
# stdlib/ui_komponenten.moo — Standard-UI-Komponenten
#
# Wiederverwendbare Setup-Bausteine, 1:1 uebernommen aus
# GUIBuilder (Moritz Kolar, https://github.com/7blacky7/GUI_Builder-autoit-)
# examples/Advanced/Demo_Modular_Components.au3
#
# Konvention:
#   - Erster Parameter `g` ist der UI-Container (Dict).
#   - Widgets werden als `g.<name>` abgelegt.
#   - `g.hFenster` muss bereits existieren (ui_baue() erledigt das).
#   - Rueckgabe: wahr bei Erfolg.
#   - Callbacks werden zur Bau-Zeit gebunden (ui_knopf akzeptiert
#     nur Create-Time-Callbacks — kein nachtraegliches Setter-API).
#
# Alle Komponenten arbeiten auf absolutem x/y/w/h-Layout gegen
# das Fenster (GtkFixed auf Linux, entsprechend auf Win/macOS).
# ============================================================


# ------------------------------------------------------------
# setup_login_form — klassisches Anmelde-Formular
#
# Layout:
#   Label (Benutzer)
#   Eingabe
#   Label (Passwort)
#   Eingabe (passwort-masked)
#   [Anmelden] [Abbrechen]
#
# Handles: g.lblUser, g.inpUser, g.lblPass, g.inpPass,
#          g.btnLogin, g.btnCancel
#
# on_login / on_cancel: optionale Callbacks; nichts = Defaults
# (Anmelden meldet Status, Abbrechen beendet die Event-Loop).
# ------------------------------------------------------------

funktion setup_login_form(g, user_label, pass_label, btn_breite, on_login = nichts, on_cancel = nichts):
    setze f auf g.hFenster
    g.lblUser  = ui_label(f, user_label, 20, 20, 120, 20)
    g.inpUser  = ui_eingabe(f, 20, 44, 260, 26, "", falsch)
    g.lblPass  = ui_label(f, pass_label, 20, 80, 120, 20)
    g.inpPass  = ui_eingabe(f, 20, 104, 260, 26, "", wahr)

    wenn on_login == nichts:
        setze on_login auf () => { zeige "[login] Anmelden geklickt" }
    wenn on_cancel == nichts:
        setze on_cancel auf () => { ui_beenden() }

    setze y auf 150
    g.btnLogin  = ui_knopf(f, "Anmelden",  20,                   y, btn_breite, 30, on_login)
    g.btnCancel = ui_knopf(f, "Abbrechen", 20 + btn_breite + 10, y, btn_breite, 30, on_cancel)
    gib_zurück wahr


# ------------------------------------------------------------
# setup_user_form — Benutzer-Stammdaten-Formular
#
# 3 beschriftete Eingabefelder + Kategorie-Dropdown.
#
# Handles: g.lblName, g.inpName, g.lblEmail, g.inpEmail,
#          g.lblCat, g.ddCat
# ------------------------------------------------------------

funktion setup_user_form(g, name_label, email_label, cat_label, breite):
    setze f auf g.hFenster
    setze y auf 80

    g.lblName = ui_label(f, name_label, 20, y, breite, 20)
    g.inpName = ui_eingabe(f, 20, y + 22, breite, 26, "", falsch)
    setze y auf y + 60

    g.lblEmail = ui_label(f, email_label, 20, y, breite, 20)
    g.inpEmail = ui_eingabe(f, 20, y + 22, breite, 26, "", falsch)
    setze y auf y + 60

    g.lblCat = ui_label(f, cat_label, 20, y, breite, 20)
    g.ddCat  = ui_dropdown(f, ["Administrator", "Benutzer", "Gast"], 20, y + 22, breite, 26, () => { })
    gib_zurück wahr


# ------------------------------------------------------------
# setup_toolbar — horizontale Werkzeug-Leiste
#
# buttons_liste: Liste — Elemente duerfen sein:
#   "Text"                        → Default-Callback (print)
#   ["Text", () => { ... }]       → eigener Callback
#
# Handles: g.toolbar  — Liste der Button-Handles
# ------------------------------------------------------------

funktion setup_toolbar(g, buttons_liste):
    setze f auf g.hFenster
    setze x auf 10
    setze y auf 10
    setze bb auf 90
    setze h auf 28
    g.toolbar = []
    für eintrag in buttons_liste:
        setze text auf eintrag
        setze cb auf nichts
        wenn typ_von(eintrag) == "Liste":
            setze text auf eintrag[0]
            wenn länge(eintrag) > 1:
                setze cb auf eintrag[1]
        wenn cb == nichts:
            setze cb auf () => { zeige "[toolbar] " + text }
        setze btn auf ui_knopf(f, text, x, y, bb, h, cb)
        g.toolbar.hinzufügen(btn)
        setze x auf x + bb + 6
    gib_zurück wahr


# ------------------------------------------------------------
# setup_status_bar — Status-Leiste am unteren Fensterrand
#
# Zwei Labels: links Status, rechts Info.
# Positioniert sich anhand g.hoehe (von ui_baue gesetzt).
#
# Handles: g.trennerStatus, g.lblStatus, g.lblInfo
# ------------------------------------------------------------

funktion setup_status_bar(g, status_text, info_text):
    setze f auf g.hFenster
    setze b auf g.breite
    wenn b == nichts:
        setze b auf 600
    setze h auf g.hoehe
    wenn h == nichts:
        setze h auf 400
    setze y auf h - 28

    g.trennerStatus = ui_trenner(f, 0, y - 4, b, 1)
    g.lblStatus = ui_label(f, status_text, 10, y, b / 2, 20)
    g.lblInfo   = ui_label(f, info_text,   b / 2, y, b / 2 - 10, 20)
    gib_zurück wahr


# ------------------------------------------------------------
# setup_app_header — grosser Titel + Version + Trenner
#
# Handles: g.lblHeader, g.lblVersion, g.trennerHeader
# ------------------------------------------------------------

funktion setup_app_header(g, titel, version):
    setze f auf g.hFenster
    setze b auf g.breite
    wenn b == nichts:
        setze b auf 600
    g.lblHeader     = ui_label(f, titel,   15, 10, b - 140, 28)
    g.lblVersion    = ui_label(f, version, b - 120, 16, 110, 18)
    g.trennerHeader = ui_trenner(f, 0, 48, b, 2)
    gib_zurück wahr


# ------------------------------------------------------------
# setup_action_buttons — Reihe Aktions-Buttons
#
# buttons_liste: Liste — Elemente wie bei setup_toolbar:
#   "Text"                        → Default-Callback
#   ["Text", () => { ... }]       → eigener Callback
#
# Handles: g.actions — Liste der Button-Handles (in Eingangsreihenfolge)
# ------------------------------------------------------------

funktion setup_action_buttons(g, buttons_liste):
    setze f auf g.hFenster
    setze h auf g.hoehe
    wenn h == nichts:
        setze h auf 400
    setze y auf h - 70

    setze bb auf 110
    setze x auf 15
    g.actions = []
    für eintrag in buttons_liste:
        setze text auf eintrag
        setze cb auf nichts
        wenn typ_von(eintrag) == "Liste":
            setze text auf eintrag[0]
            wenn länge(eintrag) > 1:
                setze cb auf eintrag[1]
        wenn cb == nichts:
            setze cb auf () => { zeige "[action] " + text }
        setze btn auf ui_knopf(f, text, x, y, bb, 30, cb)
        g.actions.hinzufügen(btn)
        setze x auf x + bb + 8
    gib_zurück wahr


# ------------------------------------------------------------
# setup_notes_area — beschrifteter mehrzeiliger Textbereich
#
# Handles: g.lblNotes, g.txtNotes
# ------------------------------------------------------------

funktion setup_notes_area(g, label, breite, hoehe):
    setze f auf g.hFenster
    setze y auf 60
    g.lblNotes = ui_label(f, label, 20, y, breite, 20)
    g.txtNotes = ui_textbereich(f, 20, y + 22, breite, hoehe)
    gib_zurück wahr


# ------------------------------------------------------------
# setup_about_info — Info-Ueber-Inhalt
#
# Legt Titel, Version und Beschreibung in das Fenster.
# Ideal fuer ein separates About-Fenster.
#
# Handles: g.lblAboutTitel, g.lblAboutVersion,
#          g.trennerAbout, g.lblAboutBeschreibung
# ------------------------------------------------------------

funktion setup_about_info(g, titel, version, beschreibung):
    setze f auf g.hFenster
    g.lblAboutTitel        = ui_label(f, titel,                 20, 20, 360, 28)
    g.lblAboutVersion      = ui_label(f, "Version: " + version, 20, 54, 360, 20)
    g.trennerAbout         = ui_trenner(f, 0, 84, 400, 1)
    g.lblAboutBeschreibung = ui_label(f, beschreibung,          20, 94, 360, 180)
    gib_zurück wahr

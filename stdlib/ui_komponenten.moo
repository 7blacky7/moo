# ============================================================
# stdlib/ui_komponenten.moo — Standard-UI-Komponenten
#
# Nach GUIBuilder (Moritz Kolar).
#
# ⚠ Widget-Handles werden mit `g["key"]` abgelegt (Bracket-Syntax).
# Hintergrund: Dot-Access (`g.key`) auf Dicts mit Pointer-tagged
# MooValues (Widgets, Fenster) segfaultet aktuell im Runtime.
# Bug reported — Workaround: nur Brackets.
#
# Signatur-Kontrakt:
#   Jede Setup-Funktion nimmt `(g, p)` entgegen — `g` ist der
#   Container-Dict, `p` ist eine Liste der Parameter. Der Compiler
#   begrenzt indirekte Aufrufe auf 3 Args, darum ein Listen-Param.
# ============================================================


funktion ui_default_noop():
    gib_zurück nichts

funktion ui_default_login_cb():
    zeige "[login] Anmelden geklickt"

funktion ui_default_cancel_cb():
    ui_beenden()


# setup_login_form
# p = [user_label, pass_label, btn_breite, on_login?, on_cancel?]
funktion setup_login_form(g, p):
    setze f auf g["hFenster"]
    setze user_label auf p[0]
    setze pass_label auf p[1]
    setze btn_breite auf p[2]

    setze on_login auf ui_default_login_cb
    setze on_cancel auf ui_default_cancel_cb
    wenn länge(p) > 3:
        wenn p[3] != nichts:
            setze on_login auf p[3]
    wenn länge(p) > 4:
        wenn p[4] != nichts:
            setze on_cancel auf p[4]

    g["lblUser"]  = ui_label(f, user_label, 20, 20, 120, 20)
    g["inpUser"]  = ui_eingabe(f, 20, 44, 260, 26, "", falsch)
    g["lblPass"]  = ui_label(f, pass_label, 20, 80, 120, 20)
    g["inpPass"]  = ui_eingabe(f, 20, 104, 260, 26, "", wahr)

    setze y auf 150
    g["btnLogin"]  = ui_knopf(f, "Anmelden",  20,                   y, btn_breite, 30, on_login)
    g["btnCancel"] = ui_knopf(f, "Abbrechen", 20 + btn_breite + 10, y, btn_breite, 30, on_cancel)
    gib_zurück wahr


# setup_user_form
# p = [name_label, email_label, cat_label, breite]
funktion setup_user_form(g, p):
    setze f auf g["hFenster"]
    setze name_label auf p[0]
    setze email_label auf p[1]
    setze cat_label auf p[2]
    setze breite auf p[3]

    setze y auf 80
    g["lblName"] = ui_label(f, name_label, 20, y, breite, 20)
    g["inpName"] = ui_eingabe(f, 20, y + 22, breite, 26, "", falsch)
    setze y auf y + 60

    g["lblEmail"] = ui_label(f, email_label, 20, y, breite, 20)
    g["inpEmail"] = ui_eingabe(f, 20, y + 22, breite, 26, "", falsch)
    setze y auf y + 60

    g["lblCat"] = ui_label(f, cat_label, 20, y, breite, 20)
    g["ddCat"]  = ui_dropdown(f, ["Administrator", "Benutzer", "Gast"], 20, y + 22, breite, 26, ui_default_noop)
    gib_zurück wahr


# setup_toolbar
# p = [buttons_liste]  — Elemente: "Text" oder [text, callback]
funktion setup_toolbar(g, p):
    setze f auf g["hFenster"]
    setze buttons_liste auf p[0]
    setze x auf 10
    setze y auf 10
    setze bb auf 90
    setze h auf 28
    setze tb auf []
    für eintrag in buttons_liste:
        setze text auf eintrag
        setze cb auf ui_default_noop
        wenn typ_von(eintrag) == "Liste":
            setze text auf eintrag[0]
            wenn länge(eintrag) > 1:
                setze cb auf eintrag[1]
        setze btn auf ui_knopf(f, text, x, y, bb, h, cb)
        tb.hinzufügen(btn)
        setze x auf x + bb + 6
    g["toolbar"] = tb
    gib_zurück wahr


# setup_status_bar
# p = [status_text, info_text]
funktion setup_status_bar(g, p):
    setze f auf g["hFenster"]
    setze status_text auf p[0]
    setze info_text auf p[1]

    setze b auf g["breite"]
    wenn b == nichts:
        setze b auf 600
    setze h auf g["hoehe"]
    wenn h == nichts:
        setze h auf 400
    setze y auf h - 28

    g["trennerStatus"] = ui_trenner(f, 0, y - 4, b, 1)
    g["lblStatus"] = ui_label(f, status_text, 10, y, b / 2, 20)
    g["lblInfo"]   = ui_label(f, info_text,   b / 2, y, b / 2 - 10, 20)
    gib_zurück wahr


# setup_app_header
# p = [titel, version]
funktion setup_app_header(g, p):
    setze f auf g["hFenster"]
    setze titel auf p[0]
    setze version auf p[1]

    setze b auf g["breite"]
    wenn b == nichts:
        setze b auf 600
    g["lblHeader"]     = ui_label(f, titel,   15, 10, b - 140, 28)
    g["lblVersion"]    = ui_label(f, version, b - 120, 16, 110, 18)
    g["trennerHeader"] = ui_trenner(f, 0, 48, b, 2)
    gib_zurück wahr


# setup_action_buttons
# p = [buttons_liste]
funktion setup_action_buttons(g, p):
    setze f auf g["hFenster"]
    setze buttons_liste auf p[0]

    setze h auf g["hoehe"]
    wenn h == nichts:
        setze h auf 400
    setze y auf h - 70
    setze bb auf 110
    setze x auf 15
    setze acts auf []
    für eintrag in buttons_liste:
        setze text auf eintrag
        setze cb auf ui_default_noop
        wenn typ_von(eintrag) == "Liste":
            setze text auf eintrag[0]
            wenn länge(eintrag) > 1:
                setze cb auf eintrag[1]
        setze btn auf ui_knopf(f, text, x, y, bb, 30, cb)
        acts.hinzufügen(btn)
        setze x auf x + bb + 8
    g["actions"] = acts
    gib_zurück wahr


# setup_notes_area
# p = [label, breite, hoehe]
funktion setup_notes_area(g, p):
    setze f auf g["hFenster"]
    setze label auf p[0]
    setze breite auf p[1]
    setze hoehe auf p[2]

    setze y auf 60
    g["lblNotes"] = ui_label(f, label, 20, y, breite, 20)
    g["txtNotes"] = ui_textbereich(f, 20, y + 22, breite, hoehe)
    gib_zurück wahr


# setup_about_info
# p = [titel, version, beschreibung]
funktion setup_about_info(g, p):
    setze f auf g["hFenster"]
    setze titel auf p[0]
    setze version auf p[1]
    setze beschreibung auf p[2]

    g["lblAboutTitel"]        = ui_label(f, titel,                 20, 20, 360, 28)
    g["lblAboutVersion"]      = ui_label(f, "Version: " + version, 20, 54, 360, 20)
    g["trennerAbout"]         = ui_trenner(f, 0, 84, 400, 1)
    g["lblAboutBeschreibung"] = ui_label(f, beschreibung,          20, 94, 360, 180)
    gib_zurück wahr

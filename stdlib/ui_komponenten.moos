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


# ============================================================
# Phase 2b — Zusatz-Komponenten (Settings, File-Browser, Wizard,
# Shortcuts, Suchleiste, Listen-mit-Aktion, Log-Konsole, Status+)
# ============================================================


# setup_settings_panel
# p = [categories_liste, width]
#
# Tab-Container mit einem Tab pro Kategorie. User fuellt die Tabs
# selbst (tab-Handles werden als g["tabCat_<index>"] und
# g["tab_<Name>"] abgelegt).
#
# Handles: g["tabsSettings"], g["tabCat_0"]...g["tabCat_N"],
#          g["tab_<Name>"] (sanitized key)
funktion setup_settings_panel(g, p):
    setze f auf g["hFenster"]
    setze categories_liste auf p[0]
    setze breite auf p[1]

    setze h auf g["hoehe"]
    wenn h == nichts:
        setze h auf 400

    g["tabsSettings"] = ui_tabs(f, 10, 60, breite, h - 100)
    setze i auf 0
    für name in categories_liste:
        setze tab auf ui_tab_hinzu(g["tabsSettings"], name)
        g["tabCat_" + i] = tab
        g["tab_" + name] = tab
        setze i auf i + 1
    gib_zurück wahr


# setup_file_browser
# p = [label, start_pfad, breite, hoehe]
#
# Listen-Widget fuer Datei-Auswahl + Filter-Eingabe + Oeffnen/Abbrechen-Knoepfe.
# Dateien werden nicht automatisch geladen — der User ruft
# ui_liste_zeile_hinzu(g["listFiles"], ["dateiname"]) selbst.
#
# Handles: g["lblFiles"], g["inpFilter"], g["listFiles"],
#          g["btnFileOpen"], g["btnFileCancel"]
funktion setup_file_browser(g, p):
    setze f auf g["hFenster"]
    setze label auf p[0]
    setze start_pfad auf p[1]
    setze breite auf p[2]
    setze hoehe auf p[3]

    g["startPfad"]     = start_pfad
    g["lblFiles"]      = ui_label(f, label, 10, 10, breite, 20)
    g["inpFilter"]     = ui_eingabe(f, 10, 34, breite, 26, "*.*", falsch)
    g["listFiles"]     = ui_liste(f, ["Name"], 10, 66, breite, hoehe - 130)
    setze by auf hoehe - 50
    g["btnFileOpen"]   = ui_knopf(f, "Oeffnen",   10,            by, 110, 30, ui_default_noop)
    g["btnFileCancel"] = ui_knopf(f, "Abbrechen", 130,           by, 110, 30, ui_default_cancel_cb)
    gib_zurück wahr


# setup_wizard_pages
# p = [seiten_titel, zeige_zurueck?, zeige_weiter?]
#
# Mehrschritt-Wizard. ui_tabs mit einem Tab je Seite (versteckt
# durch ui_tabs_auswahl_setze). Fortschrittsbalken oben, Zurueck/
# Weiter/Abbrechen-Knoepfe unten. Event-Logik (welcher Tab aktiv,
# Enable-Zustaende) muss der User selbst machen.
#
# Handles: g["wizardSeiten"], g["wizardFortschritt"],
#          g["btnZurueck"], g["btnWeiter"], g["btnAbbrechen"]
funktion setup_wizard_pages(g, p):
    setze f auf g["hFenster"]
    setze seiten_titel auf p[0]
    setze zeige_zurueck auf wahr
    setze zeige_weiter auf wahr
    wenn länge(p) > 1:
        setze zeige_zurueck auf p[1]
    wenn länge(p) > 2:
        setze zeige_weiter auf p[2]

    setze b auf g["breite"]
    wenn b == nichts:
        setze b auf 600
    setze h auf g["hoehe"]
    wenn h == nichts:
        setze h auf 400

    g["wizardFortschritt"] = ui_fortschritt(f, 10, 10, b - 20, 14)
    g["wizardSeiten"] = ui_tabs(f, 10, 36, b - 20, h - 110)
    setze seiten_handles auf []
    für titel in seiten_titel:
        setze tab auf ui_tab_hinzu(g["wizardSeiten"], titel)
        seiten_handles.hinzufügen(tab)
    g["wizardSeitenHandles"] = seiten_handles

    setze by auf h - 60
    wenn zeige_zurueck:
        g["btnZurueck"] = ui_knopf(f, "< Zurueck", 10,     by, 100, 30, ui_default_noop)
    wenn zeige_weiter:
        g["btnWeiter"] = ui_knopf(f, "Weiter >",  120,    by, 100, 30, ui_default_noop)
    g["btnAbbrechen"]  = ui_knopf(f, "Abbrechen", b - 120, by, 110, 30, ui_default_cancel_cb)
    gib_zurück wahr


# setup_keyboard_shortcuts
# p = [shortcuts_liste]
#
# STUB-Version: Die Runtime-Funktion ui_shortcut_bind(fenster, seq, cb)
# existiert aktuell noch nicht in moo_ui.h. Wir legen die Struktur an,
# loggen die geplanten Bindings und warten auf ui-arch.
#
# Handles: g["shortcuts"] — Liste [shortcut_string, callback]-Paare
funktion setup_keyboard_shortcuts(g, p):
    setze shortcuts_liste auf p[0]
    g["shortcuts"] = shortcuts_liste
    zeige "[ui_shortcuts] STUB — " + länge(shortcuts_liste) + " Bindings vorgemerkt (Runtime fehlt)"
    für eintrag in shortcuts_liste:
        zeige "  " + eintrag[0] + " -> <callback>"
    gib_zurück wahr


# setup_suchleiste
# p = [platzhalter, on_suche?]
#
# Eingabefeld mit Live-Callback pro Tastenanschlag (via
# ui_eingabe_on_change). Ikon-Knopf rechts.
#
# Handles: g["inpSuche"], g["btnSuche"]
funktion setup_suchleiste(g, p):
    setze f auf g["hFenster"]
    setze platzhalter auf p[0]
    setze on_suche auf nichts
    wenn länge(p) > 1:
        setze on_suche auf p[1]

    setze b auf g["breite"]
    wenn b == nichts:
        setze b auf 600

    g["inpSuche"] = ui_eingabe(f, 10, 10, b - 130, 28, platzhalter, falsch)
    g["btnSuche"] = ui_knopf(f, "Suchen", b - 115, 10, 100, 28, ui_default_noop)
    wenn on_suche != nichts:
        ui_eingabe_on_change(g["inpSuche"], on_suche)
    gib_zurück wahr


# setup_liste_mit_aktion
# p = [spalten, action_buttons]
#
# Listen-Widget + Button-Reihe darunter. action_buttons: Liste aus
# Strings oder [text, callback]-Paaren.
#
# Handles: g["listHaupt"], g["listAktionen"] (Liste Button-Handles)
funktion setup_liste_mit_aktion(g, p):
    setze f auf g["hFenster"]
    setze spalten auf p[0]
    setze action_buttons auf p[1]

    setze b auf g["breite"]
    wenn b == nichts:
        setze b auf 600
    setze h auf g["hoehe"]
    wenn h == nichts:
        setze h auf 400

    g["listHaupt"] = ui_liste(f, spalten, 10, 10, b - 20, h - 100)

    setze by auf h - 70
    setze x auf 10
    setze bb auf 120
    setze acts auf []
    für eintrag in action_buttons:
        setze text auf eintrag
        setze cb auf ui_default_noop
        wenn typ_von(eintrag) == "Liste":
            setze text auf eintrag[0]
            wenn länge(eintrag) > 1:
                setze cb auf eintrag[1]
        setze btn auf ui_knopf(f, text, x, by, bb, 30, cb)
        acts.hinzufügen(btn)
        setze x auf x + bb + 8
    g["listAktionen"] = acts
    gib_zurück wahr


# setup_log_konsole
# p = [breite, hoehe, max_zeilen?]
#
# Scroll-Container + read-only Textbereich + "Loeschen"-Knopf.
#
# Handles: g["logText"], g["btnLogClear"], g["logMaxZeilen"]
#
# Helper:
#   log_anhaengen(g, text)  - fuegt Text + "\n" an, autoscroll
#   log_leeren(g)           - leert den Log
funktion setup_log_konsole(g, p):
    setze f auf g["hFenster"]
    setze breite auf p[0]
    setze hoehe auf p[1]
    setze max_zeilen auf 1000
    wenn länge(p) > 2:
        setze max_zeilen auf p[2]
    g["logMaxZeilen"] = max_zeilen

    g["logText"]     = ui_textbereich(f, 10, 40, breite, hoehe - 50)
    ui_aktiv(g["logText"], falsch)
    g["btnLogClear"] = ui_knopf(f, "Loeschen", 10, 5, 100, 28, log_leeren_cb)
    gib_zurück wahr


funktion log_anhaengen(g, text):
    ui_aktiv(g["logText"], wahr)
    ui_textbereich_anhaengen(g["logText"], text + "\n")
    ui_aktiv(g["logText"], falsch)


funktion log_leeren(g):
    ui_aktiv(g["logText"], wahr)
    ui_textbereich_setze(g["logText"], "")
    ui_aktiv(g["logText"], falsch)


# Default-Callback fuer Log-Clear — funktioniert nur wenn ein globales
# `g` im Modul-Scope existiert. Echte Apps binden den eigenen Handler.
funktion log_leeren_cb():
    zeige "[log] Loeschen (bitte eigenen Callback binden)"


# setup_statusbar_erweitert
# p = [links_text, rechts_text, mit_fortschritt?]
#
# Erweiterte Status-Leiste: Label links + Label rechts + optional
# Fortschrittsbalken dazwischen.
#
# Handles: g["lblStatusLinks"], g["lblStatusRechts"],
#          g["statusFortschritt"] (nur wenn mit_fortschritt=wahr)
funktion setup_statusbar_erweitert(g, p):
    setze f auf g["hFenster"]
    setze links_text auf p[0]
    setze rechts_text auf p[1]
    setze mit_fortschritt auf falsch
    wenn länge(p) > 2:
        setze mit_fortschritt auf p[2]

    setze b auf g["breite"]
    wenn b == nichts:
        setze b auf 600
    setze h auf g["hoehe"]
    wenn h == nichts:
        setze h auf 400
    setze y auf h - 28

    g["trennerStatusPlus"] = ui_trenner(f, 0, y - 4, b, 1)
    setze links_b auf b / 3
    setze mitte_b auf b / 3
    setze rechts_b auf b - links_b - mitte_b - 20

    g["lblStatusLinks"]  = ui_label(f, links_text, 10, y, links_b, 20)
    wenn mit_fortschritt:
        g["statusFortschritt"] = ui_fortschritt(f, links_b + 10, y + 4, mitte_b, 14)
    g["lblStatusRechts"] = ui_label(f, rechts_text, b - rechts_b - 10, y, rechts_b, 20)
    gib_zurück wahr

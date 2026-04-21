# ============================================================
# stdlib/ui_baue.moo — Master-Constructor fuer UI-Komposition
#
# Schicht 2 der moo-UI-Architektur. Inspiriert von
# AutoIt GUIBuilder's _GUI_Create_ (Moritz Kolar).
#
# Idee:
#   - Ein Dict als GUI-Container (hFenster, inpUser, btnLogin, ...)
#   - Eine Liste von Setup-Funktionen baut alle Widgets
#   - ui_baue() klebt das zusammen: Fenster + alle Setups
#
# Nutzung:
#   importiere ui
#
#   setze g auf {}
#   ui_baue(g, "Login", 450, 400, [
#       [setup_login_form, ["Benutzer:", "Passwort:", 100]],
#       [setup_status_bar, ["Bereit", "OK"]],
#   ])
#   ui_zeige(g.hFenster)
#   ui_laufen()
# ============================================================


# ------------------------------------------------------------
# rufe_mit(funk, args) — Apply-Helper
#
# moo hat Spread (..liste) als Listen-Literal-Feature, aber nicht
# garantiert als Call-Site-Spread. Wir dispatchen manuell ueber die
# Listenlaenge. Deckt bis 10 Parameter ab — ausreichend fuer alle
# Setup-Funktionen im Standard-Komponenten-Satz.
# ------------------------------------------------------------

funktion rufe_mit(funk, args):
    setze n auf länge(args)
    wenn n == 0:
        gib_zurück funk()
    wenn n == 1:
        gib_zurück funk(args[0])
    wenn n == 2:
        gib_zurück funk(args[0], args[1])
    wenn n == 3:
        gib_zurück funk(args[0], args[1], args[2])
    wenn n == 4:
        gib_zurück funk(args[0], args[1], args[2], args[3])
    wenn n == 5:
        gib_zurück funk(args[0], args[1], args[2], args[3], args[4])
    wenn n == 6:
        gib_zurück funk(args[0], args[1], args[2], args[3], args[4], args[5])
    wenn n == 7:
        gib_zurück funk(args[0], args[1], args[2], args[3], args[4], args[5], args[6])
    wenn n == 8:
        gib_zurück funk(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7])
    wenn n == 9:
        gib_zurück funk(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8])
    gib_zurück funk(args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9])


# ------------------------------------------------------------
# ui_baue — Master-Constructor
#
# container   — Dict, in dem Widget-Handles abgelegt werden
#               (container.hFenster ist das Top-Level-Fenster)
# titel       — Fenstertitel
# breite/hoehe — Groesse in Pixeln
# setups      — Liste [[funk, [params...]], ...]
# resizable   — darf der User das Fenster groesse-aendern (Standard: falsch)
# maximiert   — beim Start maximieren (Standard: falsch)
# parent      — Parent-Fenster (Standard: nichts = Top-Level)
# ------------------------------------------------------------

funktion ui_baue(container, titel, breite, hoehe, setups, resizable = falsch, maximiert = falsch, parent = nichts):
    # 1) Fenster erzeugen und ablegen
    container.hFenster = ui_fenster(titel, breite, hoehe, resizable, maximiert, parent)
    container.breite = breite
    container.hoehe = hoehe

    # 2) Alle Setup-Funktionen in Reihenfolge aufrufen
    für eintrag in setups:
        setze funk auf eintrag[0]
        setze params auf []
        wenn länge(eintrag) > 1:
            setze params auf eintrag[1]
        rufe_mit(funk, [container] + params)

    gib_zurück wahr


# ------------------------------------------------------------
# ui_debug_baue — wie ui_baue, aber mit Log-Output
# (vgl. GUIBuilder __showguilog)
# ------------------------------------------------------------

funktion ui_debug_baue(container, titel, breite, hoehe, setups, resizable = falsch, maximiert = falsch, parent = nichts):
    zeige "[ui_baue] Fenster '" + titel + "' " + breite + "x" + hoehe
    container.hFenster = ui_fenster(titel, breite, hoehe, resizable, maximiert, parent)
    container.breite = breite
    container.hoehe = hoehe

    setze i auf 0
    für eintrag in setups:
        setze funk auf eintrag[0]
        setze params auf []
        wenn länge(eintrag) > 1:
            setze params auf eintrag[1]
        zeige "[ui_baue]  setup #" + i + " mit " + länge(params) + " params"
        setze ergebnis auf rufe_mit(funk, [container] + params)
        zeige "[ui_baue]  setup #" + i + " -> " + ergebnis
        setze i auf i + 1

    gib_zurück wahr

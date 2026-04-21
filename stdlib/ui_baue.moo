# ============================================================
# stdlib/ui_baue.moo — Master-Constructor fuer UI-Komposition
#
# Schicht 2 der moo-UI-Architektur. Inspiriert vom AutoIt
# GUIBuilder `_GUI_Create_` (Moritz Kolar).
#
# ⚠ Speichert Widget-Handles strikt mit `g["key"]` (Bracket-Syntax).
# Dot-Access `g.key` auf Dicts, die Pointer-tagged MooValues halten,
# fuehrt aktuell (2026-04-21) zu Segfaults im Runtime — Bug offen,
# Workaround: Brackets.
#
# Idee:
#   - Ein Dict als GUI-Container (hFenster, inpUser, btnLogin, ...)
#   - Eine Liste von Setup-Funktionen baut alle Widgets
#   - ui_baue() klebt das zusammen: Fenster + alle Setups
#
# Setup-Kontrakt (WICHTIG):
#   Jede Setup-Funktion hat die Signatur `setup_X(g, p)` — `p` ist
#   eine Liste aller Parameter. Das ist notwendig, weil der Compiler
#   indirekte Funktionsaufrufe auf maximal 3 Argumente begrenzt.
#
# Nutzung:
#   importiere ui
#   setze g auf {}
#   ui_baue(g, "Login", 450, 400, [
#       [setup_login_form, ["Benutzer:", "Passwort:", 100]],
#       [setup_status_bar, ["Bereit", "OK"]],
#   ])
#   ui_zeige(g["hFenster"])
#   ui_laufen()
# ============================================================


konstante UI_FLAG_NONE       auf 0
konstante UI_FLAG_RESIZABLE  auf 1
konstante UI_FLAG_MAXIMIZED  auf 2
konstante UI_FLAG_FULLSCREEN auf 4
konstante UI_FLAG_MODAL      auf 8
konstante UI_FLAG_NO_DECOR   auf 16
konstante UI_FLAG_ALWAYS_TOP auf 32


funktion ui_flags(resizable = falsch, maximiert = falsch, fullscreen = falsch, modal = falsch):
    setze f auf 0
    wenn resizable:
        setze f auf f + UI_FLAG_RESIZABLE
    wenn maximiert:
        setze f auf f + UI_FLAG_MAXIMIZED
    wenn fullscreen:
        setze f auf f + UI_FLAG_FULLSCREEN
    wenn modal:
        setze f auf f + UI_FLAG_MODAL
    gib_zurück f


funktion ui_baue(container, titel, breite, hoehe, setups, resizable = falsch, maximiert = falsch, parent = nichts):
    setze flags auf ui_flags(resizable, maximiert)
    container["hFenster"] = ui_fenster(titel, breite, hoehe, flags, parent)
    container["breite"] = breite
    container["hoehe"] = hoehe

    für eintrag in setups:
        setze funk auf eintrag[0]
        setze params auf []
        wenn länge(eintrag) > 1:
            setze params auf eintrag[1]
        funk(container, params)

    gib_zurück wahr


funktion ui_debug_baue(container, titel, breite, hoehe, setups, resizable = falsch, maximiert = falsch, parent = nichts):
    zeige "[ui_baue] Fenster '" + titel + "' " + breite + "x" + hoehe
    setze flags auf ui_flags(resizable, maximiert)
    container["hFenster"] = ui_fenster(titel, breite, hoehe, flags, parent)
    container["breite"] = breite
    container["hoehe"] = hoehe

    setze i auf 0
    für eintrag in setups:
        setze funk auf eintrag[0]
        setze params auf []
        wenn länge(eintrag) > 1:
            setze params auf eintrag[1]
        zeige "[ui_baue]  setup #" + i + " mit " + länge(params) + " params"
        setze ergebnis auf funk(container, params)
        zeige "[ui_baue]  setup #" + i + " -> " + ergebnis
        setze i auf i + 1

    gib_zurück wahr

# P016 V1: UI-only Frame-Backend muss explizit fail-closed sein.
# Kein Fenster, kein Desktop-Input und keine Capture-API.
# Dieser Vertrag wird mit einem moo_ui-only Compiler gebaut und ausgefuehrt.

importiere ui_moo

konstante NICHT_VERFUEGBAR auf "Frame-Backend ist in diesem Build nicht verfuegbar (kein 3D-/SDL-Backend mitgebaut)"

funktion rechteck_wirft():
    versuche:
        zeichne_rechteck(nichts, 0, 0, 1, 1, "#000000")
        gib_zurück falsch
    fange fehler:
        gib_zurück text(fehler) == NICHT_VERFUEGBAR

funktion kreis_wirft():
    versuche:
        zeichne_kreis(nichts, 0, 0, 1, "#000000")
        gib_zurück falsch
    fange fehler:
        gib_zurück text(fehler) == NICHT_VERFUEGBAR

funktion linie_wirft():
    versuche:
        zeichne_linie(nichts, 0, 0, 1, 1, "#000000")
        gib_zurück falsch
    fange fehler:
        gib_zurück text(fehler) == NICHT_VERFUEGBAR

wenn rechteck_wirft() == falsch:
    wirf "Frame-Rechteck war nicht explizit unverfuegbar"
wenn kreis_wirft() == falsch:
    wirf "Frame-Kreis war nicht explizit unverfuegbar"
wenn linie_wirft() == falsch:
    wirf "Frame-Linie war nicht explizit unverfuegbar"

zeige "P016-V1-UI-ONLY-FRAME-UNAVAILABLE-OK"

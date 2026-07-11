# ============================================================
# beispiele/ui_moo_spiel_overlay.moo — ui_moo als In-Game-Menü
#
# Zeigt das Frame-Backend (UIMOO-6): ein Pausemenü-Overlay über
# einem laufenden "Spielfeld" auf der 2D-API. Dasselbe Widget-Set
# wie am Desktop — Knopf, Slider, Checkbox — per Polling bedient.
#
# Bedienung: P = Pause an/aus, Maus bedient das Menü, Escape = Ende.
# Headless-Smoke: UIMOO_DEMO_AUTO=1 rendert 3 Ticks + beendet.
# ============================================================

importiere ui_moo

konstante B auf 640
konstante H auf 480

setze g auf {}
g["laeuft"] = wahr
g["pause"] = falsch
g["spieler_x"] = 100
g["musik"] = 70

funktion auf_weiter(w):
    g["pause"] = falsch
    gib_zurück wahr

funktion auf_ende(w):
    g["laeuft"] = falsch
    gib_zurück wahr

funktion auf_musik(w, wert):
    g["musik"] = wert
    gib_zurück wahr

funktion nix(w, wert):
    gib_zurück wahr

# HUD (immer sichtbar) + Pausemenü (nur bei Pause) als zwei Wurzeln
setze hud auf uim_frame_wurzel(B, H)
uim_hinzu(hud, uim_label("LEBEN: 3   PUNKTE: 1240", 12, 10, 200, 12))

setze menue auf uim_frame_wurzel(B, H)
setze mp auf uim_hinzu(menue, uim_panel("PAUSE", 200, 120, 240, 220))
uim_hinzu(mp, uim_knopf("WEITER", 40, 40, 160, 32, auf_weiter))
uim_hinzu(mp, uim_label("MUSIK", 40, 92, 100, 12))
uim_hinzu(mp, uim_slider(40, 110, 160, 24, 0, 100, 70, auf_musik))
uim_hinzu(mp, uim_checkbox("VOLLBILD", 40, 148, 160, 20, falsch, nix))
uim_hinzu(mp, uim_knopf("BEENDEN", 40, 180, 160, 28, auf_ende))

funktion spielfeld_zeichnen(win, tick):
    zeichne_rechteck(win, 0, 0, B, H, "#182430")
    zeichne_rechteck(win, 0, H - 60, B, 60, "#2A4A2A")
    # "Spieler" bewegt sich solange keine Pause
    zeichne_rechteck(win, g["spieler_x"], H - 92, 32, 32, "#E0B040")
    zeichne_kreis(win, 520, 90, 30, "#F0E0A0")
    gib_zurück wahr

setze win auf fenster_erstelle("ui_moo Spiel-Overlay", B, H)
setze auto auf umgebung("UIMOO_DEMO_AUTO") != nichts
setze tick auf 0

solange g["laeuft"]:
    wenn g["pause"] == falsch:
        g["spieler_x"] = g["spieler_x"] + 2
        wenn g["spieler_x"] > B - 32:
            g["spieler_x"] = 0

    spielfeld_zeichnen(win, tick)
    uim_frame_zeichne(hud, win)

    # Eingaben pollen und je nach Modus verteilen
    setze mx auf maus_x(win)
    setze my auf maus_y(win)
    setze md auf maus_gedrückt(win)
    wenn taste_gedrückt("p"):
        g["pause"] = wahr
    wenn taste_gedrückt("escape"):
        g["laeuft"] = falsch

    wenn g["pause"]:
        # Abdunkeln + Menü als Overlay, Events ans Menü
        zeichne_rechteck(win, 0, 0, B, H, "#101010")
        uim_frame_zeichne(menue, win)
        uim_frame_maus(menue, mx, my, md)

    fenster_aktualisieren(win)
    setze tick auf tick + 1
    wenn auto:
        wenn tick == 2:
            g["pause"] = wahr
        wenn tick >= 3:
            g["laeuft"] = falsch

fenster_schliessen(win)
wenn auto:
    zeige "UIMOO-OVERLAY-OK"

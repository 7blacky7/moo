# ============================================================
# beispiele/tests/ui_moo_frame_test.moo — UIMOO-6 Frame-Backend
#
# ui_moo als In-Game-Overlay auf der 2D-Spiel-API, headless nach
# dungeon_selftest-Muster. ENDLICH echte Pixel-Regression:
#   f0: leeres Spielfeld (dunkelrot)
#   f1: HUD gezeichnet -> test_frame_diff(f0,f1) > 0,
#       test_frame_region auf Knopf-Fläche = Theme-Flächenfarbe
#   Klick-Flanke auf Knopf (uim_frame_maus press+release) -> Callback
#   f2: Slider nach Klick bei 75% -> diff(f1,f2) > 0
# Marker: === UIMOO-FRAME OK ===
# ============================================================

importiere ui_moo

setze g auf {}
g["klicks"] = 0
setze fehler auf 0

funktion pruefe(name, ok):
    wenn ok:
        zeige "  ok: " + name
        gib_zurück 0
    zeige "  FEHLER: " + name
    gib_zurück 1

funktion auf_weiter(w):
    g["klicks"] = g["klicks"] + 1
    gib_zurück wahr

funktion nix(w, wert):
    gib_zurück wahr

setze win auf fenster_erstelle("uimoo frame test", 400, 300)

setze hud auf uim_frame_wurzel(400, 300)
setze kn auf uim_hinzu(hud, uim_knopf("WEITER", 40, 40, 120, 32, auf_weiter))
setze schieber auf uim_hinzu(hud, uim_slider(40, 100, 200, 24, 0, 100, 50, nix))
uim_hinzu(hud, uim_label("PUNKTE: 42", 40, 150, 100, 12))

# f0: nur Spielfeld
zeichne_rechteck(win, 0, 0, 400, 300, "#301818")
setze f0 auf test_frame_grab(win)

# f1: Spielfeld + HUD-Overlay
zeichne_rechteck(win, 0, 0, 400, 300, "#301818")
uim_frame_zeichne(hud, win)
setze f1 auf test_frame_grab(win)
test_frame_save_png(f1, "beispiele/snapshots/ui_moo/frame_backend_hud.png")

setze d01 auf test_frame_diff(f0, f1)
setze fehler auf fehler + pruefe("HUD sichtbar (diff > 1000 px)", d01["geaenderte_pixel"] > 1000)

# Knopf-Fläche (Theme dunkel: flaeche = 58,62,74) — Region links im Knopf,
# vor Textbeginn (Text "WEITER" startet zentriert bei x=76) und abseits
# des 1px-Rands.
setze reg auf test_frame_region(f1, 44, 44, 24, 4)
setze fehler auf fehler + pruefe("Knopf-Flächenfarbe (region 58/62/74)", reg["rot"] == 58 und reg["gruen"] == 62 und reg["blau"] == 74)

# Klick-Flanke: Press-Tick + Release-Tick auf dem Knopf
uim_frame_maus(hud, 100, 56, wahr)
uim_frame_maus(hud, 100, 56, falsch)
setze fehler auf fehler + pruefe("Knopf-Callback über Maus-Flanke", g["klicks"] == 1)

# Slider: Press bei 75% (x = 40 + 150), Release
uim_frame_maus(hud, 190, 112, wahr)
uim_frame_maus(hud, 190, 112, falsch)
setze sw auf schieber["wert"]
setze fehler auf fehler + pruefe("Slider-Wert ~75 (" + text(sw) + ")", sw > 72 und sw < 78)

# f2: neu zeichnen -> Pixel-Diff belegt Slider-Bewegung + Druck-Zustände
zeichne_rechteck(win, 0, 0, 400, 300, "#301818")
uim_frame_zeichne(hud, win)
setze f2 auf test_frame_grab(win)
setze d12 auf test_frame_diff(f1, f2)
setze fehler auf fehler + pruefe("Zustandswechsel sichtbar (diff > 0)", d12["geaenderte_pixel"] > 0)

# Tastatur über Kern: Tab fokussiert, Return aktiviert Knopf
uim_frame_taste(hud, "Tab", wahr, 0)
uim_frame_taste(hud, "Return", wahr, 0)
setze fehler auf fehler + pruefe("Return aktiviert fokussierten Knopf", g["klicks"] == 2)

fenster_schliessen(win)

wenn fehler == 0:
    zeige "=== UIMOO-FRAME OK ==="
sonst:
    zeige "=== UIMOO-FRAME FEHLER: " + text(fehler) + " ==="
    beende(1)

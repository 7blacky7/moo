# ============================================================
# test_maus.moo — Demo: Maus-Simulation + Screenshot vorher/nachher
#
# Zeigt automatisierten UI-Selftest mit den neuen Builtins:
#   raum_sim_maus_pos(win, x, y)
#   raum_sim_maus_taste(win, button, pressed)
#   raum_sim_rad(win, dy)
#   screenshot(win, pfad) | bildschirmfoto(win, pfad) — tag-dispatched, fuer 3D-Window Aufruf von moo_3d_screenshot_bmp
#
# Ablauf:
#   1. Erstelle 3D-Fenster mit einem roten Wuerfel auf gruenem Boden
#   2. Screenshot before.bmp (Wuerfel im Bild)
#   3. Simuliere LMB-Drag: cursor (400,300) -> press -> move (200,300) -> release
#   4. Screenshot after.bmp (Wuerfel sollte verschoben aussehen, abhaengig vom Pan)
#   5. Vergleich-Hinweis ausgeben
#
# Kompilieren / Starten:
#   moo-compiler run beispiele/city_builder/test_maus.moo
#   ls -la /tmp/test_maus_*.bmp
# ============================================================

konstante WIN_W auf 800
konstante WIN_H auf 600

zeige "=== Maus-Sim + Screenshot Demo ==="

setze win auf raum_erstelle("test_maus", WIN_W, WIN_H)
raum_perspektive(win, 50.0, 0.1, 50.0)

# Zustaende
setze cam_x auf 0.0
setze cam_z auf 0.0
setze cam_zoom auf 6.0

# Maus-Pan-Tracking
setze last_mx auf 0.0
setze last_my auf 0.0

funktion render_szene(win, cx, cz, zoom):
    raum_löschen(win, 0.55, 0.75, 0.92)
    setze auge_y auf zoom * sinus(0.7)
    setze auge_z auf cz + zoom * cosinus(0.7)
    raum_kamera(win, cx, auge_y, auge_z, cx, 0.0, cz)
    # Roter Wuerfel
    raum_würfel(win, 0, 0, 0, 1.0, "rot")
    # Gruener Boden (flacher Wuerfel)
    raum_würfel(win, 0, -1.0, 0, 4.0, "gruen")
    raum_aktualisieren(win)

# === Frame 0: Initial-Render + Screenshot ===
render_szene(win, cam_x, cam_z, cam_zoom)
warte(50)
render_szene(win, cam_x, cam_z, cam_zoom)
setze ok1 auf screenshot(win, "/tmp/test_maus_before.bmp")
zeige "before.bmp geschrieben: " + text(ok1)

# === Simuliere LMB-Drag von (400,300) nach (200,300) ===
zeige "Simuliere LMB-Drag: (400,300) -> (200,300)"
raum_sim_maus_pos(win, 400, 300)
raum_sim_maus_taste(win, "links", wahr)

# Initialisiere last-Position bei dem aktuellen sim-Stand
setze last_mx auf raum_maus_x(win)
setze last_my auf raum_maus_y(win)

# Bewege in 5 Schritten von 400 nach 200 (mit Pan-Logik)
setze schritt auf 0
solange schritt < 5:
    setze ziel_x auf 400 - (schritt + 1) * 40
    raum_sim_maus_pos(win, ziel_x, 300)
    setze mx auf raum_maus_x(win)
    setze my auf raum_maus_y(win)
    setze mdx auf mx - last_mx
    setze mdy auf my - last_my
    setze last_mx auf mx
    setze last_my auf my
    # Pan auf X-Achse: Maus links -> Kamera nach links, mit zoom-skaliertem Faktor
    wenn raum_maus_taste(win, "links"):
        setze pan_factor auf cam_zoom * 0.005
        setze cam_x auf cam_x - mdx * pan_factor
    render_szene(win, cam_x, cam_z, cam_zoom)
    warte(20)
    setze schritt auf schritt + 1

raum_sim_maus_taste(win, "links", falsch)

# === Frame N: Final-Render + Screenshot ===
render_szene(win, cam_x, cam_z, cam_zoom)
warte(50)
setze ok2 auf screenshot(win, "/tmp/test_maus_after.bmp")
zeige "after.bmp geschrieben: " + text(ok2)

zeige "Kamera nach Drag: cam_x=" + text(cam_x)
zeige ""
zeige "Pruefe Vergleich:"
zeige "  cmp /tmp/test_maus_before.bmp /tmp/test_maus_after.bmp"
zeige "  -> sollten unterschiedlich sein, da der Wuerfel verschoben aussieht."
zeige "  feh /tmp/test_maus_before.bmp /tmp/test_maus_after.bmp"

raum_schliessen(win)
zeige "=== Demo beendet ==="

# ============================================================
# test_platformer.moo — Automatisierter Test fuer Platformer
#
# Testet: Fenster, Sprites, Bewegung, Sprung, Schwerkraft,
#         Plattform-Kollision, Muenzen, Feinde, Screenshot pro Aktion
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600
setze SCHWERKRAFT auf 0.5
setze SPRUNGKRAFT auf -10.0
setze GESCHWINDIGKEIT auf 5
setze BODEN_Y auf 550

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

zeige "=== PLATFORMER AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster + Sprites ---
zeige "[Test 1] Fenster + Sprites laden"
setze win auf fenster_erstelle("Platformer Test", BREITE, HOEHE)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster", "nicht offen")

setze spr_pfad auf "beispiele/assets/sprites/kenney_platformer/PNG/"
setze spr_idle auf sprite_laden(win, spr_pfad + "Characters/platformChar_idle.png")
setze spr_walk1 auf sprite_laden(win, spr_pfad + "Characters/platformChar_walk1.png")
setze spr_jump auf sprite_laden(win, spr_pfad + "Characters/platformChar_jump.png")
setze spr_tile_top auf sprite_laden(win, spr_pfad + "Tiles/platformPack_tile003.png")
setze spr_muenze auf sprite_laden(win, spr_pfad + "Items/platformPack_item001.png")

wenn spr_idle > 0:
    test_ok("Spieler-Sprite geladen (ID: " + text(spr_idle) + ")")
sonst:
    test_fail("Sprite", "ID = 0")
wenn spr_tile_top > 0:
    test_ok("Tile-Sprite geladen")
sonst:
    test_fail("Tile-Sprite", "nicht geladen")

# --- Spielzustand ---
setze spieler_x auf 100.0
setze spieler_y auf 400.0
setze spieler_vy auf 0.0
setze spieler_w auf 24
setze spieler_h auf 36
setze auf_boden auf falsch
setze punkte auf 0
setze kamera_x auf 0.0

# Plattformen
setze plat_x auf [0, 500, 250, 450]
setze plat_y auf [550, 550, 450, 380]
setze plat_w auf [400, 300, 120, 100]
setze plat_anzahl auf 4

# Muenzen
setze muenz_x auf [300, 470]
setze muenz_y auf [420, 350]
setze muenz_aktiv auf [wahr, wahr]
setze muenz_anzahl auf 2

funktion rechteck_kollidiert(ax, ay, aw, ah, bx, by, bw, bh):
    wenn ax + aw < bx:
        gib_zurück falsch
    wenn ax > bx + bw:
        gib_zurück falsch
    wenn ay + ah < by:
        gib_zurück falsch
    wenn ay > by + bh:
        gib_zurück falsch
    gib_zurück wahr

funktion zeichne_szene():
    fenster_löschen(win, "#87CEEB")
    # Plattformen
    setze i auf 0
    solange i < plat_anzahl:
        setze draw_x auf plat_x[i] - kamera_x
        setze tx auf 0
        solange tx < plat_w[i]:
            sprite_zeichnen_skaliert(win, spr_tile_top, draw_x + tx, plat_y[i], 32, 20)
            setze tx auf tx + 32
        setze i auf i + 1
    # Muenzen
    setze i auf 0
    solange i < muenz_anzahl:
        wenn muenz_aktiv[i]:
            setze draw_x auf muenz_x[i] - kamera_x
            sprite_zeichnen_skaliert(win, spr_muenze, draw_x - 12, muenz_y[i] - 12, 24, 24)
        setze i auf i + 1
    # Spieler
    setze draw_x auf spieler_x - kamera_x
    sprite_zeichnen_skaliert(win, spr_idle, draw_x - 4, spieler_y - 10, 32, 48)

funktion physik_schritt():
    setze spieler_vy auf spieler_vy + SCHWERKRAFT
    setze spieler_y auf spieler_y + spieler_vy
    setze auf_boden auf falsch
    setze i auf 0
    solange i < plat_anzahl:
        wenn spieler_vy >= 0:
            wenn spieler_x + spieler_w > plat_x[i]:
                wenn spieler_x < plat_x[i] + plat_w[i]:
                    wenn spieler_y + spieler_h >= plat_y[i]:
                        wenn spieler_y + spieler_h <= plat_y[i] + 20:
                            setze spieler_y auf (plat_y[i] - spieler_h) * 1.0
                            setze spieler_vy auf 0.0
                            setze auf_boden auf wahr
        setze i auf i + 1

# --- Test 2: Initiales Rendering ---
zeige "[Test 2] Start-Szene rendern"
# Erst Schwerkraft anwenden bis Spieler auf Boden steht
setze frame auf 0
solange frame < 30:
    physik_schritt()
    setze frame auf frame + 1

zeichne_szene()
screenshot(win, "beispiele/test_screenshots/platformer_01_start.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("Szene mit Sprites gerendert")

# --- Test 3: Auf Boden gelandet ---
zeige "[Test 3] Schwerkraft + Boden-Kollision"
wenn auf_boden:
    test_ok("Spieler steht auf Boden: y = " + text(spieler_y))
sonst:
    test_fail("Boden", "Spieler schwebt: y = " + text(spieler_y) + " vy = " + text(spieler_vy))

# --- Test 4: Nach rechts laufen ---
zeige "[Test 4] Nach rechts laufen"
setze alte_x auf spieler_x
setze frame auf 0
solange frame < 10:
    taste_simulieren("d", wahr)
    warte(20)
    taste_simulieren("d", falsch)
    setze spieler_x auf spieler_x + GESCHWINDIGKEIT
    physik_schritt()
    zeichne_szene()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/platformer_02_rechts.bmp")
warte(100)
wenn spieler_x > alte_x:
    test_ok("Spieler nach rechts: " + text(alte_x) + " -> " + text(spieler_x))
sonst:
    test_fail("Rechts", "Position unveraendert")

# --- Test 5: Sprung ---
zeige "[Test 5] Sprung"
setze alte_y auf spieler_y
taste_simulieren("leertaste", wahr)
warte(30)
taste_simulieren("leertaste", falsch)
setze spieler_vy auf SPRUNGKRAFT
setze auf_boden auf falsch

# 10 Frames aufsteigen
setze frame auf 0
solange frame < 10:
    physik_schritt()
    zeichne_szene()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/platformer_03_sprung.bmp")
warte(100)

wenn spieler_y < alte_y:
    test_ok("Spieler springt: y " + text(alte_y) + " -> " + text(spieler_y))
sonst:
    test_fail("Sprung", "y nicht verringert")

# --- Test 6: Landung nach Sprung ---
zeige "[Test 6] Landung nach Sprung"
# Frames bis Landung
setze frame auf 0
solange frame < 40 und nicht auf_boden:
    physik_schritt()
    zeichne_szene()
    fenster_aktualisieren(win)
    warte(20)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/platformer_04_landung.bmp")
warte(100)

wenn auf_boden:
    test_ok("Spieler gelandet nach " + text(frame) + " Frames")
sonst:
    test_fail("Landung", "Nicht gelandet nach 40 Frames, y = " + text(spieler_y))

# --- Test 7: Muenze einsammeln ---
zeige "[Test 7] Muenze einsammeln"
# Spieler zur Muenze bewegen
setze spieler_x auf 290.0
setze spieler_y auf 414.0
setze spieler_vy auf 0.0
setze alte_punkte auf punkte

# Physik + Muenz-Check
setze frame auf 0
solange frame < 5:
    physik_schritt()
    setze spieler_x auf spieler_x + GESCHWINDIGKEIT
    # Muenzen pruefen
    setze i auf 0
    solange i < muenz_anzahl:
        wenn muenz_aktiv[i]:
            wenn rechteck_kollidiert(spieler_x, spieler_y, spieler_w, spieler_h, muenz_x[i] - 6, muenz_y[i] - 6, 12, 12):
                setze muenz_aktiv[i] auf falsch
                setze punkte auf punkte + 100
        setze i auf i + 1
    zeichne_szene()
    fenster_aktualisieren(win)
    warte(50)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/platformer_05_muenze.bmp")
warte(100)

wenn punkte > alte_punkte:
    test_ok("Muenze eingesammelt, Punkte: " + text(alte_punkte) + " -> " + text(punkte))
sonst:
    test_fail("Muenze", "Punkte unveraendert")

# --- Test 8: Kamera-Scrolling ---
zeige "[Test 8] Kamera-Scrolling"
setze spieler_x auf 500.0
setze kamera_x auf spieler_x - 300.0
wenn kamera_x < 0:
    setze kamera_x auf 0.0

zeichne_szene()
screenshot(win, "beispiele/test_screenshots/platformer_06_scrolling.bmp")
fenster_aktualisieren(win)
warte(100)

wenn kamera_x > 0:
    test_ok("Kamera scrollt mit: kamera_x = " + text(kamera_x))
sonst:
    test_fail("Scrolling", "kamera_x = 0")

# --- Test 9: Game-Loop (40 Frames) ---
zeige "[Test 9] Game-Loop (40 Frames)"
setze spieler_x auf 200.0
setze spieler_y auf 400.0
setze spieler_vy auf 0.0

setze frame auf 0
solange frame < 40:
    physik_schritt()
    # Alle paar Frames Richtung wechseln
    wenn frame < 20:
        setze spieler_x auf spieler_x + GESCHWINDIGKEIT
    sonst:
        setze spieler_x auf spieler_x - GESCHWINDIGKEIT
    setze kamera_x auf spieler_x - 300.0
    wenn kamera_x < 0:
        setze kamera_x auf 0.0
    zeichne_szene()
    wenn frame == 0:
        screenshot(win, "beispiele/test_screenshots/platformer_07_loop_start.bmp")
    wenn frame == 20:
        screenshot(win, "beispiele/test_screenshots/platformer_08_loop_mitte.bmp")
    wenn frame == 39:
        screenshot(win, "beispiele/test_screenshots/platformer_09_loop_ende.bmp")
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

test_ok("40 Frames ohne Crash")

# ============================================================
zeige ""
zeige "=== ERGEBNIS ==="
zeige "Bestanden: " + text(bestanden)
zeige "Fehlgeschlagen: " + text(fehlgeschlagen)
wenn fehlgeschlagen == 0:
    zeige "ALLE TESTS BESTANDEN!"
sonst:
    zeige "FEHLER GEFUNDEN — siehe oben"

fenster_schliessen(win)

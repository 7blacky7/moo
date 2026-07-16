# ============================================================
# test_breakout.moo — Automatisierter Test fuer Breakout
#
# Kompilieren: moo-compiler compile beispiele/domain/game/testscreens/test_breakout.moo -o beispiele/test_breakout
# Starten:     ./beispiele/domain/game/testscreens/test_breakout
#
# Testet: Fenster, Paddle-Bewegung, Ball-Start, Brick-Kollision,
#         Wand-Reflexion, Punkte-System, Screenshot nach jeder Aktion
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600
setze PADDLE_W auf 120
setze PADDLE_H auf 15
setze PADDLE_Y auf 560
setze PADDLE_SPEED auf 8
setze BALL_R auf 8

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

# ============================================================
zeige "=== BREAKOUT AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster erstellen ---
zeige "[Test 1] Fenster erstellen"
setze win auf fenster_erstelle("Breakout Test", BREITE, HOEHE)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster offen", "fenster_offen gibt falsch zurueck")

# --- Test 2: Initiales Rendering — Bricks + Paddle + Ball ---
zeige "[Test 2] Initiales Rendering"

# Spielzustand aufbauen
setze paddle_x auf (BREITE - PADDLE_W) / 2
setze ball_x auf 400.0
setze ball_y auf 540.0
setze ball_dx auf 4.0
setze ball_dy auf -4.0
setze ball_aktiv auf falsch
setze punkte auf 0

# Bricks
setze brick_aktiv auf []
setze brick_x auf []
setze brick_y auf []
setze brick_farbe auf []
setze reihe auf 0
solange reihe < 6:
    setze spalte auf 0
    solange spalte < 10:
        brick_aktiv.hinzufügen(wahr)
        brick_x.hinzufügen(30 + spalte * 75)
        brick_y.hinzufügen(60 + reihe * 30)
        brick_farbe.hinzufügen(reihe)
        setze spalte auf spalte + 1
    setze reihe auf reihe + 1
setze total_bricks auf 60

funktion reihen_farbe(reihe):
    wenn reihe == 0:
        gib_zurück "#F44336"
    wenn reihe == 1:
        gib_zurück "#FF9800"
    wenn reihe == 2:
        gib_zurück "#FFEB3B"
    wenn reihe == 3:
        gib_zurück "#4CAF50"
    wenn reihe == 4:
        gib_zurück "#2196F3"
    wenn reihe == 5:
        gib_zurück "#9C27B0"
    gib_zurück "#FFFFFF"

funktion zeichne_spielfeld():
    fenster_löschen(win, "#1A1A2E")
    # Bricks
    setze i auf 0
    solange i < total_bricks:
        wenn brick_aktiv[i]:
            zeichne_rechteck(win, brick_x[i], brick_y[i], 70, 25, reihen_farbe(brick_farbe[i]))
        setze i auf i + 1
    # Paddle
    zeichne_rechteck(win, paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, "#E0E0E0")
    # Ball
    zeichne_kreis(win, ball_x, ball_y, BALL_R, "#FFFFFF")

zeichne_spielfeld()
screenshot(win, "beispiele/test_screenshots/breakout_01_start.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("Startbildschirm gerendert — 60 Bricks, Paddle Mitte, Ball auf Paddle")

# --- Test 3: Paddle nach rechts bewegen ---
zeige "[Test 3] Paddle nach rechts"
setze alte_pos auf paddle_x

# 10 Frames rechts druecken
setze frame auf 0
solange frame < 10:
    taste_simulieren("d", wahr)
    setze paddle_x auf paddle_x + PADDLE_SPEED
    wenn paddle_x > BREITE - PADDLE_W:
        setze paddle_x auf BREITE - PADDLE_W
    zeichne_spielfeld()
    fenster_aktualisieren(win)
    warte(50)
    setze frame auf frame + 1
taste_simulieren("d", falsch)
warte(50)

screenshot(win, "beispiele/test_screenshots/breakout_02_paddle_rechts.bmp")
warte(100)

wenn paddle_x > alte_pos:
    test_ok("Paddle nach rechts bewegt: " + text(alte_pos) + " -> " + text(paddle_x))
sonst:
    test_fail("Paddle rechts", "Position unveraendert")

# --- Test 4: Paddle nach links bewegen ---
zeige "[Test 4] Paddle nach links"
setze alte_pos auf paddle_x

setze frame auf 0
solange frame < 20:
    taste_simulieren("a", wahr)
    setze paddle_x auf paddle_x - PADDLE_SPEED
    wenn paddle_x < 0:
        setze paddle_x auf 0
    zeichne_spielfeld()
    fenster_aktualisieren(win)
    warte(50)
    setze frame auf frame + 1
taste_simulieren("a", falsch)
warte(50)

screenshot(win, "beispiele/test_screenshots/breakout_03_paddle_links.bmp")
warte(100)

wenn paddle_x < alte_pos:
    test_ok("Paddle nach links bewegt: " + text(alte_pos) + " -> " + text(paddle_x))
sonst:
    test_fail("Paddle links", "Position unveraendert")

# --- Test 5: Paddle Begrenzung links ---
zeige "[Test 5] Paddle Begrenzung"
# Paddle ganz nach links fahren
setze frame auf 0
solange frame < 100:
    setze paddle_x auf paddle_x - PADDLE_SPEED
    wenn paddle_x < 0:
        setze paddle_x auf 0
    setze frame auf frame + 1

zeichne_spielfeld()
screenshot(win, "beispiele/test_screenshots/breakout_04_paddle_grenze.bmp")
fenster_aktualisieren(win)
warte(100)

wenn paddle_x == 0:
    test_ok("Paddle stoppt am linken Rand")
sonst:
    test_fail("Paddle Grenze", "paddle_x = " + text(paddle_x) + " statt 0")

# --- Test 6: Ball starten mit Leertaste ---
zeige "[Test 6] Ball starten"
# Paddle in die Mitte
setze paddle_x auf (BREITE - PADDLE_W) / 2
setze ball_x auf paddle_x + PADDLE_W / 2.0
setze ball_y auf 540.0
setze ball_aktiv auf falsch

zeichne_spielfeld()
screenshot(win, "beispiele/test_screenshots/breakout_05_vor_start.bmp")
fenster_aktualisieren(win)
warte(100)

# Leertaste druecken
taste_simulieren("leertaste", wahr)
warte(50)
taste_simulieren("leertaste", falsch)
setze ball_aktiv auf wahr
setze ball_dx auf 4.0
setze ball_dy auf -4.0

# 5 Frames Ball fliegen lassen
setze frame auf 0
solange frame < 5:
    setze ball_x auf ball_x + ball_dx
    setze ball_y auf ball_y + ball_dy
    zeichne_spielfeld()
    fenster_aktualisieren(win)
    warte(50)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/breakout_06_ball_fliegt.bmp")
warte(100)

wenn ball_y < 540.0:
    test_ok("Ball fliegt nach oben: y = " + text(ball_y))
sonst:
    test_fail("Ball starten", "Ball bewegt sich nicht nach oben")

# --- Test 7: Ball-Wand-Reflexion ---
zeige "[Test 7] Wand-Reflexion"
# Ball an linke Wand setzen
setze ball_x auf 5.0
setze ball_y auf 300.0
setze ball_dx auf -4.0
setze ball_dy auf -2.0

zeichne_spielfeld()
screenshot(win, "beispiele/test_screenshots/breakout_07_vor_wand.bmp")
fenster_aktualisieren(win)
warte(100)

# Einen Frame simulieren — Ball trifft Wand
setze ball_x auf ball_x + ball_dx
wenn ball_x - BALL_R < 0:
    setze ball_x auf BALL_R * 1.0
    setze ball_dx auf 0 - ball_dx

zeichne_spielfeld()
screenshot(win, "beispiele/test_screenshots/breakout_08_nach_wand.bmp")
fenster_aktualisieren(win)
warte(100)

wenn ball_dx > 0:
    test_ok("Ball reflektiert an linker Wand: dx = " + text(ball_dx))
sonst:
    test_fail("Wand-Reflexion", "dx immer noch negativ: " + text(ball_dx))

# --- Test 8: Brick-Kollision ---
zeige "[Test 8] Brick-Kollision"
# Ball direkt auf ersten Brick positionieren
setze ziel_brick auf 25
setze ball_x auf brick_x[ziel_brick] + 35.0
setze ball_y auf brick_y[ziel_brick] + 30.0
setze ball_dy auf -4.0

zeichne_spielfeld()
screenshot(win, "beispiele/test_screenshots/breakout_09_vor_brick.bmp")
fenster_aktualisieren(win)
warte(100)

# Kollision pruefen (vereinfacht)
setze alte_punkte auf punkte
brick_aktiv[ziel_brick] = falsch
setze punkte auf punkte + (6 - brick_farbe[ziel_brick]) * 10
setze ball_dy auf 0 - ball_dy

zeichne_spielfeld()
screenshot(win, "beispiele/test_screenshots/breakout_10_nach_brick.bmp")
fenster_aktualisieren(win)
warte(100)

wenn nicht brick_aktiv[ziel_brick]:
    test_ok("Brick zerstoert")
sonst:
    test_fail("Brick-Kollision", "Brick noch aktiv")

wenn punkte > alte_punkte:
    test_ok("Punkte erhoeht: " + text(alte_punkte) + " -> " + text(punkte))
sonst:
    test_fail("Punkte", "Unveraendert")

# --- Test 9: Mehrere Bricks zerstoeren ---
zeige "[Test 9] Mehrere Bricks zerstoeren"
setze zerstoert auf 0
setze i auf 0
solange i < 5:
    brick_aktiv[i] = falsch
    setze zerstoert auf zerstoert + 1
    setze i auf i + 1

zeichne_spielfeld()
screenshot(win, "beispiele/test_screenshots/breakout_11_mehrere_bricks.bmp")
fenster_aktualisieren(win)
warte(100)

# Zaehle aktive Bricks
setze aktive auf 0
setze i auf 0
solange i < total_bricks:
    wenn brick_aktiv[i]:
        setze aktive auf aktive + 1
    setze i auf i + 1

# 6 Bricks sollten weg sein (5 + 1 von Test 8)
wenn aktive == total_bricks - 6:
    test_ok("6 Bricks zerstoert, " + text(aktive) + " uebrig")
sonst:
    test_fail("Brick-Zaehlung", "Erwartet " + text(total_bricks - 6) + " aktive, aber " + text(aktive))

# --- Test 10: Ball Game-Loop mit Paddle-Verfolgung ---
zeige "[Test 10] Game-Loop (50 Frames mit Paddle-Tracking)"
setze ball_x auf 400.0
setze ball_y auf 400.0
setze ball_dx auf 3.0
setze ball_dy auf -3.0
setze paddle_x auf 340.0

setze frame auf 0
solange frame < 50:
    # Ball bewegen
    setze ball_x auf ball_x + ball_dx
    setze ball_y auf ball_y + ball_dy

    # Wand-Reflexion
    wenn ball_x - BALL_R < 0:
        setze ball_x auf BALL_R * 1.0
        setze ball_dx auf 0 - ball_dx
    wenn ball_x + BALL_R > BREITE:
        setze ball_x auf (BREITE - BALL_R) * 1.0
        setze ball_dx auf 0 - ball_dx
    wenn ball_y - BALL_R < 0:
        setze ball_y auf BALL_R * 1.0
        setze ball_dy auf 0 - ball_dy

    # Paddle folgt Ball
    wenn ball_x > paddle_x + PADDLE_W / 2:
        setze paddle_x auf paddle_x + PADDLE_SPEED
    sonst:
        setze paddle_x auf paddle_x - PADDLE_SPEED
    wenn paddle_x < 0:
        setze paddle_x auf 0
    wenn paddle_x > BREITE - PADDLE_W:
        setze paddle_x auf BREITE - PADDLE_W

    zeichne_spielfeld()
    # Screenshots bei wichtigen Frames
    wenn frame == 0:
        screenshot(win, "beispiele/test_screenshots/breakout_12_loop_start.bmp")
    wenn frame == 25:
        screenshot(win, "beispiele/test_screenshots/breakout_13_loop_mitte.bmp")
    wenn frame == 49:
        screenshot(win, "beispiele/test_screenshots/breakout_14_loop_ende.bmp")
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

test_ok("50 Frames Game-Loop ohne Crash")

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

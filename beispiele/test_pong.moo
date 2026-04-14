# ============================================================
# test_pong.moo — Automatisierter Test fuer Pong
#
# Testet: Fenster, Paddle-Bewegung, Ball-Physik, Wand-Reflexion,
#         Paddle-Kollision, Screenshot pro Aktion
# ============================================================

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

zeige "=== PONG AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster ---
zeige "[Test 1] Fenster"
setze win auf fenster_erstelle("Pong Test", 800, 600)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster", "nicht offen")

# Spielzustand
setze paddle_x auf 50
setze paddle_y auf 250
setze paddle_breite auf 15
setze paddle_hoehe auf 100
setze paddle_speed auf 8
setze ball_x auf 400.0
setze ball_y auf 300.0
setze ball_dx auf 5.0
setze ball_dy auf 3.0
setze ball_radius auf 10
setze punkte auf 0

funktion zeichne_pong():
    fenster_löschen(win, "schwarz")
    setze linie_y auf 0
    solange linie_y < 600:
        zeichne_rechteck(win, 398, linie_y, 4, 20, "grau")
        setze linie_y auf linie_y + 40
    zeichne_rechteck(win, paddle_x, paddle_y, paddle_breite, paddle_hoehe, "weiss")
    zeichne_kreis(win, ball_x, ball_y, ball_radius, "weiss")

# --- Test 2: Start-Rendering ---
zeige "[Test 2] Start-Rendering"
zeichne_pong()
screenshot(win, "beispiele/test_screenshots/pong_01_start.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("Pong-Feld gerendert")

# --- Test 3: Paddle nach oben ---
zeige "[Test 3] Paddle nach oben"
setze alte_y auf paddle_y
setze frame auf 0
solange frame < 10:
    taste_simulieren("hoch", wahr)
    warte(20)
    taste_simulieren("hoch", falsch)
    setze paddle_y auf paddle_y - paddle_speed
    wenn paddle_y < 0:
        setze paddle_y auf 0
    zeichne_pong()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/pong_02_paddle_oben.bmp")
warte(100)
wenn paddle_y < alte_y:
    test_ok("Paddle nach oben: " + text(alte_y) + " -> " + text(paddle_y))
sonst:
    test_fail("Paddle oben", "unveraendert")

# --- Test 4: Paddle nach unten ---
zeige "[Test 4] Paddle nach unten"
setze alte_y auf paddle_y
setze frame auf 0
solange frame < 20:
    taste_simulieren("runter", wahr)
    warte(20)
    taste_simulieren("runter", falsch)
    setze paddle_y auf paddle_y + paddle_speed
    wenn paddle_y + paddle_hoehe > 600:
        setze paddle_y auf 600 - paddle_hoehe
    zeichne_pong()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/pong_03_paddle_unten.bmp")
warte(100)
wenn paddle_y > alte_y:
    test_ok("Paddle nach unten: " + text(alte_y) + " -> " + text(paddle_y))
sonst:
    test_fail("Paddle unten", "unveraendert")

# --- Test 5: Ball-Bewegung ---
zeige "[Test 5] Ball-Bewegung"
setze ball_x auf 400.0
setze ball_y auf 300.0
setze ball_dx auf 5.0
setze ball_dy auf 3.0
setze alte_x auf ball_x

setze frame auf 0
solange frame < 10:
    setze ball_x auf ball_x + ball_dx
    setze ball_y auf ball_y + ball_dy
    zeichne_pong()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/pong_04_ball_bewegt.bmp")
warte(100)
wenn ball_x > alte_x:
    test_ok("Ball bewegt sich: x " + text(alte_x) + " -> " + text(ball_x))
sonst:
    test_fail("Ball", "x unveraendert")

# --- Test 6: Wand-Reflexion oben ---
zeige "[Test 6] Wand-Reflexion oben"
setze ball_x auf 500.0
setze ball_y auf 5.0
setze ball_dx auf 5.0
setze ball_dy auf -3.0

zeichne_pong()
screenshot(win, "beispiele/test_screenshots/pong_05_vor_wand.bmp")
fenster_aktualisieren(win)
warte(100)

setze ball_y auf ball_y + ball_dy
wenn ball_y - ball_radius < 0:
    setze ball_dy auf 0 - ball_dy
    setze ball_y auf ball_radius * 1.0

zeichne_pong()
screenshot(win, "beispiele/test_screenshots/pong_06_nach_wand.bmp")
fenster_aktualisieren(win)
warte(100)

wenn ball_dy > 0:
    test_ok("Ball reflektiert an oberer Wand: dy = " + text(ball_dy))
sonst:
    test_fail("Wand-Reflexion", "dy immer noch negativ")

# --- Test 7: Wand-Reflexion rechts ---
zeige "[Test 7] Wand-Reflexion rechts"
setze ball_x auf 795.0
setze ball_y auf 300.0
setze ball_dx auf 5.0

setze ball_x auf ball_x + ball_dx
wenn ball_x + ball_radius > 800:
    setze ball_dx auf 0 - ball_dx
    setze ball_x auf (800 - ball_radius) * 1.0

zeichne_pong()
screenshot(win, "beispiele/test_screenshots/pong_07_wand_rechts.bmp")
fenster_aktualisieren(win)
warte(100)

wenn ball_dx < 0:
    test_ok("Ball reflektiert an rechter Wand")
sonst:
    test_fail("Rechte Wand", "dx immer noch positiv")

# --- Test 8: Paddle-Kollision ---
zeige "[Test 8] Paddle-Kollision"
setze paddle_y auf 280
setze ball_x auf 70.0
setze ball_y auf 300.0
setze ball_dx auf -5.0
setze ball_dy auf 2.0
setze alte_punkte auf punkte

zeichne_pong()
screenshot(win, "beispiele/test_screenshots/pong_08_vor_paddle.bmp")
fenster_aktualisieren(win)
warte(100)

# Ball bewegen bis Paddle
setze frame auf 0
solange frame < 5:
    setze ball_x auf ball_x + ball_dx
    setze ball_y auf ball_y + ball_dy
    # Paddle-Kollision
    wenn ball_x - ball_radius < paddle_x + paddle_breite:
        wenn ball_y > paddle_y:
            wenn ball_y < paddle_y + paddle_hoehe:
                wenn ball_dx < 0:
                    setze ball_dx auf 0 - ball_dx
                    setze ball_x auf (paddle_x + paddle_breite + ball_radius) * 1.0
                    setze punkte auf punkte + 1
    zeichne_pong()
    fenster_aktualisieren(win)
    warte(50)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/pong_09_nach_paddle.bmp")
warte(100)

wenn punkte > alte_punkte:
    test_ok("Paddle-Kollision, Punkte: " + text(alte_punkte) + " -> " + text(punkte))
sonst:
    test_fail("Paddle-Kollision", "Kein Treffer")

# --- Test 9: Game-Loop (50 Frames) ---
zeige "[Test 9] Game-Loop (50 Frames)"
setze ball_x auf 400.0
setze ball_y auf 300.0
setze ball_dx auf 5.0
setze ball_dy auf 3.0
setze paddle_y auf 250

setze frame auf 0
solange frame < 50:
    setze ball_x auf ball_x + ball_dx
    setze ball_y auf ball_y + ball_dy
    # Waende
    wenn ball_y - ball_radius < 0:
        setze ball_dy auf 0 - ball_dy
        setze ball_y auf ball_radius * 1.0
    wenn ball_y + ball_radius > 600:
        setze ball_dy auf 0 - ball_dy
        setze ball_y auf (600 - ball_radius) * 1.0
    wenn ball_x + ball_radius > 800:
        setze ball_dx auf 0 - ball_dx
        setze ball_x auf (800 - ball_radius) * 1.0
    wenn ball_x - ball_radius < 0:
        setze ball_dx auf 0 - ball_dx
        setze ball_x auf ball_radius * 1.0
    # Paddle folgt Ball
    wenn ball_y > paddle_y + paddle_hoehe / 2:
        setze paddle_y auf paddle_y + paddle_speed
    sonst:
        setze paddle_y auf paddle_y - paddle_speed

    zeichne_pong()
    wenn frame == 25:
        screenshot(win, "beispiele/test_screenshots/pong_10_loop_mitte.bmp")
    wenn frame == 49:
        screenshot(win, "beispiele/test_screenshots/pong_11_loop_ende.bmp")
    fenster_aktualisieren(win)
    warte(20)
    setze frame auf frame + 1

test_ok("50 Frames ohne Crash")

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

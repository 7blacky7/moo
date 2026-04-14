# ============================================================
# moo Pinball
#
# Kompilieren: moo-compiler compile beispiele/pinball.moo -o beispiele/pinball
# Starten:     ./beispiele/pinball
#
# Links/A = Linker Flipper, Rechts/D = Rechter Flipper
# Leertaste = Ball starten
# Escape = Beenden
# ============================================================

setze BREITE auf 400
setze HOEHE auf 700
setze SCHWERKRAFT auf 0.15
setze MAX_BUMPER auf 8

# Ball
setze ball_x auf 370.0
setze ball_y auf 600.0
setze ball_vx auf 0.0
setze ball_vy auf 0.0
setze ball_r auf 6
setze ball_aktiv auf falsch

# Flipper
setze flip_l_y auf 620
setze flip_r_y auf 620
setze flip_l_auf auf falsch
setze flip_r_auf auf falsch

# Bumper (Kreise die Ball abprallen lassen)
setze bump_x auf []
setze bump_y auf []
setze bump_r auf []
setze bump_flash auf []

funktion bumper(bx, by, br):
    bump_x.hinzufügen(bx)
    bump_y.hinzufügen(by)
    bump_r.hinzufügen(br)
    bump_flash.hinzufügen(0)

bumper(130, 200, 25)
bumper(270, 200, 25)
bumper(200, 280, 20)
bumper(100, 350, 18)
bumper(300, 350, 18)
bumper(200, 150, 22)
bumper(150, 430, 15)
bumper(250, 430, 15)

setze punkte auf 0
setze leben auf 3
setze multiplier auf 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Pinball", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Flipper-Input
    setze flip_l_auf auf taste_gedrückt("links") oder taste_gedrückt("a")
    setze flip_r_auf auf taste_gedrückt("rechts") oder taste_gedrückt("d")

    # Ball starten
    wenn taste_gedrückt("leertaste") und nicht ball_aktiv:
        setze ball_aktiv auf wahr
        setze ball_x auf 370.0
        setze ball_y auf 500.0
        setze ball_vx auf -2.0 - (punkte % 3) * 0.5
        setze ball_vy auf -8.0
        setze multiplier auf 1

    wenn ball_aktiv:
        # Physik
        setze ball_vy auf ball_vy + SCHWERKRAFT
        setze ball_x auf ball_x + ball_vx
        setze ball_y auf ball_y + ball_vy

        # Waende
        wenn ball_x < ball_r + 20:
            setze ball_x auf (ball_r + 20) * 1.0
            setze ball_vx auf abs(ball_vx) * 0.8
        wenn ball_x > BREITE - ball_r - 20:
            setze ball_x auf (BREITE - ball_r - 20) * 1.0
            setze ball_vx auf 0 - abs(ball_vx) * 0.8

        # Decke
        wenn ball_y < ball_r + 30:
            setze ball_y auf (ball_r + 30) * 1.0
            setze ball_vy auf abs(ball_vy) * 0.8

        # Flipper-Kollision
        # Linker Flipper (x: 80-180, y: flip_l_y)
        wenn ball_y + ball_r > flip_l_y - 5 und ball_y < flip_l_y + 10:
            wenn ball_x > 80 und ball_x < 190:
                wenn ball_vy > 0:
                    wenn flip_l_auf:
                        setze ball_vy auf -10.0
                        setze ball_vx auf (ball_x - 135) * 0.08
                    sonst:
                        setze ball_vy auf -3.0

        # Rechter Flipper (x: 210-320, y: flip_r_y)
        wenn ball_y + ball_r > flip_r_y - 5 und ball_y < flip_r_y + 10:
            wenn ball_x > 210 und ball_x < 320:
                wenn ball_vy > 0:
                    wenn flip_r_auf:
                        setze ball_vy auf -10.0
                        setze ball_vx auf (ball_x - 265) * 0.08
                    sonst:
                        setze ball_vy auf -3.0

        # Bumper-Kollision
        setze bi auf 0
        solange bi < MAX_BUMPER:
            setze ddx auf ball_x - bump_x[bi]
            setze ddy auf ball_y - bump_y[bi]
            setze dist auf ddx * ddx + ddy * ddy
            setze br auf bump_r[bi] + ball_r
            wenn dist < br * br und dist > 1:
                # Abprallen
                setze nd auf sqrt(dist)
                setze nx auf ddx / nd
                setze ny auf ddy / nd
                setze ball_vx auf nx * 6.0
                setze ball_vy auf ny * 6.0
                setze ball_x auf bump_x[bi] + nx * (br + 1)
                setze ball_y auf bump_y[bi] + ny * (br + 1)
                setze punkte auf punkte + 10 * multiplier
                setze multiplier auf multiplier + 1
                setze bump_flash[bi] auf 8
            setze bi auf bi + 1

        # Ball verloren
        wenn ball_y > HOEHE + 20:
            setze ball_aktiv auf falsch
            setze leben auf leben - 1
            wenn leben <= 0:
                stopp

    # Flash-Timer
    setze bi auf 0
    solange bi < MAX_BUMPER:
        wenn bump_flash[bi] > 0:
            setze bump_flash[bi] auf bump_flash[bi] - 1
        setze bi auf bi + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#1A237E")

    # Spielfeld-Rahmen
    zeichne_rechteck(win, 15, 25, 5, HOEHE - 25, "#3F51B5")
    zeichne_rechteck(win, BREITE - 20, 25, 5, HOEHE - 25, "#3F51B5")
    zeichne_rechteck(win, 15, 25, BREITE - 30, 5, "#3F51B5")

    # Bumper
    setze bi auf 0
    solange bi < MAX_BUMPER:
        wenn bump_flash[bi] > 0:
            zeichne_kreis(win, bump_x[bi], bump_y[bi], bump_r[bi] + 3, "#FFEB3B")
        zeichne_kreis(win, bump_x[bi], bump_y[bi], bump_r[bi], "#E91E63")
        zeichne_kreis(win, bump_x[bi], bump_y[bi], bump_r[bi] - 4, "#F48FB1")
        setze bi auf bi + 1

    # Slingshots (dreieckige Waende)
    zeichne_linie(win, 50, 500, 100, 580, "#FF9800")
    zeichne_linie(win, 350, 500, 300, 580, "#FF9800")

    # Flipper
    wenn flip_l_auf:
        zeichne_rechteck(win, 80, flip_l_y - 8, 110, 10, "#4CAF50")
    sonst:
        zeichne_rechteck(win, 80, flip_l_y, 110, 10, "#388E3C")

    wenn flip_r_auf:
        zeichne_rechteck(win, 210, flip_r_y - 8, 110, 10, "#4CAF50")
    sonst:
        zeichne_rechteck(win, 210, flip_r_y, 110, 10, "#388E3C")

    # Drain
    zeichne_rechteck(win, 170, HOEHE - 10, 60, 10, "#F44336")

    # Ball
    wenn ball_aktiv:
        zeichne_kreis(win, ball_x, ball_y, ball_r, "#E0E0E0")
        zeichne_kreis(win, ball_x - 2, ball_y - 2, 2, "#FFFFFF")

    # Launch-Lane
    zeichne_rechteck(win, BREITE - 35, 30, 15, HOEHE - 60, "#283593")

    # HUD
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, 30 + li * 20, HOEHE - 20, 6, "#F44336")
        setze li auf li + 1

    # Score
    setze si auf 0
    solange si < punkte / 50 und si < 25:
        zeichne_rechteck(win, 30 + si * 8, 10, 5, 10, "#FFD700")
        setze si auf si + 1

    # Multiplier
    setze mi auf 0
    solange mi < multiplier und mi < 10:
        zeichne_kreis(win, BREITE - 30, 40 + mi * 15, 4, "#FF9800")
        setze mi auf mi + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Game Over! Punkte: " + text(punkte)

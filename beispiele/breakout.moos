# ============================================================
# moo Breakout — Klassisches Brick-Breaker-Spiel
#
# Kompilieren: moo-compiler run beispiele/breakout.moo
#
# Bedienung:
#   Links/Rechts oder A/D - Paddle bewegen
#   Leertaste - Ball starten
#   Escape - Beenden
#
# Zeigt: 2D Game-Dev mit bestehender moo-API
#   * Rechtecke fuer Paddle, Bricks, Waende
#   * Kreis fuer Ball
#   * Kollisionserkennung (AABB + Kreis)
#   * Punkte-System
# ============================================================

# === KONSTANTEN ===
setze BREITE auf 800
setze HOEHE auf 600
setze PADDLE_W auf 120
setze PADDLE_H auf 15
setze PADDLE_Y auf 560
setze PADDLE_SPEED auf 8

setze BALL_R auf 8
setze BALL_START_X auf 400
setze BALL_START_Y auf 540

setze BRICK_W auf 70
setze BRICK_H auf 25
setze BRICK_ABSTAND auf 5
setze BRICK_REIHEN auf 6
setze BRICK_SPALTEN auf 10
setze BRICK_START_X auf 30
setze BRICK_START_Y auf 60

# === FARBEN ===
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

# === SPIELZUSTAND ===
setze paddle_x auf (BREITE - PADDLE_W) / 2
setze ball_x auf BALL_START_X * 1.0
setze ball_y auf BALL_START_Y * 1.0
setze ball_dx auf 4.0
setze ball_dy auf -4.0
setze ball_aktiv auf falsch
setze punkte auf 0
setze leben auf 3

# Bricks als Liste: [aktiv, x, y, farbe_index]
# Wir nutzen 3 Listen: brick_aktiv, brick_x, brick_y
setze brick_aktiv auf []
setze brick_x auf []
setze brick_y auf []
setze brick_farbe auf []

# Bricks initialisieren
setze reihe auf 0
solange reihe < BRICK_REIHEN:
    setze spalte auf 0
    solange spalte < BRICK_SPALTEN:
        brick_aktiv.hinzufügen(wahr)
        brick_x.hinzufügen(BRICK_START_X + spalte * (BRICK_W + BRICK_ABSTAND))
        brick_y.hinzufügen(BRICK_START_Y + reihe * (BRICK_H + BRICK_ABSTAND))
        brick_farbe.hinzufügen(reihe)
        setze spalte auf spalte + 1
    setze reihe auf reihe + 1

setze total_bricks auf BRICK_REIHEN * BRICK_SPALTEN

# === KOLLISIONSERKENNUNG ===
funktion ball_trifft_rechteck(bx, by, br, rx, ry, rw, rh):
    # Naechster Punkt auf dem Rechteck zum Ball-Mittelpunkt
    setze nx auf bx
    wenn nx < rx:
        setze nx auf rx
    wenn nx > rx + rw:
        setze nx auf rx + rw
    setze ny auf by
    wenn ny < ry:
        setze ny auf ry
    wenn ny > ry + rh:
        setze ny auf ry + rh
    setze dx auf bx - nx
    setze dy auf by - ny
    gib_zurück (dx * dx + dy * dy) < (br * br)

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Breakout", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # === INPUT ===
    wenn taste_gedrückt("links") oder taste_gedrückt("a"):
        setze paddle_x auf paddle_x - PADDLE_SPEED
    wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
        setze paddle_x auf paddle_x + PADDLE_SPEED

    # Paddle begrenzen
    wenn paddle_x < 0:
        setze paddle_x auf 0
    wenn paddle_x > BREITE - PADDLE_W:
        setze paddle_x auf BREITE - PADDLE_W

    # Ball starten
    wenn taste_gedrückt("leertaste") und nicht ball_aktiv:
        setze ball_aktiv auf wahr
        setze ball_dx auf 4.0
        setze ball_dy auf -4.0

    # Ball auf Paddle wenn nicht aktiv
    wenn nicht ball_aktiv:
        setze ball_x auf paddle_x + PADDLE_W / 2.0
        setze ball_y auf BALL_START_Y * 1.0

    # === BALL-PHYSIK ===
    wenn ball_aktiv:
        setze ball_x auf ball_x + ball_dx
        setze ball_y auf ball_y + ball_dy

        # Wand links/rechts
        wenn ball_x - BALL_R < 0:
            setze ball_x auf BALL_R * 1.0
            setze ball_dx auf 0 - ball_dx
        wenn ball_x + BALL_R > BREITE:
            setze ball_x auf (BREITE - BALL_R) * 1.0
            setze ball_dx auf 0 - ball_dx

        # Decke
        wenn ball_y - BALL_R < 0:
            setze ball_y auf BALL_R * 1.0
            setze ball_dy auf 0 - ball_dy

        # Boden = Leben verloren
        wenn ball_y + BALL_R > HOEHE:
            setze leben auf leben - 1
            setze ball_aktiv auf falsch
            setze ball_x auf BALL_START_X * 1.0
            setze ball_y auf BALL_START_Y * 1.0
            wenn leben <= 0:
                stopp

        # Paddle-Kollision
        wenn ball_trifft_rechteck(ball_x, ball_y, BALL_R, paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H):
            wenn ball_dy > 0:
                setze ball_dy auf 0 - ball_dy
                # Winkel basierend auf Treffpunkt
                setze treffer auf (ball_x - paddle_x) / PADDLE_W
                setze ball_dx auf (treffer - 0.5) * 8.0

        # Brick-Kollision
        setze i auf 0
        solange i < total_bricks:
            wenn brick_aktiv[i]:
                wenn ball_trifft_rechteck(ball_x, ball_y, BALL_R, brick_x[i], brick_y[i], BRICK_W, BRICK_H):
                    brick_aktiv[i] = falsch
                    setze ball_dy auf 0 - ball_dy
                    setze punkte auf punkte + (BRICK_REIHEN - brick_farbe[i]) * 10
            setze i auf i + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#1A1A2E")

    # Bricks
    setze i auf 0
    solange i < total_bricks:
        wenn brick_aktiv[i]:
            zeichne_rechteck(win, brick_x[i], brick_y[i], BRICK_W, BRICK_H, reihen_farbe(brick_farbe[i]))
        setze i auf i + 1

    # Paddle
    zeichne_rechteck(win, paddle_x, PADDLE_Y, PADDLE_W, PADDLE_H, "#E0E0E0")

    # Ball
    zeichne_kreis(win, ball_x, ball_y, BALL_R, "#FFFFFF")

    # HUD: Leben als Kreise
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, 20 + li * 25, 20, 8, "#F44336")
        setze li auf li + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn leben <= 0:
    zeige "GAME OVER! Punkte: " + text(punkte)
sonst:
    zeige "GEWONNEN! Punkte: " + text(punkte)

# ============================================================
# moo Pong — Klassiker (2 Spieler)
#
# Kompilieren: moo-compiler compile beispiele/pong.moo -o beispiele/pong
# Starten:     ./beispiele/pong
#
# Spieler 1: W/S
# Spieler 2: Hoch/Runter
# Escape = Beenden
# ============================================================

setze BREITE auf 800
setze HOEHE auf 500
setze PADDLE_H auf 80
setze PADDLE_W auf 12
setze PADDLE_SPEED auf 6
setze BALL_SIZE auf 8

# Paddles
setze p1_y auf 210.0
setze p2_y auf 210.0

# Ball
setze ball_x auf 400.0
setze ball_y auf 250.0
setze ball_dx auf 4.0
setze ball_dy auf 2.0

# Score
setze score1 auf 0
setze score2 auf 0
setze max_score auf 7

funktion ball_reset():
    setze ball_x auf 400.0
    setze ball_y auf 250.0
    setze ball_dy auf 2.0

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Pong", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn score1 >= max_score oder score2 >= max_score:
        stopp

    # === INPUT ===
    wenn taste_gedrückt("w"):
        setze p1_y auf p1_y - PADDLE_SPEED
    wenn taste_gedrückt("s"):
        setze p1_y auf p1_y + PADDLE_SPEED
    wenn taste_gedrückt("oben"):
        setze p2_y auf p2_y - PADDLE_SPEED
    wenn taste_gedrückt("unten"):
        setze p2_y auf p2_y + PADDLE_SPEED

    # Paddle-Grenzen
    wenn p1_y < 0:
        setze p1_y auf 0.0
    wenn p1_y > HOEHE - PADDLE_H:
        setze p1_y auf (HOEHE - PADDLE_H) * 1.0
    wenn p2_y < 0:
        setze p2_y auf 0.0
    wenn p2_y > HOEHE - PADDLE_H:
        setze p2_y auf (HOEHE - PADDLE_H) * 1.0

    # === BALL ===
    setze ball_x auf ball_x + ball_dx
    setze ball_y auf ball_y + ball_dy

    # Oben/Unten
    wenn ball_y < BALL_SIZE:
        setze ball_y auf BALL_SIZE * 1.0
        setze ball_dy auf 0 - ball_dy
    wenn ball_y > HOEHE - BALL_SIZE:
        setze ball_y auf (HOEHE - BALL_SIZE) * 1.0
        setze ball_dy auf 0 - ball_dy

    # Paddle 1 (links, x=20)
    wenn ball_dx < 0:
        wenn ball_x - BALL_SIZE < 20 + PADDLE_W:
            wenn ball_y > p1_y und ball_y < p1_y + PADDLE_H:
                setze ball_dx auf 0 - ball_dx
                # Winkel basierend auf Treffpunkt
                setze treffer auf (ball_y - p1_y) / PADDLE_H
                setze ball_dy auf (treffer - 0.5) * 8.0
                setze ball_x auf (20 + PADDLE_W + BALL_SIZE) * 1.0

    # Paddle 2 (rechts, x=BREITE-20-PADDLE_W)
    wenn ball_dx > 0:
        wenn ball_x + BALL_SIZE > BREITE - 20 - PADDLE_W:
            wenn ball_y > p2_y und ball_y < p2_y + PADDLE_H:
                setze ball_dx auf 0 - ball_dx
                setze treffer auf (ball_y - p2_y) / PADDLE_H
                setze ball_dy auf (treffer - 0.5) * 8.0
                setze ball_x auf (BREITE - 20 - PADDLE_W - BALL_SIZE) * 1.0

    # Punkte
    wenn ball_x < 0:
        setze score2 auf score2 + 1
        ball_reset()
        setze ball_dx auf 4.0
    wenn ball_x > BREITE:
        setze score1 auf score1 + 1
        ball_reset()
        setze ball_dx auf -4.0

    # === ZEICHNEN ===
    fenster_löschen(win, "#111111")

    # Mittellinie
    setze mi auf 0
    solange mi < HOEHE / 20:
        zeichne_rechteck(win, BREITE / 2 - 1, mi * 20, 2, 10, "#333333")
        setze mi auf mi + 1

    # Paddles
    zeichne_rechteck(win, 20, p1_y, PADDLE_W, PADDLE_H, "#4CAF50")
    zeichne_rechteck(win, BREITE - 20 - PADDLE_W, p2_y, PADDLE_W, PADDLE_H, "#2196F3")

    # Ball
    zeichne_kreis(win, ball_x, ball_y, BALL_SIZE, "#FFFFFF")

    # Score (als Kreise)
    setze si auf 0
    solange si < score1:
        zeichne_kreis(win, BREITE / 4 + si * 20, 30, 6, "#4CAF50")
        setze si auf si + 1
    setze si auf 0
    solange si < score2:
        zeichne_kreis(win, BREITE * 3 / 4 + si * 20, 30, 6, "#2196F3")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn score1 >= max_score:
    zeige "Spieler 1 gewinnt! " + text(score1) + ":" + text(score2)
sonst:
    zeige "Spieler 2 gewinnt! " + text(score1) + ":" + text(score2)

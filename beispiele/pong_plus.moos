# ============================================================
# pong_plus.moo — Pong gegen KI
#
# Kompilieren: moo-compiler compile beispiele/pong_plus.moo -o beispiele/pong_plus
# Starten:     ./beispiele/pong_plus
#
# Steuerung: W/S oder Pfeile = Paddle, Escape = Beenden
# Gegner: KI mit anpassbarer Staerke
# ============================================================

konstante WIN_W auf 640
konstante WIN_H auf 400
konstante PADDLE_H auf 70
konstante PADDLE_W auf 10
konstante BALL_R auf 8
konstante MAX_SCORE auf 5

# Paddles
setze p1_y auf 165.0
setze p2_y auf 165.0
setze p1_speed auf 5.0
setze p2_speed auf 4.5

# Ball
setze ball_x auf 320.0
setze ball_y auf 200.0
setze ball_vx auf -5.0
setze ball_vy auf 3.0

# Score
setze p1_score auf 0
setze p2_score auf 0
setze game_over auf falsch
setze winner auf 0

# PRNG
setze pp_seed auf 42

funktion pp_rand(max_val):
    setze pp_seed auf (pp_seed * 1103515245 + 12345) % 2147483648
    gib_zurück pp_seed % max_val

funktion reset_ball(richtung):
    setze ball_x auf WIN_W / 2 * 1.0
    setze ball_y auf WIN_H / 2 * 1.0
    setze ball_vx auf richtung * 5.0
    setze ball_vy auf (pp_rand(40) - 20) / 10.0

funktion neu_starten():
    setze p1_score auf 0
    setze p2_score auf 0
    setze game_over auf falsch
    setze winner auf 0
    reset_ball(-1)

# === Hauptprogramm ===
zeige "=== moo Pong Plus ==="
zeige "W/S = Links (du), KI rechts. Erste 5 Punkte gewinnt."

setze win auf fenster_erstelle("moo Pong Plus", WIN_W, WIN_H)
setze pp_seed auf zeit_ms() % 99991

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_gedrückt("r") und game_over:
        neu_starten()

    wenn game_over == falsch:
        # Spieler-Paddle
        wenn taste_gedrückt("w") oder taste_gedrückt("oben"):
            setze p1_y auf p1_y - p1_speed
        wenn taste_gedrückt("s") oder taste_gedrückt("unten"):
            setze p1_y auf p1_y + p1_speed
        wenn p1_y < 0: setze p1_y auf 0.0
        wenn p1_y > WIN_H - PADDLE_H: setze p1_y auf WIN_H - PADDLE_H * 1.0

        # KI-Paddle (folgt dem Ball mit Delay)
        setze ziel auf ball_y - PADDLE_H / 2
        wenn p2_y < ziel - 3:
            setze p2_y auf p2_y + p2_speed
        sonst wenn p2_y > ziel + 3:
            setze p2_y auf p2_y - p2_speed
        wenn p2_y < 0: setze p2_y auf 0.0
        wenn p2_y > WIN_H - PADDLE_H: setze p2_y auf WIN_H - PADDLE_H * 1.0

        # Ball bewegen
        setze ball_x auf ball_x + ball_vx
        setze ball_y auf ball_y + ball_vy

        # Oben/Unten
        wenn ball_y < BALL_R:
            setze ball_y auf BALL_R * 1.0
            setze ball_vy auf ball_vy * -1
        wenn ball_y > WIN_H - BALL_R:
            setze ball_y auf WIN_H - BALL_R * 1.0
            setze ball_vy auf ball_vy * -1

        # Paddle-Kollision P1 (links)
        wenn ball_x - BALL_R < 20 + PADDLE_W und ball_x > 20:
            wenn ball_y > p1_y und ball_y < p1_y + PADDLE_H:
                wenn ball_vx < 0:
                    setze ball_vx auf ball_vx * -1.05
                    # Winkel basiert auf Treffer-Punkt
                    setze off auf (ball_y - (p1_y + PADDLE_H / 2)) / (PADDLE_H / 2)
                    setze ball_vy auf off * 5

        # Paddle-Kollision P2 (rechts)
        wenn ball_x + BALL_R > WIN_W - 20 - PADDLE_W und ball_x < WIN_W - 20:
            wenn ball_y > p2_y und ball_y < p2_y + PADDLE_H:
                wenn ball_vx > 0:
                    setze ball_vx auf ball_vx * -1.05
                    setze off auf (ball_y - (p2_y + PADDLE_H / 2)) / (PADDLE_H / 2)
                    setze ball_vy auf off * 5

        # Punkt?
        wenn ball_x < 0:
            setze p2_score auf p2_score + 1
            wenn p2_score >= MAX_SCORE:
                setze game_over auf wahr
                setze winner auf 2
            sonst:
                reset_ball(1)
        wenn ball_x > WIN_W:
            setze p1_score auf p1_score + 1
            wenn p1_score >= MAX_SCORE:
                setze game_over auf wahr
                setze winner auf 1
            sonst:
                reset_ball(-1)

    # === Zeichnen ===
    fenster_löschen(win, "#000000")

    # Mittel-Linie
    setze my auf 0
    solange my < WIN_H:
        zeichne_rechteck(win, WIN_W / 2 - 2, my, 4, 10, "#424242")
        setze my auf my + 20

    # Score-Kreise
    setze s1 auf 0
    solange s1 < p1_score:
        zeichne_kreis(win, WIN_W / 4 + s1 * 20, 30, 8, "#2196F3")
        setze s1 auf s1 + 1
    setze s2 auf 0
    solange s2 < p2_score:
        zeichne_kreis(win, WIN_W * 3 / 4 - s2 * 20, 30, 8, "#F44336")
        setze s2 auf s2 + 1

    # Paddles
    zeichne_rechteck(win, 20, p1_y, PADDLE_W, PADDLE_H, "#2196F3")
    zeichne_rechteck(win, 22, p1_y + 2, PADDLE_W - 4, PADDLE_H - 4, "#42A5F5")
    zeichne_rechteck(win, WIN_W - 20 - PADDLE_W, p2_y, PADDLE_W, PADDLE_H, "#F44336")
    zeichne_rechteck(win, WIN_W - 18 - PADDLE_W, p2_y + 2, PADDLE_W - 4, PADDLE_H - 4, "#EF5350")

    # Ball
    zeichne_kreis(win, ball_x, ball_y, BALL_R, "#FFFFFF")
    zeichne_kreis(win, ball_x - 2, ball_y - 2, BALL_R - 3, "#FFEB3B")

    # Game Over
    wenn game_over:
        setze gc auf "#2196F3"
        wenn winner == 2: setze gc auf "#F44336"
        zeichne_rechteck(win, WIN_W / 2 - 80, WIN_H / 2 - 25, 160, 50, gc)
        zeichne_rechteck(win, WIN_W / 2 - 78, WIN_H / 2 - 23, 156, 46, "#263238")
        # Sieger-Symbol
        setze cx auf WIN_W / 2
        setze cy auf WIN_H / 2
        zeichne_kreis(win, cx, cy, 14, gc)

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Pong Plus beendet. " + text(p1_score) + ":" + text(p2_score)

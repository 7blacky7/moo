# Pong — Demo-Spiel in moo
# Steuerung: Pfeiltasten (oben/unten) fuer Paddle, Escape zum Beenden

# Fenster erstellen
setze fenster auf fenster_erstelle("Moo Pong", 800, 600)

# Spielzustand
setze paddle_x auf 50
setze paddle_y auf 250
setze paddle_breite auf 15
setze paddle_höhe auf 100
setze paddle_speed auf 8

setze ball_x auf 400.0
setze ball_y auf 300.0
setze ball_dx auf 5.0
setze ball_dy auf 3.0
setze ball_radius auf 10

setze punkte auf 0
setze leben auf 3

# Spielschleife
solange fenster_offen(fenster):
    # --- Input ---
    wenn taste_gedrückt("oben"):
        setze paddle_y auf paddle_y - paddle_speed
    wenn taste_gedrückt("unten"):
        setze paddle_y auf paddle_y + paddle_speed
    wenn taste_gedrückt("escape"):
        stopp

    # Paddle-Grenzen
    wenn paddle_y < 0:
        setze paddle_y auf 0
    wenn paddle_y + paddle_höhe > 600:
        setze paddle_y auf 600 - paddle_höhe

    # --- Ball bewegen ---
    setze ball_x auf ball_x + ball_dx
    setze ball_y auf ball_y + ball_dy

    # Wand oben/unten
    wenn ball_y - ball_radius < 0:
        setze ball_dy auf 0 - ball_dy
        setze ball_y auf ball_radius
    wenn ball_y + ball_radius > 600:
        setze ball_dy auf 0 - ball_dy
        setze ball_y auf 600 - ball_radius

    # Wand rechts (Abprallen)
    wenn ball_x + ball_radius > 800:
        setze ball_dx auf 0 - ball_dx
        setze ball_x auf 800 - ball_radius

    # Paddle-Kollision
    wenn ball_x - ball_radius < paddle_x + paddle_breite:
        wenn ball_y > paddle_y:
            wenn ball_y < paddle_y + paddle_höhe:
                wenn ball_dx < 0:
                    setze ball_dx auf 0 - ball_dx
                    setze ball_x auf paddle_x + paddle_breite + ball_radius
                    setze punkte auf punkte + 1

    # Ball links raus = Leben verloren
    wenn ball_x + ball_radius < 0:
        setze leben auf leben - 1
        setze ball_x auf 400.0
        setze ball_y auf 300.0
        setze ball_dx auf 5.0
        setze ball_dy auf 3.0

    # Game Over
    wenn leben < 1:
        stopp

    # --- Zeichnen ---
    fenster_löschen(fenster, "schwarz")

    # Mittellinie
    setze linie_y auf 0
    solange linie_y < 600:
        zeichne_rechteck(fenster, 398, linie_y, 4, 20, "grau")
        setze linie_y auf linie_y + 40

    # Paddle
    zeichne_rechteck(fenster, paddle_x, paddle_y, paddle_breite, paddle_höhe, "weiss")

    # Ball
    zeichne_kreis(fenster, ball_x, ball_y, ball_radius, "weiss")

    fenster_aktualisieren(fenster)
    warte(16)

# Aufräumen
fenster_schliessen(fenster)

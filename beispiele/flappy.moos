# ============================================================
# flappy.moo — Flappy Bird Klon
#
# Kompilieren: moo-compiler compile beispiele/flappy.moo -o beispiele/flappy
# Starten:     ./beispiele/flappy
#
# Steuerung: Leertaste = Fluegel schlagen, Escape = Beenden
# ============================================================

konstante WIN_W auf 400
konstante WIN_H auf 600
konstante SCHWERKRAFT auf 0.5
konstante FLAP auf -8.0
konstante ROHR_BREITE auf 60
konstante LUECKE auf 160
konstante ROHR_SPEED auf 3
konstante MAX_ROHRE auf 4

# === Farben ===
konstante C_HIMMEL auf "#87CEEB"
konstante C_VOGEL auf "#FFD700"
konstante C_VOGEL_AUGE auf "#FFFFFF"
konstante C_VOGEL_PUPILLE auf "#000000"
konstante C_VOGEL_SCHNABEL auf "#FF6B35"
konstante C_ROHR auf "#4CAF50"
konstante C_ROHR_RAND auf "#388E3C"
konstante C_BODEN auf "#8D6E63"
konstante C_BODEN_GRAS auf "#4CAF50"

# === Vogel ===
setze vogel_y auf 250.0
setze vogel_vy auf 0.0
konstante VOGEL_X auf 80
konstante VOGEL_R auf 16

# === Rohre ===
setze rohr_x auf []
setze rohr_y auf []
setze rohr_punkt auf []

# === State ===
setze score auf 0
setze highscore auf 0
setze game_over auf falsch
setze gestartet auf falsch
setze flap_cooldown auf 0

# === PRNG ===
setze rng_seed auf 12345

funktion zufall(min_val, max_val):
    setze rng_seed auf (rng_seed * 1103515245 + 12345) % 2147483648
    setze range auf max_val - min_val
    gib_zurück min_val + (rng_seed % range)

# === Rohre initialisieren ===
funktion init_rohre():
    setze rohr_x auf []
    setze rohr_y auf []
    setze rohr_punkt auf []
    setze ri auf 0
    solange ri < MAX_ROHRE:
        rohr_x.hinzufügen(WIN_W + ri * 200)
        rohr_y.hinzufügen(zufall(100, WIN_H - LUECKE - 100))
        rohr_punkt.hinzufügen(falsch)
        setze ri auf ri + 1

# === Vogel zeichnen ===
funktion zeichne_vogel(win, y_pos):
    # Koerper
    zeichne_kreis(win, VOGEL_X, y_pos, VOGEL_R, C_VOGEL)
    zeichne_kreis(win, VOGEL_X + 2, y_pos - 2, VOGEL_R - 2, "#FFEB3B")
    # Auge
    zeichne_kreis(win, VOGEL_X + 8, y_pos - 6, 6, C_VOGEL_AUGE)
    zeichne_kreis(win, VOGEL_X + 10, y_pos - 6, 3, C_VOGEL_PUPILLE)
    # Schnabel
    zeichne_rechteck(win, VOGEL_X + 14, y_pos - 2, 12, 6, C_VOGEL_SCHNABEL)
    zeichne_rechteck(win, VOGEL_X + 14, y_pos + 2, 10, 4, "#E65100")
    # Fluegel
    setze fluegel_y auf y_pos + 2
    wenn vogel_vy < -2:
        setze fluegel_y auf y_pos - 4
    zeichne_rechteck(win, VOGEL_X - 10, fluegel_y, 14, 8, "#FFC107")

# === Rohr zeichnen ===
funktion zeichne_rohr(win, rx, ry):
    # Oberes Rohr
    zeichne_rechteck(win, rx, 0, ROHR_BREITE, ry, C_ROHR)
    zeichne_rechteck(win, rx + 4, 0, ROHR_BREITE - 8, ry, "#66BB6A")
    # Oberer Rand
    zeichne_rechteck(win, rx - 4, ry - 24, ROHR_BREITE + 8, 24, C_ROHR_RAND)
    zeichne_rechteck(win, rx - 2, ry - 22, ROHR_BREITE + 4, 20, C_ROHR)

    # Unteres Rohr
    setze unten_y auf ry + LUECKE
    zeichne_rechteck(win, rx, unten_y, ROHR_BREITE, WIN_H - unten_y, C_ROHR)
    zeichne_rechteck(win, rx + 4, unten_y, ROHR_BREITE - 8, WIN_H - unten_y, "#66BB6A")
    # Unterer Rand
    zeichne_rechteck(win, rx - 4, unten_y, ROHR_BREITE + 8, 24, C_ROHR_RAND)
    zeichne_rechteck(win, rx - 2, unten_y + 2, ROHR_BREITE + 4, 20, C_ROHR)

# === Boden zeichnen ===
funktion zeichne_boden(win):
    zeichne_rechteck(win, 0, WIN_H - 60, WIN_W, 60, C_BODEN)
    zeichne_rechteck(win, 0, WIN_H - 60, WIN_W, 8, C_BODEN_GRAS)

# === Wolken ===
funktion zeichne_wolken(win):
    zeichne_kreis(win, 80, 80, 30, "#FFFFFF")
    zeichne_kreis(win, 110, 75, 25, "#FFFFFF")
    zeichne_kreis(win, 60, 85, 20, "#FFFFFF")
    zeichne_kreis(win, 280, 120, 22, "#FFFFFF")
    zeichne_kreis(win, 305, 115, 18, "#FFFFFF")
    zeichne_kreis(win, 265, 125, 16, "#FFFFFF")

# === HUD ===
funktion zeichne_hud(win):
    # Score als grosse Kreise oben
    setze si auf 0
    solange si < score und si < 50:
        setze sx_pos auf WIN_W / 2 - (min(score, 50) * 6) / 2 + si * 6
        zeichne_kreis(win, sx_pos, 40, 4, "#FFD700")
        setze si auf si + 1

# === Kollision ===
funktion check_kollision():
    # Boden
    wenn vogel_y + VOGEL_R > WIN_H - 60:
        gib_zurück wahr
    # Decke
    wenn vogel_y - VOGEL_R < 0:
        gib_zurück wahr
    # Rohre
    setze ri auf 0
    solange ri < länge(rohr_x):
        setze rx auf rohr_x[ri]
        setze ry auf rohr_y[ri]
        # Horizontal im Rohr-Bereich?
        wenn VOGEL_X + VOGEL_R > rx und VOGEL_X - VOGEL_R < rx + ROHR_BREITE:
            # Oben oder unten?
            wenn vogel_y - VOGEL_R < ry oder vogel_y + VOGEL_R > ry + LUECKE:
                gib_zurück wahr
        setze ri auf ri + 1
    gib_zurück falsch

# === Neues Spiel ===
funktion neues_spiel():
    setze vogel_y auf 250.0
    setze vogel_vy auf 0.0
    setze score auf 0
    setze game_over auf falsch
    setze gestartet auf falsch
    init_rohre()

# === Hauptprogramm ===
zeige "=== moo Flappy Bird ==="
zeige "Leertaste = Fliegen, Escape = Beenden"

setze win auf fenster_erstelle("moo Flappy", WIN_W, WIN_H)
init_rohre()

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Flap
    wenn flap_cooldown > 0:
        setze flap_cooldown auf flap_cooldown - 1

    wenn taste_gedrückt("leertaste") und flap_cooldown == 0:
        wenn game_over:
            neues_spiel()
        sonst:
            setze gestartet auf wahr
            setze vogel_vy auf FLAP
        setze flap_cooldown auf 8

    # Physik
    wenn gestartet und game_over == falsch:
        setze vogel_vy auf vogel_vy + SCHWERKRAFT
        setze vogel_y auf vogel_y + vogel_vy

        # Rohre bewegen
        setze ri auf 0
        solange ri < länge(rohr_x):
            rohr_x[ri] = rohr_x[ri] - ROHR_SPEED

            # Punkt zaehlen
            wenn rohr_x[ri] + ROHR_BREITE < VOGEL_X und rohr_punkt[ri] == falsch:
                setze score auf score + 1
                rohr_punkt[ri] = wahr

            # Rohr recyclen
            wenn rohr_x[ri] < -ROHR_BREITE:
                rohr_x[ri] = rohr_x[ri] + MAX_ROHRE * 200
                rohr_y[ri] = zufall(100, WIN_H - LUECKE - 100)
                rohr_punkt[ri] = falsch

            setze ri auf ri + 1

        # Kollision
        wenn check_kollision():
            setze game_over auf wahr
            wenn score > highscore:
                setze highscore auf score

    # === Zeichnen ===
    fenster_löschen(win, C_HIMMEL)
    zeichne_wolken(win)

    # Rohre
    setze ri auf 0
    solange ri < länge(rohr_x):
        zeichne_rohr(win, rohr_x[ri], rohr_y[ri])
        setze ri auf ri + 1

    zeichne_boden(win)
    zeichne_vogel(win, vogel_y)
    zeichne_hud(win)

    # Game Over Anzeige
    wenn game_over:
        zeichne_rechteck(win, WIN_W / 2 - 80, WIN_H / 2 - 30, 160, 60, "#D32F2F")
        zeichne_rechteck(win, WIN_W / 2 - 78, WIN_H / 2 - 28, 156, 56, "#F44336")
        # "X" Symbol
        zeichne_linie(win, WIN_W / 2 - 10, WIN_H / 2 - 10, WIN_W / 2 + 10, WIN_H / 2 + 10, "#FFFFFF")
        zeichne_linie(win, WIN_W / 2 + 10, WIN_H / 2 - 10, WIN_W / 2 - 10, WIN_H / 2 + 10, "#FFFFFF")

    # Start-Anzeige
    wenn gestartet == falsch und game_over == falsch:
        zeichne_rechteck(win, WIN_W / 2 - 60, WIN_H / 2 + 40, 120, 30, "#2196F3")
        zeichne_kreis(win, WIN_W / 2, WIN_H / 2 + 55, 8, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Flappy beendet. Score: " + text(score) + " Highscore: " + text(highscore)

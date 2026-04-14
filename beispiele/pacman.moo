# ============================================================
# moo Pac-Man — Klassisches Labyrinth-Spiel
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/pacman.moo -o beispiele/pacman
#   ./beispiele/pacman
#
# Bedienung:
#   WASD oder Pfeiltasten - Richtung aendern
#   Escape - Beenden
#
# Features:
#   * 21x21 Labyrinth
#   * Punkte einsammeln
#   * 4 Geister mit verschiedenen KI-Strategien
#   * Power-Pellets: Geister werden verletzbar
#   * 3 Leben
#   * Klassischer Pac-Man Look
# ============================================================

setze BREITE auf 672
setze HOEHE auf 720
setze TILE auf 32
setze GRID_W auf 21
setze GRID_H auf 21
setze OFFSET_X auf 0
setze OFFSET_Y auf 48

# Karte: 0=leer+punkt, 1=wand, 2=leer, 3=power-pellet, 4=geister-haus
setze karte auf []

# Labyrinth definieren (vereinfacht)
# 1=Wand, 0=Punkt, 3=Power-Pellet, 2=leer, 4=Geister-Haus
setze layout auf [
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,
    1,0,1,1,0,1,1,1,0,0,1,0,0,1,1,1,0,1,1,0,1,
    1,3,1,1,0,1,1,1,0,0,0,0,0,1,1,1,0,1,1,3,1,
    1,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0,0,0,1,
    1,0,1,1,0,1,0,1,1,1,2,1,1,1,0,1,0,1,1,0,1,
    1,0,0,0,0,1,0,0,0,0,2,0,0,0,0,1,0,0,0,0,1,
    1,1,1,1,0,1,1,1,0,1,2,1,0,1,1,1,0,1,1,1,1,
    2,2,2,1,0,1,2,2,2,1,4,1,2,2,2,1,0,1,2,2,2,
    1,1,1,1,0,1,2,1,1,4,4,4,1,1,2,1,0,1,1,1,1,
    2,2,2,2,0,2,2,1,4,4,4,4,4,1,2,2,0,2,2,2,2,
    1,1,1,1,0,1,2,1,1,1,1,1,1,1,2,1,0,1,1,1,1,
    2,2,2,1,0,1,2,2,2,2,2,2,2,2,2,1,0,1,2,2,2,
    1,1,1,1,0,1,0,1,1,1,1,1,1,1,0,1,0,1,1,1,1,
    1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,1,
    1,0,1,1,0,1,1,1,0,0,1,0,0,1,1,1,0,1,1,0,1,
    1,3,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,3,1,
    1,1,0,1,0,1,0,1,1,1,1,1,1,1,0,1,0,1,0,1,1,
    1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,
    1,0,1,1,1,1,1,1,0,0,1,0,0,1,1,1,1,1,1,0,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
]

# Layout in Karte kopieren
setze idx auf 0
solange idx < GRID_W * GRID_H:
    karte.hinzufügen(layout[idx])
    setze idx auf idx + 1

# Punkte zaehlen
setze total_punkte auf 0
setze idx auf 0
solange idx < GRID_W * GRID_H:
    wenn karte[idx] == 0 oder karte[idx] == 3:
        setze total_punkte auf total_punkte + 1
    setze idx auf idx + 1

# === PAC-MAN ===
setze pac_x auf 10.0
setze pac_y auf 16.0
setze pac_dx auf 0
setze pac_dy auf 0
setze pac_next_dx auf 0
setze pac_next_dy auf 0
setze pac_speed auf 0.08
setze pac_mund auf 0
setze pac_mund_dir auf 1
setze leben auf 3
setze score auf 0
setze power_timer auf 0
setze eingesammelt auf 0

# === GEISTER ===
setze GEISTER_COUNT auf 4
setze geist_x auf [9.0, 10.0, 11.0, 10.0]
setze geist_y auf [9.0, 9.0, 9.0, 10.0]
setze geist_dx auf [0, 0, 0, 0]
setze geist_dy auf [-1, -1, -1, 1]
setze geist_farbe auf ["#F44336", "#FF9800", "#00BCD4", "#E91E63"]
setze geist_aktiv auf [wahr, wahr, wahr, wahr]
setze geist_verletzbar auf [falsch, falsch, falsch, falsch]

setze unverwundbar auf 0

# === HILFSFUNKTIONEN ===
funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

funktion ist_wand(gx, gy):
    wenn gx < 0 oder gx >= GRID_W oder gy < 0 oder gy >= GRID_H:
        gib_zurück falsch
    setze val auf karte[gy * GRID_W + gx]
    gib_zurück val == 1 oder val == 4

funktion kann_gehen(gx, gy):
    wenn gx < 0 oder gx >= GRID_W:
        gib_zurück wahr
    wenn gy < 0 oder gy >= GRID_H:
        gib_zurück falsch
    gib_zurück nicht ist_wand(gx, gy)

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Pac-Man", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn leben <= 0:
        stopp
    wenn eingesammelt >= total_punkte:
        stopp

    # === INPUT ===
    wenn taste_gedrückt("w") oder taste_gedrückt("hoch"):
        setze pac_next_dx auf 0
        setze pac_next_dy auf -1
    wenn taste_gedrückt("s") oder taste_gedrückt("runter"):
        setze pac_next_dx auf 0
        setze pac_next_dy auf 1
    wenn taste_gedrückt("a") oder taste_gedrückt("links"):
        setze pac_next_dx auf -1
        setze pac_next_dy auf 0
    wenn taste_gedrückt("d") oder taste_gedrückt("rechts"):
        setze pac_next_dx auf 1
        setze pac_next_dy auf 0

    # Richtungswechsel pruefen (an Grid-Punkt)
    setze gx auf boden(pac_x + 0.5)
    setze gy auf boden(pac_y + 0.5)
    setze near_center auf abs_wert(pac_x - gx) < 0.15 und abs_wert(pac_y - gy) < 0.15
    wenn near_center:
        wenn kann_gehen(gx + pac_next_dx, gy + pac_next_dy):
            setze pac_dx auf pac_next_dx
            setze pac_dy auf pac_next_dy

    # Pac-Man bewegen
    setze nx auf pac_x + pac_dx * pac_speed
    setze ny auf pac_y + pac_dy * pac_speed
    setze ngx auf boden(nx + 0.5)
    setze ngy auf boden(ny + 0.5)

    # Wand-Check
    wenn near_center und ist_wand(gx + pac_dx, gy + pac_dy):
        setze pac_dx auf 0
        setze pac_dy auf 0
    sonst:
        setze pac_x auf nx
        setze pac_y auf ny

    # Tunnel (Wrap-Around)
    wenn pac_x < -0.5:
        setze pac_x auf GRID_W - 0.5
    wenn pac_x > GRID_W - 0.5:
        setze pac_x auf -0.5

    # Punkte einsammeln
    setze gx auf boden(pac_x + 0.5)
    setze gy auf boden(pac_y + 0.5)
    wenn gx >= 0 und gx < GRID_W und gy >= 0 und gy < GRID_H:
        setze tile_val auf karte[gy * GRID_W + gx]
        wenn tile_val == 0:
            karte[gy * GRID_W + gx] = 2
            setze score auf score + 10
            setze eingesammelt auf eingesammelt + 1
        wenn tile_val == 3:
            karte[gy * GRID_W + gx] = 2
            setze score auf score + 50
            setze eingesammelt auf eingesammelt + 1
            setze power_timer auf 300
            setze gi auf 0
            solange gi < GEISTER_COUNT:
                geist_verletzbar[gi] = wahr
                setze gi auf gi + 1

    wenn power_timer > 0:
        setze power_timer auf power_timer - 1
        wenn power_timer == 0:
            setze gi auf 0
            solange gi < GEISTER_COUNT:
                geist_verletzbar[gi] = falsch
                setze gi auf gi + 1

    # === GEISTER BEWEGEN ===
    setze gi auf 0
    solange gi < GEISTER_COUNT:
        wenn geist_aktiv[gi]:
            setze gx auf boden(geist_x[gi] + 0.5)
            setze gy auf boden(geist_y[gi] + 0.5)
            setze near auf abs_wert(geist_x[gi] - gx) < 0.1 und abs_wert(geist_y[gi] - gy) < 0.1

            wenn near:
                # Einfache KI: zufaellige Richtung an Kreuzungen
                setze dirs auf []
                wenn kann_gehen(gx, gy - 1) und geist_dy[gi] != 1:
                    dirs.hinzufügen(0)
                wenn kann_gehen(gx, gy + 1) und geist_dy[gi] != -1:
                    dirs.hinzufügen(1)
                wenn kann_gehen(gx - 1, gy) und geist_dx[gi] != 1:
                    dirs.hinzufügen(2)
                wenn kann_gehen(gx + 1, gy) und geist_dx[gi] != -1:
                    dirs.hinzufügen(3)
                wenn länge(dirs) > 0:
                    setze rng_state auf rng_state + gi * 13 + gx * 7 + gy
                    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
                    wenn rng_state < 0:
                        setze rng_state auf 0 - rng_state
                    setze choice auf dirs[rng_state % länge(dirs)]
                    wenn choice == 0:
                        geist_dx[gi] = 0
                        geist_dy[gi] = -1
                    wenn choice == 1:
                        geist_dx[gi] = 0
                        geist_dy[gi] = 1
                    wenn choice == 2:
                        geist_dx[gi] = -1
                        geist_dy[gi] = 0
                    wenn choice == 3:
                        geist_dx[gi] = 1
                        geist_dy[gi] = 0

            setze g_speed auf 0.05
            wenn geist_verletzbar[gi]:
                setze g_speed auf 0.03
            geist_x[gi] = geist_x[gi] + geist_dx[gi] * g_speed
            geist_y[gi] = geist_y[gi] + geist_dy[gi] * g_speed

            # Tunnel
            wenn geist_x[gi] < -0.5:
                geist_x[gi] = GRID_W - 0.5
            wenn geist_x[gi] > GRID_W - 0.5:
                geist_x[gi] = -0.5

            # Kollision mit Pac-Man
            wenn unverwundbar <= 0:
                wenn abs_wert(pac_x - geist_x[gi]) < 0.6 und abs_wert(pac_y - geist_y[gi]) < 0.6:
                    wenn geist_verletzbar[gi]:
                        # Geist gefressen
                        geist_x[gi] = 10.0
                        geist_y[gi] = 9.0
                        geist_verletzbar[gi] = falsch
                        setze score auf score + 200
                    sonst:
                        # Pac-Man stirbt
                        setze leben auf leben - 1
                        setze pac_x auf 10.0
                        setze pac_y auf 16.0
                        setze pac_dx auf 0
                        setze pac_dy auf 0
                        setze unverwundbar auf 60
        setze gi auf gi + 1

    wenn unverwundbar > 0:
        setze unverwundbar auf unverwundbar - 1

    # Mund-Animation
    setze pac_mund auf pac_mund + pac_mund_dir
    wenn pac_mund > 8:
        setze pac_mund_dir auf -1
    wenn pac_mund < 0:
        setze pac_mund_dir auf 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#000000")

    # Labyrinth
    setze gy auf 0
    solange gy < GRID_H:
        setze gx auf 0
        solange gx < GRID_W:
            setze dx auf OFFSET_X + gx * TILE
            setze dy auf OFFSET_Y + gy * TILE
            setze val auf karte[gy * GRID_W + gx]
            wenn val == 1:
                zeichne_rechteck(win, dx + 2, dy + 2, TILE - 4, TILE - 4, "#1A237E")
                zeichne_rechteck(win, dx + 4, dy + 4, TILE - 8, TILE - 8, "#283593")
            wenn val == 0:
                zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 3, "#FFEB3B")
            wenn val == 3:
                zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 8, "#FFEB3B")
            setze gx auf gx + 1
        setze gy auf gy + 1

    # Geister
    setze gi auf 0
    solange gi < GEISTER_COUNT:
        wenn geist_aktiv[gi]:
            setze dx auf OFFSET_X + geist_x[gi] * TILE + TILE / 2
            setze dy auf OFFSET_Y + geist_y[gi] * TILE + TILE / 2
            wenn geist_verletzbar[gi]:
                wenn power_timer > 60 oder (power_timer / 5) % 2 == 0:
                    zeichne_kreis(win, dx, dy, 13, "#1565C0")
                    zeichne_rechteck(win, dx - 13, dy, 26, 13, "#1565C0")
                sonst:
                    zeichne_kreis(win, dx, dy, 13, "#FFFFFF")
                    zeichne_rechteck(win, dx - 13, dy, 26, 13, "#FFFFFF")
            sonst:
                zeichne_kreis(win, dx, dy, 13, geist_farbe[gi])
                zeichne_rechteck(win, dx - 13, dy, 26, 13, geist_farbe[gi])
            # Augen
            zeichne_kreis(win, dx - 5, dy - 3, 4, "#FFFFFF")
            zeichne_kreis(win, dx + 5, dy - 3, 4, "#FFFFFF")
            zeichne_kreis(win, dx - 4, dy - 2, 2, "#000000")
            zeichne_kreis(win, dx + 6, dy - 2, 2, "#000000")
        setze gi auf gi + 1

    # Pac-Man
    wenn unverwundbar == 0 oder (unverwundbar / 3) % 2 == 0:
        setze dx auf OFFSET_X + pac_x * TILE + TILE / 2
        setze dy auf OFFSET_Y + pac_y * TILE + TILE / 2
        zeichne_kreis(win, dx, dy, 14, "#FFEB3B")
        # Mund (schwarzes Dreieck simuliert mit Rechteck)
        wenn pac_mund > 2:
            wenn pac_dx == 1:
                zeichne_rechteck(win, dx, dy - pac_mund, 15, pac_mund * 2, "#000000")
            wenn pac_dx == -1:
                zeichne_rechteck(win, dx - 15, dy - pac_mund, 15, pac_mund * 2, "#000000")
            wenn pac_dy == 1:
                zeichne_rechteck(win, dx - pac_mund, dy, pac_mund * 2, 15, "#000000")
            wenn pac_dy == -1:
                zeichne_rechteck(win, dx - pac_mund, dy - 15, pac_mund * 2, 15, "#000000")
        # Auge
        zeichne_kreis(win, dx - 3 + pac_dx * 5, dy - 5 + pac_dy * 5, 3, "#000000")

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 44, "#000000")
    # Score als Punkte
    setze score_dots auf score / 50
    wenn score_dots > 30:
        setze score_dots auf 30
    setze si auf 0
    solange si < score_dots:
        zeichne_kreis(win, 15 + si * 18, 15, 5, "#FFD700")
        setze si auf si + 1
    # Leben
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, BREITE - 20 - li * 30, 22, 10, "#FFEB3B")
        setze li auf li + 1
    # Power-Timer
    wenn power_timer > 0:
        setze pw auf power_timer * 100 / 300
        zeichne_rechteck(win, BREITE / 2 - 50, 35, pw, 6, "#1565C0")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn eingesammelt >= total_punkte:
    zeige "GEWONNEN! Score: " + text(score) + " | Leben: " + text(leben)
sonst:
    zeige "GAME OVER! Score: " + text(score)

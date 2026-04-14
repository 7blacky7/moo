# ============================================================
# moo Tron — Lichtrennen (2 Spieler)
#
# Kompilieren: moo-compiler compile beispiele/tron.moo -o beispiele/tron
# Starten:     ./beispiele/tron
#
# Spieler 1 (Blau): WASD
# Spieler 2 (Rot): Pfeiltasten
# Escape = Beenden, R = Neustart
# ============================================================

setze BREITE auf 600
setze HOEHE auf 600
setze CELL auf 6
setze GRID_W auf BREITE / CELL
setze GRID_H auf HOEHE / CELL

# Grid (0=leer, 1=P1, 2=P2)
setze grid auf []
setze gi auf 0
solange gi < GRID_W * GRID_H:
    grid.hinzufügen(0)
    setze gi auf gi + 1

# Spieler 1 (Blau)
setze p1_x auf 25
setze p1_y auf 50
setze p1_dx auf 1
setze p1_dy auf 0
setze p1_lebt auf wahr

# Spieler 2 (Rot)
setze p2_x auf 75
setze p2_y auf 50
setze p2_dx auf -1
setze p2_dy auf 0
setze p2_lebt auf wahr

setze game_over auf falsch
setze frame auf 0
setze speed auf 2

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Tron", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn nicht game_over:
        # === INPUT P1 (WASD) ===
        wenn taste_gedrückt("w") und p1_dy != 1:
            setze p1_dx auf 0
            setze p1_dy auf -1
        wenn taste_gedrückt("s") und p1_dy != -1:
            setze p1_dx auf 0
            setze p1_dy auf 1
        wenn taste_gedrückt("a") und p1_dx != 1:
            setze p1_dx auf -1
            setze p1_dy auf 0
        wenn taste_gedrückt("d") und p1_dx != -1:
            setze p1_dx auf 1
            setze p1_dy auf 0

        # === INPUT P2 (Pfeiltasten) ===
        wenn taste_gedrückt("oben") und p2_dy != 1:
            setze p2_dx auf 0
            setze p2_dy auf -1
        wenn taste_gedrückt("unten") und p2_dy != -1:
            setze p2_dx auf 0
            setze p2_dy auf 1
        wenn taste_gedrückt("links") und p2_dx != 1:
            setze p2_dx auf -1
            setze p2_dy auf 0
        wenn taste_gedrückt("rechts") und p2_dx != -1:
            setze p2_dx auf 1
            setze p2_dy auf 0

        # Bewegen (alle N Frames)
        setze frame auf frame + 1
        wenn frame >= speed:
            setze frame auf 0

            # P1 bewegen
            wenn p1_lebt:
                setze p1_x auf p1_x + p1_dx
                setze p1_y auf p1_y + p1_dy

                # Wandkollision
                wenn p1_x < 0 oder p1_x >= GRID_W oder p1_y < 0 oder p1_y >= GRID_H:
                    setze p1_lebt auf falsch
                sonst:
                    setze idx auf p1_y * GRID_W + p1_x
                    wenn grid[idx] != 0:
                        setze p1_lebt auf falsch
                    sonst:
                        setze grid[idx] auf 1

            # P2 bewegen
            wenn p2_lebt:
                setze p2_x auf p2_x + p2_dx
                setze p2_y auf p2_y + p2_dy

                wenn p2_x < 0 oder p2_x >= GRID_W oder p2_y < 0 oder p2_y >= GRID_H:
                    setze p2_lebt auf falsch
                sonst:
                    setze idx auf p2_y * GRID_W + p2_x
                    wenn grid[idx] != 0:
                        setze p2_lebt auf falsch
                    sonst:
                        setze grid[idx] auf 2

            # Game Over Check
            wenn nicht p1_lebt oder nicht p2_lebt:
                setze game_over auf wahr

    # === ZEICHNEN ===
    fenster_löschen(win, "#111111")

    # Grid zeichnen
    setze gy auf 0
    solange gy < GRID_H:
        setze gx auf 0
        solange gx < GRID_W:
            setze val auf grid[gy * GRID_W + gx]
            wenn val == 1:
                zeichne_rechteck(win, gx * CELL, gy * CELL, CELL, CELL, "#1565C0")
            wenn val == 2:
                zeichne_rechteck(win, gx * CELL, gy * CELL, CELL, CELL, "#C62828")
            setze gx auf gx + 1
        setze gy auf gy + 1

    # Koepfe (heller)
    wenn p1_lebt:
        zeichne_rechteck(win, p1_x * CELL, p1_y * CELL, CELL, CELL, "#42A5F5")
    wenn p2_lebt:
        zeichne_rechteck(win, p2_x * CELL, p2_y * CELL, CELL, CELL, "#EF5350")

    # Game Over Anzeige
    wenn game_over:
        zeichne_rechteck(win, BREITE / 2 - 80, HOEHE / 2 - 20, 160, 40, "#333333")
        wenn p1_lebt und nicht p2_lebt:
            # P1 gewinnt
            zeichne_rechteck(win, BREITE / 2 - 60, HOEHE / 2 - 10, 120, 20, "#1565C0")
        wenn p2_lebt und nicht p1_lebt:
            # P2 gewinnt
            zeichne_rechteck(win, BREITE / 2 - 60, HOEHE / 2 - 10, 120, 20, "#C62828")
        wenn nicht p1_lebt und nicht p2_lebt:
            # Unentschieden
            zeichne_rechteck(win, BREITE / 2 - 60, HOEHE / 2 - 10, 60, 20, "#1565C0")
            zeichne_rechteck(win, BREITE / 2, HOEHE / 2 - 10, 60, 20, "#C62828")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn p1_lebt und nicht p2_lebt:
    zeige "Spieler 1 (Blau) gewinnt!"
wenn p2_lebt und nicht p1_lebt:
    zeige "Spieler 2 (Rot) gewinnt!"
wenn nicht p1_lebt und nicht p2_lebt:
    zeige "Unentschieden!"

# ============================================================
# moo Maze — Prozedurales Labyrinth
#
# Kompilieren: moo-compiler compile beispiele/maze.moo -o beispiele/maze
# Starten:     ./beispiele/maze
#
# WASD/Pfeiltasten = Bewegen
# R = Neues Labyrinth, Escape = Beenden
# ============================================================

setze BREITE auf 640
setze HOEHE auf 640
setze MAZE_W auf 31
setze MAZE_H auf 31
setze CELL auf 20

# Maze-Grid (1=Wand, 0=Weg)
setze maze auf []
setze mi auf 0
solange mi < MAZE_W * MAZE_H:
    maze.hinzufügen(1)
    setze mi auf mi + 1

# Rekursiver Backtracker (iterativ mit Stack)
# Start bei (1,1)
setze sx auf []
setze sy auf []
sx.hinzufügen(1)
sy.hinzufügen(1)
setze maze[1 * MAZE_W + 1] auf 0
setze stack_len auf 1
setze seed auf 42

solange stack_len > 0:
    setze cx auf sx[stack_len - 1]
    setze cy auf sy[stack_len - 1]

    # Nachbarn pruefen (2 Schritte entfernt)
    setze richtungen auf []
    # Oben
    wenn cy >= 3 und maze[(cy - 2) * MAZE_W + cx] == 1:
        richtungen.hinzufügen(0)
    # Rechts
    wenn cx <= MAZE_W - 4 und maze[cy * MAZE_W + cx + 2] == 1:
        richtungen.hinzufügen(1)
    # Unten
    wenn cy <= MAZE_H - 4 und maze[(cy + 2) * MAZE_W + cx] == 1:
        richtungen.hinzufügen(2)
    # Links
    wenn cx >= 3 und maze[cy * MAZE_W + cx - 2] == 1:
        richtungen.hinzufügen(3)

    wenn länge(richtungen) > 0:
        # Zufaellige Richtung
        setze seed auf (seed * 1103515245 + 12345) % 2147483648
        setze ri auf seed % länge(richtungen)
        setze richt auf richtungen[ri]

        wenn richt == 0:
            setze maze[(cy - 1) * MAZE_W + cx] auf 0
            setze maze[(cy - 2) * MAZE_W + cx] auf 0
            sx.hinzufügen(cx)
            sy.hinzufügen(cy - 2)
        wenn richt == 1:
            setze maze[cy * MAZE_W + cx + 1] auf 0
            setze maze[cy * MAZE_W + cx + 2] auf 0
            sx.hinzufügen(cx + 2)
            sy.hinzufügen(cy)
        wenn richt == 2:
            setze maze[(cy + 1) * MAZE_W + cx] auf 0
            setze maze[(cy + 2) * MAZE_W + cx] auf 0
            sx.hinzufügen(cx)
            sy.hinzufügen(cy + 2)
        wenn richt == 3:
            setze maze[cy * MAZE_W + cx - 1] auf 0
            setze maze[cy * MAZE_W + cx - 2] auf 0
            sx.hinzufügen(cx - 2)
            sy.hinzufügen(cy)
        setze stack_len auf stack_len + 1
    sonst:
        # Backtrack
        setze stack_len auf stack_len - 1

# Spieler
setze spieler_x auf 1
setze spieler_y auf 1
setze ziel_x auf MAZE_W - 2
setze ziel_y auf MAZE_H - 2
setze schritte auf 0
setze gewonnen auf falsch
setze eingabe_cd auf 0

# Muenzen auf freien Feldern
setze muenzen auf []
setze mi auf 0
solange mi < MAZE_W * MAZE_H:
    muenzen.hinzufügen(falsch)
    setze mi auf mi + 1

setze muenz_count auf 0
setze my auf 1
solange my < MAZE_H - 1:
    setze mx auf 1
    solange mx < MAZE_W - 1:
        wenn maze[my * MAZE_W + mx] == 0:
            setze seed auf (seed * 1103515245 + 12345) % 2147483648
            wenn seed % 5 == 0:
                setze muenzen[my * MAZE_W + mx] auf wahr
                setze muenz_count auf muenz_count + 1
        setze mx auf mx + 1
    setze my auf my + 1

setze gesammelt auf 0

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Maze", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn nicht gewonnen und eingabe_cd <= 0:
        wenn taste_gedrückt("w") oder taste_gedrückt("oben"):
            wenn spieler_y > 0 und maze[(spieler_y - 1) * MAZE_W + spieler_x] == 0:
                setze spieler_y auf spieler_y - 1
                setze schritte auf schritte + 1
            setze eingabe_cd auf 6
        wenn taste_gedrückt("s") oder taste_gedrückt("unten"):
            wenn spieler_y < MAZE_H - 1 und maze[(spieler_y + 1) * MAZE_W + spieler_x] == 0:
                setze spieler_y auf spieler_y + 1
                setze schritte auf schritte + 1
            setze eingabe_cd auf 6
        wenn taste_gedrückt("a") oder taste_gedrückt("links"):
            wenn spieler_x > 0 und maze[spieler_y * MAZE_W + spieler_x - 1] == 0:
                setze spieler_x auf spieler_x - 1
                setze schritte auf schritte + 1
            setze eingabe_cd auf 6
        wenn taste_gedrückt("d") oder taste_gedrückt("rechts"):
            wenn spieler_x < MAZE_W - 1 und maze[spieler_y * MAZE_W + spieler_x + 1] == 0:
                setze spieler_x auf spieler_x + 1
                setze schritte auf schritte + 1
            setze eingabe_cd auf 6

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    # Muenze einsammeln
    setze pidx auf spieler_y * MAZE_W + spieler_x
    wenn muenzen[pidx]:
        setze muenzen[pidx] auf falsch
        setze gesammelt auf gesammelt + 1

    # Ziel erreicht
    wenn spieler_x == ziel_x und spieler_y == ziel_y:
        setze gewonnen auf wahr

    # === ZEICHNEN ===
    fenster_löschen(win, "#000000")

    setze my auf 0
    solange my < MAZE_H:
        setze mx auf 0
        solange mx < MAZE_W:
            setze dx auf mx * CELL
            setze dy auf my * CELL
            wenn maze[my * MAZE_W + mx] == 1:
                zeichne_rechteck(win, dx, dy, CELL, CELL, "#37474F")
            sonst:
                zeichne_rechteck(win, dx, dy, CELL, CELL, "#1A1A2E")
                # Muenze
                wenn muenzen[my * MAZE_W + mx]:
                    zeichne_kreis(win, dx + CELL / 2, dy + CELL / 2, 4, "#FFD700")
            setze mx auf mx + 1
        setze my auf my + 1

    # Ziel
    zeichne_rechteck(win, ziel_x * CELL + 2, ziel_y * CELL + 2, CELL - 4, CELL - 4, "#4CAF50")

    # Spieler
    zeichne_kreis(win, spieler_x * CELL + CELL / 2, spieler_y * CELL + CELL / 2, 7, "#2196F3")
    zeichne_kreis(win, spieler_x * CELL + CELL / 2 + 2, spieler_y * CELL + CELL / 2 - 2, 2, "#FFFFFF")

    # HUD
    setze si auf 0
    solange si < gesammelt und si < 20:
        zeichne_kreis(win, 10 + si * 12, HOEHE - 15, 4, "#FFD700")
        setze si auf si + 1

    wenn gewonnen:
        zeichne_rechteck(win, BREITE / 2 - 80, HOEHE / 2 - 20, 160, 40, "#4CAF50")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Maze beendet! Schritte: " + text(schritte) + " Muenzen: " + text(gesammelt)

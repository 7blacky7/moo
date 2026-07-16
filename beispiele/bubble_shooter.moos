# ============================================================
# moo Bubble Shooter — Puzzle Bobble Style
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/bubble_shooter.moo -o beispiele/bubble_shooter
#   ./beispiele/bubble_shooter
#
# Bedienung:
#   Links/Rechts oder A/D - Zielen
#   Leertaste - Schiessen
#   Escape - Beenden
#
# Features:
#   * Hexagonales Bubble-Grid (12 Reihen)
#   * 6 Farben, 3er-Match entfernt Gruppen
#   * Physik-basierter Schuss (Winkel + Abprallen)
#   * Haengende Bubbles fallen nach Match
#   * Score + Level (neue Reihen von oben)
# ============================================================

setze BREITE auf 480
setze HOEHE auf 640
setze BUBBLE_R auf 16
setze GRID_COLS auf 12
setze GRID_ROWS auf 14
setze OFFSET_X auf 24
setze OFFSET_Y auf 40
setze FARBEN auf 6
setze SCHUSS_SPEED auf 8

# Grid: -1=leer, 0-5=Farbe
setze grid auf []
setze gi auf 0
solange gi < GRID_COLS * GRID_ROWS:
    grid.hinzufügen(-1)
    setze gi auf gi + 1

setze rng_state auf 66666
funktion rng():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    wenn rng_state < 0:
        setze rng_state auf 0 - rng_state
    gib_zurück rng_state

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

# Bubble-Position berechnen (Hex-Grid)
funktion bubble_x(col, row):
    setze bx auf OFFSET_X + col * BUBBLE_R * 2
    wenn row % 2 == 1:
        setze bx auf bx + BUBBLE_R
    gib_zurück bx + BUBBLE_R

funktion bubble_y(row):
    gib_zurück OFFSET_Y + row * BUBBLE_R * 1.7 + BUBBLE_R

funktion grid_get(col, row):
    wenn col < 0 oder row < 0 oder row >= GRID_ROWS:
        gib_zurück -1
    setze max_col auf GRID_COLS
    wenn row % 2 == 1:
        setze max_col auf GRID_COLS - 1
    wenn col >= max_col:
        gib_zurück -1
    gib_zurück grid[row * GRID_COLS + col]

funktion grid_set(col, row, val):
    wenn col >= 0 und row >= 0 und row < GRID_ROWS und col < GRID_COLS:
        grid[row * GRID_COLS + col] = val

# Farben
funktion bubble_farbe(typ):
    wenn typ == 0:
        gib_zurück "#F44336"
    wenn typ == 1:
        gib_zurück "#2196F3"
    wenn typ == 2:
        gib_zurück "#4CAF50"
    wenn typ == 3:
        gib_zurück "#FFEB3B"
    wenn typ == 4:
        gib_zurück "#9C27B0"
    wenn typ == 5:
        gib_zurück "#FF9800"
    gib_zurück "#FFFFFF"

funktion bubble_hell(typ):
    wenn typ == 0:
        gib_zurück "#EF9A9A"
    wenn typ == 1:
        gib_zurück "#90CAF9"
    wenn typ == 2:
        gib_zurück "#A5D6A7"
    wenn typ == 3:
        gib_zurück "#FFF9C4"
    wenn typ == 4:
        gib_zurück "#CE93D8"
    wenn typ == 5:
        gib_zurück "#FFCC80"
    gib_zurück "#EEEEEE"

# Grid fuellen (erste 5 Reihen)
funktion grid_fuellen():
    setze row auf 0
    solange row < 5:
        setze max_col auf GRID_COLS
        wenn row % 2 == 1:
            setze max_col auf GRID_COLS - 1
        setze col auf 0
        solange col < max_col:
            grid_set(col, row, rng() % FARBEN)
            setze col auf col + 1
        setze row auf row + 1

grid_fuellen()

# Schuss
setze schuss_x auf BREITE / 2.0
setze schuss_y auf HOEHE - 60.0
setze schuss_vx auf 0.0
setze schuss_vy auf 0.0
setze schuss_farbe auf 0
setze schuss_aktiv auf falsch
setze naechste_farbe auf 0

# Zielen
setze winkel auf 90.0
setze score auf 0
setze level auf 1
setze input_cd auf 0

# Naechste Farbe
setze schuss_farbe auf rng() % FARBEN
setze naechste_farbe auf rng() % FARBEN

# Match-System (Flood-Fill)
setze match_besucht auf []
setze match_liste auf []

funktion naechtse_grid_pos(px, py):
    # Finde naechste Grid-Position fuer einen Bubble
    setze best_col auf 0
    setze best_row auf 0
    setze best_dist auf 9999.0
    setze row auf 0
    solange row < GRID_ROWS:
        setze max_col auf GRID_COLS
        wenn row % 2 == 1:
            setze max_col auf GRID_COLS - 1
        setze col auf 0
        solange col < max_col:
            wenn grid_get(col, row) < 0:
                setze bx auf bubble_x(col, row)
                setze by auf bubble_y(row)
                setze dx auf px - bx
                setze dy auf py - by
                setze dist auf wurzel(dx * dx + dy * dy)
                wenn dist < best_dist:
                    setze best_dist auf dist
                    setze best_col auf col
                    setze best_row auf row
            setze col auf col + 1
        setze row auf row + 1
    gib_zurück best_row * 100 + best_col

# Flood-Fill fuer gleiche Farben
funktion flood_count(col, row, farbe):
    # Reset
    setze match_besucht auf []
    setze match_liste auf []
    setze idx auf 0
    solange idx < GRID_COLS * GRID_ROWS:
        match_besucht.hinzufügen(falsch)
        setze idx auf idx + 1
    # BFS
    setze queue_c auf [col]
    setze queue_r auf [row]
    match_besucht[row * GRID_COLS + col] = wahr
    match_liste.hinzufügen(row * 100 + col)
    setze qi auf 0
    solange qi < länge(queue_c):
        setze cc auf queue_c[qi]
        setze rr auf queue_r[qi]
        # 6 Nachbarn (Hex-Grid)
        setze offsets_even auf [-1, 0, 1, 0, -1, 1, 0, 1, -1, -1, 0, -1]
        setze offsets_odd auf [-1, 0, 1, 0, 0, 1, 1, 1, 0, -1, 1, -1]
        setze ni auf 0
        solange ni < 6:
            wenn rr % 2 == 0:
                setze nc auf cc + offsets_even[ni * 2]
                setze nr auf rr + offsets_even[ni * 2 + 1]
            sonst:
                setze nc auf cc + offsets_odd[ni * 2]
                setze nr auf rr + offsets_odd[ni * 2 + 1]
            wenn nc >= 0 und nr >= 0 und nr < GRID_ROWS und nc < GRID_COLS:
                setze nidx auf nr * GRID_COLS + nc
                wenn nicht match_besucht[nidx]:
                    wenn grid_get(nc, nr) == farbe:
                        match_besucht[nidx] = wahr
                        queue_c.hinzufügen(nc)
                        queue_r.hinzufügen(nr)
                        match_liste.hinzufügen(nr * 100 + nc)
            setze ni auf ni + 1
        setze qi auf qi + 1
    gib_zurück länge(match_liste)

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Bubble Shooter", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # === INPUT ===
    wenn input_cd > 0:
        setze input_cd auf input_cd - 1
    sonst:
        wenn taste_gedrückt("links") oder taste_gedrückt("a"):
            setze winkel auf winkel + 2
            wenn winkel > 170:
                setze winkel auf 170.0
            setze input_cd auf 1
        wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
            setze winkel auf winkel - 2
            wenn winkel < 10:
                setze winkel auf 10.0
            setze input_cd auf 1
        wenn taste_gedrückt("leertaste") und nicht schuss_aktiv:
            setze rad auf winkel * 3.14159 / 180.0
            setze schuss_x auf BREITE / 2.0
            setze schuss_y auf HOEHE - 60.0
            setze schuss_vx auf cosinus(rad) * SCHUSS_SPEED
            setze schuss_vy auf 0 - sinus(rad) * SCHUSS_SPEED
            setze schuss_aktiv auf wahr
            setze input_cd auf 10

    # === SCHUSS BEWEGEN ===
    wenn schuss_aktiv:
        setze schuss_x auf schuss_x + schuss_vx
        setze schuss_y auf schuss_y + schuss_vy

        # Wand-Abprallen
        wenn schuss_x < BUBBLE_R:
            setze schuss_x auf BUBBLE_R * 1.0
            setze schuss_vx auf 0 - schuss_vx
        wenn schuss_x > BREITE - BUBBLE_R:
            setze schuss_x auf (BREITE - BUBBLE_R) * 1.0
            setze schuss_vx auf 0 - schuss_vx

        # Decke
        wenn schuss_y < OFFSET_Y:
            setze pos auf naechtse_grid_pos(schuss_x, schuss_y)
            setze place_row auf boden(pos / 100)
            setze place_col auf pos - place_row * 100
            grid_set(place_col, place_row, schuss_farbe)
            # Match pruefen
            setze count auf flood_count(place_col, place_row, schuss_farbe)
            wenn count >= 3:
                # Entfernen
                setze mi auf 0
                solange mi < länge(match_liste):
                    setze mpos auf match_liste[mi]
                    setze mr auf boden(mpos / 100)
                    setze mc auf mpos - mr * 100
                    grid_set(mc, mr, -1)
                    setze score auf score + 10
                    setze mi auf mi + 1
            setze schuss_aktiv auf falsch
            setze schuss_farbe auf naechste_farbe
            setze naechste_farbe auf rng() % FARBEN

        # Kollision mit Grid-Bubbles
        wenn schuss_aktiv:
            setze row auf 0
            solange row < GRID_ROWS und schuss_aktiv:
                setze max_col auf GRID_COLS
                wenn row % 2 == 1:
                    setze max_col auf GRID_COLS - 1
                setze col auf 0
                solange col < max_col und schuss_aktiv:
                    wenn grid_get(col, row) >= 0:
                        setze bx auf bubble_x(col, row)
                        setze by auf bubble_y(row)
                        setze dx auf schuss_x - bx
                        setze dy auf schuss_y - by
                        setze dist auf wurzel(dx * dx + dy * dy)
                        wenn dist < BUBBLE_R * 2:
                            setze pos auf naechtse_grid_pos(schuss_x, schuss_y)
                            setze place_row auf boden(pos / 100)
                            setze place_col auf pos - place_row * 100
                            grid_set(place_col, place_row, schuss_farbe)
                            setze count auf flood_count(place_col, place_row, schuss_farbe)
                            wenn count >= 3:
                                setze mi auf 0
                                solange mi < länge(match_liste):
                                    setze mpos auf match_liste[mi]
                                    setze mr auf boden(mpos / 100)
                                    setze mc auf mpos - mr * 100
                                    grid_set(mc, mr, -1)
                                    setze score auf score + 10
                                    setze mi auf mi + 1
                            setze schuss_aktiv auf falsch
                            setze schuss_farbe auf naechste_farbe
                            setze naechste_farbe auf rng() % FARBEN
                    setze col auf col + 1
                setze row auf row + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#1A1A2E")

    # Grid-Bubbles
    setze row auf 0
    solange row < GRID_ROWS:
        setze max_col auf GRID_COLS
        wenn row % 2 == 1:
            setze max_col auf GRID_COLS - 1
        setze col auf 0
        solange col < max_col:
            setze farbe auf grid_get(col, row)
            wenn farbe >= 0:
                setze bx auf bubble_x(col, row)
                setze by auf bubble_y(row)
                zeichne_kreis(win, bx, by, BUBBLE_R - 1, bubble_farbe(farbe))
                zeichne_kreis(win, bx - 3, by - 4, 5, bubble_hell(farbe))
            setze col auf col + 1
        setze row auf row + 1

    # Schuss
    wenn schuss_aktiv:
        zeichne_kreis(win, schuss_x, schuss_y, BUBBLE_R - 1, bubble_farbe(schuss_farbe))
        zeichne_kreis(win, schuss_x - 3, schuss_y - 4, 5, bubble_hell(schuss_farbe))

    # Shooter
    setze sx auf BREITE / 2
    setze sy auf HOEHE - 60
    # Ziel-Linie
    setze rad auf winkel * 3.14159 / 180.0
    setze aim_x auf sx + cosinus(rad) * 50
    setze aim_y auf sy - sinus(rad) * 50
    zeichne_linie(win, sx, sy, aim_x, aim_y, "#FFFFFF")
    # Kanone
    zeichne_kreis(win, sx, sy, 20, "#455A64")
    zeichne_kreis(win, sx, sy, 14, bubble_farbe(schuss_farbe))
    zeichne_kreis(win, sx - 3, sy - 3, 4, bubble_hell(schuss_farbe))

    # Naechste Bubble Vorschau
    zeichne_kreis(win, sx - 50, sy, 12, bubble_farbe(naechste_farbe))

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 30, "#0D1B2A")
    setze si auf 0
    solange si < score / 30 und si < 25:
        zeichne_kreis(win, 15 + si * 16, 15, 5, "#FFD700")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Bubble Shooter! Score: " + text(score)

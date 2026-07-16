# ============================================================
# moo Qix — Territorium einnehmen (Atari-Klassiker)
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/qix.moo -o beispiele/qix
#   ./beispiele/qix
#
# Bedienung:
#   WASD - Bewegen
#   Leertaste (halten) - Linie ziehen
#   Escape - Beenden
#
# Ziel: 75% des Spielfelds einnehmen durch Linien-Zeichnen.
# Der Qix (Feind) darf deine Linie nicht treffen!
# ============================================================

setze BREITE auf 640
setze HOEHE auf 520
setze FELD_W auf 60
setze FELD_H auf 40
setze CELL auf 10
setze OFFSET_X auf 20
setze OFFSET_Y auf 40

# Grid: 0=leer, 1=gefuellt, 2=rand, 3=aktive linie
setze grid auf []
setze gi auf 0
solange gi < FELD_W * FELD_H:
    grid.hinzufügen(0)
    setze gi auf gi + 1

# Rand setzen
setze ri auf 0
solange ri < FELD_W:
    grid[ri] = 2
    grid[(FELD_H - 1) * FELD_W + ri] = 2
    setze ri auf ri + 1
setze ri auf 0
solange ri < FELD_H:
    grid[ri * FELD_W] = 2
    grid[ri * FELD_W + FELD_W - 1] = 2
    setze ri auf ri + 1

setze rng_state auf 13579
funktion rng():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    wenn rng_state < 0:
        setze rng_state auf 0 - rng_state
    gib_zurück rng_state

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

funktion grid_get(x, y):
    wenn x < 0 oder x >= FELD_W oder y < 0 oder y >= FELD_H:
        gib_zurück -1
    gib_zurück grid[y * FELD_W + x]

funktion grid_set(x, y, val):
    wenn x >= 0 und x < FELD_W und y >= 0 und y < FELD_H:
        grid[y * FELD_W + x] = val

# Spieler
setze p_x auf 0
setze p_y auf 0
setze zieht_linie auf falsch
setze linie_pfad_x auf []
setze linie_pfad_y auf []
setze bewegung_cd auf 0

# Qix (Feind)
setze qix_x auf FELD_W / 2.0
setze qix_y auf FELD_H / 2.0
setze qix_vx auf 0.4
setze qix_vy auf 0.3

setze score auf 0
setze leben auf 3
setze gefuellt_proz auf 0
setze ziel_proz auf 75

# Flood-Fill (um abgeschlossenen Bereich zu fuellen)
funktion flood_fill_von(sx, sy):
    # Markiere alle erreichbaren 0-Felder von (sx, sy) aus
    wenn grid_get(sx, sy) != 0:
        gib_zurück 0
    setze besucht auf []
    setze bi auf 0
    solange bi < FELD_W * FELD_H:
        besucht.hinzufügen(falsch)
        setze bi auf bi + 1
    setze queue_x auf [sx]
    setze queue_y auf [sy]
    besucht[sy * FELD_W + sx] = wahr
    setze qi auf 0
    setze count auf 0
    setze qix_drin auf falsch
    solange qi < länge(queue_x):
        setze cx auf queue_x[qi]
        setze cy auf queue_y[qi]
        setze count auf count + 1
        wenn boden(qix_x) == cx und boden(qix_y) == cy:
            setze qix_drin auf wahr
        setze dirs_x auf [1, -1, 0, 0]
        setze dirs_y auf [0, 0, 1, -1]
        setze di auf 0
        solange di < 4:
            setze nx auf cx + dirs_x[di]
            setze ny auf cy + dirs_y[di]
            wenn nx >= 0 und nx < FELD_W und ny >= 0 und ny < FELD_H:
                wenn grid_get(nx, ny) == 0 und nicht besucht[ny * FELD_W + nx]:
                    besucht[ny * FELD_W + nx] = wahr
                    queue_x.hinzufügen(nx)
                    queue_y.hinzufügen(ny)
            setze di auf di + 1
        setze qi auf qi + 1
    # Wenn Qix NICHT drin → fuellen
    wenn nicht qix_drin:
        setze fi auf 0
        solange fi < FELD_W * FELD_H:
            wenn besucht[fi]:
                grid[fi] = 1
            setze fi auf fi + 1
        gib_zurück count
    gib_zurück 0

funktion linie_abschliessen():
    # Pfad zu Rand konvertieren
    setze pi auf 0
    solange pi < länge(linie_pfad_x):
        grid_set(linie_pfad_x[pi], linie_pfad_y[pi], 2)
        setze pi auf pi + 1
    # Versuche Flood-Fill von beiden Seiten der letzten Linie
    # Suche alle 0-Felder und fuelle die ohne Qix
    setze fy auf 1
    solange fy < FELD_H - 1:
        setze fx auf 1
        solange fx < FELD_W - 1:
            wenn grid_get(fx, fy) == 0:
                setze filled auf flood_fill_von(fx, fy)
                setze score auf score + filled * 10
            setze fx auf fx + 1
        setze fy auf fy + 1
    # Pfad-Listen leeren
    setze linie_pfad_x auf []
    setze linie_pfad_y auf []

# Spieler auf Rand setzen (Start unten Mitte)
setze p_x auf FELD_W / 2
setze p_y auf FELD_H - 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Qix", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn leben <= 0:
        stopp
    # Ziel erreicht?
    setze gefuellt_count auf 0
    setze total_count auf 0
    setze fi auf 0
    solange fi < FELD_W * FELD_H:
        wenn grid[fi] == 1 oder grid[fi] == 2:
            setze gefuellt_count auf gefuellt_count + 1
        setze total_count auf total_count + 1
        setze fi auf fi + 1
    setze gefuellt_proz auf gefuellt_count * 100 / total_count
    wenn gefuellt_proz >= ziel_proz:
        stopp

    # === INPUT ===
    wenn bewegung_cd > 0:
        setze bewegung_cd auf bewegung_cd - 1
    sonst:
        setze dx auf 0
        setze dy auf 0
        wenn taste_gedrückt("w"):
            setze dy auf -1
        wenn taste_gedrückt("s"):
            setze dy auf 1
        wenn taste_gedrückt("a"):
            setze dx auf -1
        wenn taste_gedrückt("d"):
            setze dx auf 1

        wenn dx != 0 oder dy != 0:
            setze nx auf p_x + dx
            setze ny auf p_y + dy
            setze target auf grid_get(nx, ny)

            wenn zieht_linie:
                # Nur auf leere Felder
                wenn target == 0:
                    setze p_x auf nx
                    setze p_y auf ny
                    grid_set(p_x, p_y, 3)
                    linie_pfad_x.hinzufügen(p_x)
                    linie_pfad_y.hinzufügen(p_y)
                wenn target == 2:
                    # Rand erreicht → Linie abschliessen
                    setze p_x auf nx
                    setze p_y auf ny
                    linie_abschliessen()
                    setze zieht_linie auf falsch
            sonst:
                # Nur auf Rand/gefuellte Bereiche
                wenn target == 2 oder target == 1:
                    setze p_x auf nx
                    setze p_y auf ny
                # Linie starten (wenn Leertaste)
                wenn taste_gedrückt("leertaste") und target == 0:
                    setze p_x auf nx
                    setze p_y auf ny
                    setze zieht_linie auf wahr
                    grid_set(p_x, p_y, 3)
                    linie_pfad_x.hinzufügen(p_x)
                    linie_pfad_y.hinzufügen(p_y)
            setze bewegung_cd auf 4

    # === QIX BEWEGEN ===
    setze qix_nx auf qix_x + qix_vx
    setze qix_ny auf qix_y + qix_vy
    setze qix_gx auf boden(qix_nx)
    setze qix_gy auf boden(qix_ny)
    setze qix_tile auf grid_get(qix_gx, qix_gy)
    # Ablenken an Waenden
    wenn qix_tile == 1 oder qix_tile == 2 oder qix_tile < 0:
        setze qix_vx auf 0 - qix_vx + (rng() % 100 - 50) * 0.005
        setze qix_vy auf 0 - qix_vy + (rng() % 100 - 50) * 0.005
        # Normalisieren
        setze mag auf wurzel(qix_vx * qix_vx + qix_vy * qix_vy)
        wenn mag > 0:
            setze qix_vx auf qix_vx / mag * 0.5
            setze qix_vy auf qix_vy / mag * 0.5
    sonst:
        setze qix_x auf qix_nx
        setze qix_y auf qix_ny

    # Qix trifft aktive Linie?
    wenn zieht_linie:
        setze pi auf 0
        solange pi < länge(linie_pfad_x):
            wenn abs_wert(qix_x - linie_pfad_x[pi]) < 1.2 und abs_wert(qix_y - linie_pfad_y[pi]) < 1.2:
                setze leben auf leben - 1
                # Linie loeschen
                setze li auf 0
                solange li < länge(linie_pfad_x):
                    grid_set(linie_pfad_x[li], linie_pfad_y[li], 0)
                    setze li auf li + 1
                setze linie_pfad_x auf []
                setze linie_pfad_y auf []
                setze zieht_linie auf falsch
                setze p_x auf FELD_W / 2
                setze p_y auf FELD_H - 1
                setze pi auf länge(linie_pfad_x)
            setze pi auf pi + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#000022")

    # Grid
    setze gy auf 0
    solange gy < FELD_H:
        setze gx auf 0
        solange gx < FELD_W:
            setze val auf grid_get(gx, gy)
            setze dx auf OFFSET_X + gx * CELL
            setze dy auf OFFSET_Y + gy * CELL
            wenn val == 1:
                zeichne_rechteck(win, dx, dy, CELL, CELL, "#1976D2")
                zeichne_rechteck(win, dx + 1, dy + 1, CELL - 2, CELL - 2, "#2196F3")
            wenn val == 2:
                zeichne_rechteck(win, dx, dy, CELL, CELL, "#FFEB3B")
            wenn val == 3:
                zeichne_rechteck(win, dx, dy, CELL, CELL, "#E91E63")
            setze gx auf gx + 1
        setze gy auf gy + 1

    # Qix (farbwechselnd)
    setze qix_farbe auf "#F44336"
    wenn rng_state % 4 == 1:
        setze qix_farbe auf "#4CAF50"
    wenn rng_state % 4 == 2:
        setze qix_farbe auf "#FF9800"
    wenn rng_state % 4 == 3:
        setze qix_farbe auf "#9C27B0"
    setze qix_dx auf OFFSET_X + qix_x * CELL
    setze qix_dy auf OFFSET_Y + qix_y * CELL
    zeichne_kreis(win, qix_dx, qix_dy, 8, qix_farbe)
    zeichne_kreis(win, qix_dx + 4, qix_dy - 2, 4, "#FFFFFF")
    zeichne_kreis(win, qix_dx - 4, qix_dy + 3, 4, "#FFFFFF")

    # Spieler
    setze p_dx auf OFFSET_X + p_x * CELL + CELL / 2
    setze p_dy auf OFFSET_Y + p_y * CELL + CELL / 2
    zeichne_kreis(win, p_dx, p_dy, 6, "#FFFFFF")
    zeichne_kreis(win, p_dx, p_dy, 4, "#F44336")

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 30, "#1B2631")
    # Fortschritts-Balken
    setze bar_w auf gefuellt_proz * 200 / 100
    zeichne_rechteck(win, 10, 10, 200, 12, "#333333")
    zeichne_rechteck(win, 10, 10, bar_w, 12, "#4CAF50")
    # Ziel-Marker bei 75%
    setze ziel_x auf 10 + ziel_proz * 2
    zeichne_rechteck(win, ziel_x, 8, 2, 16, "#FFEB3B")
    # Leben
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, BREITE - 20 - li * 25, 15, 8, "#F44336")
        setze li auf li + 1
    # Score
    setze si auf 0
    solange si < score / 50 und si < 25:
        zeichne_kreis(win, 230 + si * 12, 15, 3, "#FFD700")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn gefuellt_proz >= ziel_proz:
    zeige "GEWONNEN! " + text(gefuellt_proz) + "% | Score: " + text(score)
sonst:
    zeige "GAME OVER! " + text(gefuellt_proz) + "% | Score: " + text(score)

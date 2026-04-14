# ============================================================
# moo Boulder Dash — Diamanten sammeln, Steinen ausweichen
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/boulder_dash.moo -o beispiele/boulder_dash
#   ./beispiele/boulder_dash
#
# Bedienung:
#   WASD oder Pfeiltasten - Bewegen (graebt Erde)
#   R - Level neu
#   Escape - Beenden
#
# Features:
#   * 40x25 Grid mit Erde, Steinen, Diamanten
#   * Steine fallen mit Schwerkraft
#   * Steine koennen dich erschlagen!
#   * Diamanten sammeln, Ausgang oeffnet nach N Diamanten
#   * Steine koennen seitlich rollen
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600
setze TILE auf 20
setze GRID_W auf 40
setze GRID_H auf 30
setze OFFSET_X auf 0
setze OFFSET_Y auf 20

# Tiles: 0=leer, 1=erde, 2=wand, 3=stein, 4=diamant, 5=ausgang, 6=spieler
setze karte auf []
setze gi auf 0
solange gi < GRID_W * GRID_H:
    karte.hinzufügen(0)
    setze gi auf gi + 1

setze rng_state auf 33333
funktion rng():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    wenn rng_state < 0:
        setze rng_state auf 0 - rng_state
    gib_zurück rng_state

funktion karte_get(x, y):
    wenn x < 0 oder x >= GRID_W oder y < 0 oder y >= GRID_H:
        gib_zurück 2
    gib_zurück karte[y * GRID_W + x]

funktion karte_set(x, y, val):
    wenn x >= 0 und x < GRID_W und y >= 0 und y < GRID_H:
        karte[y * GRID_W + x] = val

# Level generieren
setze level auf 1
setze spieler_x auf 2
setze spieler_y auf 2
setze diamanten_count auf 0
setze ziel_diamanten auf 15
setze ausgang_offen auf falsch
setze score auf 0
setze leben auf 3

funktion level_gen(lvl):
    setze gi auf 0
    solange gi < GRID_W * GRID_H:
        karte[gi] = 1
        setze gi auf gi + 1
    # Rand = Wand
    setze x auf 0
    solange x < GRID_W:
        karte_set(x, 0, 2)
        karte_set(x, GRID_H - 1, 2)
        setze x auf x + 1
    setze y auf 0
    solange y < GRID_H:
        karte_set(0, y, 2)
        karte_set(GRID_W - 1, y, 2)
        setze y auf y + 1
    # Steine zufaellig
    setze si auf 0
    setze anzahl_steine auf 60 + lvl * 10
    solange si < anzahl_steine:
        setze sx auf 2 + rng() % (GRID_W - 4)
        setze sy auf 2 + rng() % (GRID_H - 4)
        karte_set(sx, sy, 3)
        setze si auf si + 1
    # Diamanten
    setze di auf 0
    setze anzahl_diamanten auf 20 + lvl * 3
    solange di < anzahl_diamanten:
        setze dx auf 2 + rng() % (GRID_W - 4)
        setze dy auf 2 + rng() % (GRID_H - 4)
        karte_set(dx, dy, 4)
        setze di auf di + 1
    # Spieler oben links
    setze spieler_x auf 2
    setze spieler_y auf 2
    karte_set(spieler_x, spieler_y, 0)
    # Ausgang unten rechts
    karte_set(GRID_W - 3, GRID_H - 3, 5)
    setze diamanten_count auf 0
    setze ausgang_offen auf falsch
    setze ziel_diamanten auf 15 + lvl * 2

level_gen(level)

setze move_cd auf 0
setze physik_timer auf 0

# Physik: Steine fallen, rollen seitlich
funktion physik_update():
    setze gy auf GRID_H - 2
    solange gy >= 1:
        setze gx auf 1
        solange gx < GRID_W - 1:
            setze tile auf karte_get(gx, gy)
            wenn tile == 3 oder tile == 4:
                setze unten auf karte_get(gx, gy + 1)
                wenn unten == 0:
                    # Faellt runter
                    karte_set(gx, gy, 0)
                    karte_set(gx, gy + 1, tile)
                sonst:
                    wenn unten == 3 oder unten == 4:
                        # Rollt seitlich (wenn Platz)
                        wenn karte_get(gx - 1, gy) == 0 und karte_get(gx - 1, gy + 1) == 0:
                            karte_set(gx, gy, 0)
                            karte_set(gx - 1, gy, tile)
                        sonst:
                            wenn karte_get(gx + 1, gy) == 0 und karte_get(gx + 1, gy + 1) == 0:
                                karte_set(gx, gy, 0)
                                karte_set(gx + 1, gy, tile)
            setze gx auf gx + 1
        setze gy auf gy - 1

# Stein kann Spieler treffen
funktion stein_trifft_spieler():
    # Stein genau ueber Spieler und fallend?
    setze ober auf karte_get(spieler_x, spieler_y - 1)
    wenn ober == 3:
        setze ober2 auf karte_get(spieler_x, spieler_y - 2)
        wenn ober2 == 3:
            # Simuliere: waere der stein "gefallen"? Hier simpel
            gib_zurück wahr
    gib_zurück falsch

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Boulder Dash", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn leben <= 0:
        stopp
    wenn level > 5:
        stopp

    wenn taste_gedrückt("r"):
        level_gen(level)

    # === INPUT ===
    wenn move_cd > 0:
        setze move_cd auf move_cd - 1
    sonst:
        setze dx auf 0
        setze dy auf 0
        wenn taste_gedrückt("w") oder taste_gedrückt("hoch"):
            setze dy auf -1
        wenn taste_gedrückt("s") oder taste_gedrückt("runter"):
            setze dy auf 1
        wenn taste_gedrückt("a") oder taste_gedrückt("links"):
            setze dx auf -1
        wenn taste_gedrückt("d") oder taste_gedrückt("rechts"):
            setze dx auf 1

        wenn dx != 0 oder dy != 0:
            setze nx auf spieler_x + dx
            setze ny auf spieler_y + dy
            setze tile auf karte_get(nx, ny)
            wenn tile == 0 oder tile == 1 oder tile == 4:
                # Leer, Erde, Diamant → bewegen
                wenn tile == 4:
                    setze diamanten_count auf diamanten_count + 1
                    setze score auf score + 100
                    wenn diamanten_count >= ziel_diamanten:
                        setze ausgang_offen auf wahr
                karte_set(spieler_x, spieler_y, 0)
                setze spieler_x auf nx
                setze spieler_y auf ny
                setze move_cd auf 5
            wenn tile == 5 und ausgang_offen:
                # Ausgang erreicht
                setze level auf level + 1
                setze score auf score + 500
                wenn level <= 5:
                    level_gen(level)
            wenn tile == 3:
                # Stein schieben (nur horizontal)
                wenn dx != 0 und dy == 0:
                    setze nx2 auf nx + dx
                    wenn karte_get(nx2, ny) == 0:
                        karte_set(nx2, ny, 3)
                        karte_set(nx, ny, 0)
                        karte_set(spieler_x, spieler_y, 0)
                        setze spieler_x auf nx
                        setze spieler_y auf ny
                        setze move_cd auf 8

    # === PHYSIK ===
    setze physik_timer auf physik_timer + 1
    wenn physik_timer >= 5:
        setze physik_timer auf 0
        physik_update()
        # Stein trifft Spieler?
        setze ober auf karte_get(spieler_x, spieler_y - 1)
        wenn ober == 3:
            # Wenn ober2 leer → Stein waere vorher gefallen
            setze ober2 auf karte_get(spieler_x, spieler_y - 2)
            wenn ober2 == 0 oder ober2 == 3:
                setze leben auf leben - 1
                level_gen(level)

    # === ZEICHNEN ===
    fenster_löschen(win, "#1A1A2E")

    # Karte
    setze gy auf 0
    solange gy < GRID_H:
        setze gx auf 0
        solange gx < GRID_W:
            setze dx auf OFFSET_X + gx * TILE
            setze dy auf OFFSET_Y + gy * TILE
            setze tile auf karte_get(gx, gy)
            wenn tile == 1:
                zeichne_rechteck(win, dx, dy, TILE, TILE, "#5D4037")
                zeichne_rechteck(win, dx + 2, dy + 2, TILE - 4, TILE - 4, "#6D4C41")
                zeichne_pixel(win, dx + 4, dy + 4, "#4E342E")
                zeichne_pixel(win, dx + 14, dy + 8, "#4E342E")
                zeichne_pixel(win, dx + 8, dy + 14, "#4E342E")
            wenn tile == 2:
                zeichne_rechteck(win, dx, dy, TILE, TILE, "#424242")
                zeichne_rechteck(win, dx + 1, dy + 1, TILE - 2, TILE - 2, "#616161")
                zeichne_linie(win, dx + 10, dy, dx + 10, dy + TILE, "#333333")
                zeichne_linie(win, dx, dy + 10, dx + TILE, dy + 10, "#333333")
            wenn tile == 3:
                zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 9, "#757575")
                zeichne_kreis(win, dx + TILE / 2 - 2, dy + TILE / 2 - 2, 4, "#9E9E9E")
            wenn tile == 4:
                zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 8, "#26C6DA")
                zeichne_kreis(win, dx + TILE / 2 - 2, dy + TILE / 2 - 3, 3, "#80DEEA")
                zeichne_pixel(win, dx + TILE / 2 + 3, dy + TILE / 2 + 3, "#FFFFFF")
            wenn tile == 5:
                wenn ausgang_offen:
                    zeichne_rechteck(win, dx + 2, dy + 2, TILE - 4, TILE - 4, "#4CAF50")
                    zeichne_rechteck(win, dx + 5, dy + 5, TILE - 10, TILE - 10, "#66BB6A")
                sonst:
                    zeichne_rechteck(win, dx + 2, dy + 2, TILE - 4, TILE - 4, "#795548")
                    zeichne_rechteck(win, dx + 5, dy + 5, TILE - 10, TILE - 10, "#8D6E63")
            setze gx auf gx + 1
        setze gy auf gy + 1

    # Spieler
    setze pdx auf OFFSET_X + spieler_x * TILE
    setze pdy auf OFFSET_Y + spieler_y * TILE
    zeichne_rechteck(win, pdx + 3, pdy + 3, TILE - 6, TILE - 6, "#FFD700")
    zeichne_rechteck(win, pdx + 5, pdy + 5, TILE - 10, TILE - 10, "#FFC107")
    zeichne_pixel(win, pdx + 7, pdy + 7, "#000000")
    zeichne_pixel(win, pdx + 12, pdy + 7, "#000000")

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 20, "#0D1B2A")
    # Diamanten Fortschritt
    setze bar_w auf diamanten_count * 200 / ziel_diamanten
    wenn bar_w > 200:
        setze bar_w auf 200
    zeichne_rechteck(win, 10, 5, 200, 10, "#333333")
    zeichne_rechteck(win, 10, 5, bar_w, 10, "#26C6DA")
    # Leben
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, BREITE - 20 - li * 15, 10, 5, "#F44336")
        setze li auf li + 1
    # Level
    setze lvi auf 0
    solange lvi < level und lvi < 5:
        zeichne_rechteck(win, BREITE / 2 - 30 + lvi * 10, 7, 8, 8, "#FF9800")
        setze lvi auf lvi + 1
    # Score
    setze si auf 0
    solange si < score / 100 und si < 30:
        zeichne_pixel(win, 230 + si * 4, 10, "#FFD700")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn level > 5:
    zeige "ALLE LEVEL GESCHAFFT! Score: " + text(score)
sonst:
    zeige "GAME OVER! Score: " + text(score) + " | Level: " + text(level)

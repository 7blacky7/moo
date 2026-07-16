# ============================================================
# moo Bomberman — 2-Spieler Arena
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/bomberman.moo -o beispiele/bomberman
#   ./beispiele/bomberman
#
# Spieler 1 (BLAU):  WASD=Bewegen, F=Bombe legen
# Spieler 2 (ROT):   Pfeiltasten=Bewegen, K=Bombe legen
# Escape = Beenden
#
# Features:
#   * 13x11 Arena mit zerstoerbaren Waenden
#   * 2-Spieler auf einer Tastatur
#   * Bomben mit Timer + Kreuz-Explosion
#   * Power-Ups: Reichweite, Extra-Bombe, Speed
#   * Kettenexplosionen
# ============================================================

setze BREITE auf 650
setze HOEHE auf 550
setze TILE auf 48
setze GRID_W auf 13
setze GRID_H auf 11
setze OFFSET_X auf 7
setze OFFSET_Y auf 7

# Karten-Typen: 0=leer, 1=feste Wand, 2=zerstoerbar, 3=Explosion
setze karte auf []

# Arena initialisieren
funktion arena_bauen():
    setze idx auf 0
    solange idx < GRID_W * GRID_H:
        karte.hinzufügen(0)
        setze idx auf idx + 1
    # Feste Waende (Schachbrett-Muster)
    setze gy auf 0
    solange gy < GRID_H:
        setze gx auf 0
        solange gx < GRID_W:
            # Rand
            wenn gx == 0 oder gx == GRID_W - 1 oder gy == 0 oder gy == GRID_H - 1:
                karte[gy * GRID_W + gx] = 1
            # Innere Saeulen (gerade x UND y)
            wenn gx % 2 == 0 und gy % 2 == 0 und gx > 0 und gx < GRID_W - 1 und gy > 0 und gy < GRID_H - 1:
                karte[gy * GRID_W + gx] = 1
            setze gx auf gx + 1
        setze gy auf gy + 1
    # Zerstoerbare Waende (zufaellig, aber Ecken freilassen)
    setze rng auf 42
    setze gy auf 1
    solange gy < GRID_H - 1:
        setze gx auf 1
        solange gx < GRID_W - 1:
            wenn karte[gy * GRID_W + gx] == 0:
                # Ecken fuer Spieler freilassen
                setze ist_ecke auf falsch
                wenn (gx <= 2 und gy <= 2):
                    setze ist_ecke auf wahr
                wenn (gx >= GRID_W - 3 und gy >= GRID_H - 3):
                    setze ist_ecke auf wahr
                wenn nicht ist_ecke:
                    setze rng auf (rng * 1103515245 + 12345) % 2147483647
                    wenn rng % 100 < 65:
                        karte[gy * GRID_W + gx] = 2
            setze gx auf gx + 1
        setze gy auf gy + 1

arena_bauen()

# === SPIELER ===
setze p1_x auf 1.0
setze p1_y auf 1.0
setze p1_speed auf 0.06
setze p1_bomben_max auf 1
setze p1_bomben_aktiv auf 0
setze p1_reichweite auf 2
setze p1_lebt auf wahr

setze p2_x auf (GRID_W - 2) * 1.0
setze p2_y auf (GRID_H - 2) * 1.0
setze p2_speed auf 0.06
setze p2_bomben_max auf 1
setze p2_bomben_aktiv auf 0
setze p2_reichweite auf 2
setze p2_lebt auf wahr

# === BOMBEN ===
setze MAX_BOMBEN auf 20
setze bom_x auf []
setze bom_y auf []
setze bom_timer auf []
setze bom_reichweite auf []
setze bom_besitzer auf []
setze bom_aktiv auf []
setze bi auf 0
solange bi < MAX_BOMBEN:
    bom_x.hinzufügen(0)
    bom_y.hinzufügen(0)
    bom_timer.hinzufügen(0)
    bom_reichweite.hinzufügen(0)
    bom_besitzer.hinzufügen(0)
    bom_aktiv.hinzufügen(falsch)
    setze bi auf bi + 1

# === EXPLOSIONEN ===
setze MAX_EXPL auf 100
setze expl_x auf []
setze expl_y auf []
setze expl_timer auf []
setze expl_aktiv auf []
setze ei auf 0
solange ei < MAX_EXPL:
    expl_x.hinzufügen(0)
    expl_y.hinzufügen(0)
    expl_timer.hinzufügen(0)
    expl_aktiv.hinzufügen(falsch)
    setze ei auf ei + 1

# === POWER-UPS ===
setze MAX_POWER auf 20
setze pow_x auf []
setze pow_y auf []
setze pow_typ auf []
setze pow_aktiv auf []
setze pi auf 0
solange pi < MAX_POWER:
    pow_x.hinzufügen(0)
    pow_y.hinzufügen(0)
    pow_typ.hinzufügen(0)
    pow_aktiv.hinzufügen(falsch)
    setze pi auf pi + 1

setze rng_state auf 7777
funktion rng_next():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    gib_zurück rng_state

setze cooldown_p1 auf 0
setze cooldown_p2 auf 0

# === HILFSFUNKTIONEN ===
funktion tile_frei(gx, gy):
    wenn gx < 0 oder gx >= GRID_W oder gy < 0 oder gy >= GRID_H:
        gib_zurück falsch
    setze val auf karte[gy * GRID_W + gx]
    gib_zurück val == 0

funktion bombe_legen(gx, gy, reichweite, besitzer):
    setze bi auf 0
    solange bi < MAX_BOMBEN:
        wenn nicht bom_aktiv[bi]:
            bom_x[bi] = gx
            bom_y[bi] = gy
            bom_timer[bi] = 120
            bom_reichweite[bi] = reichweite
            bom_besitzer[bi] = besitzer
            bom_aktiv[bi] = wahr
            gib_zurück wahr
        setze bi auf bi + 1
    gib_zurück falsch

funktion explosion_spawnen(gx, gy):
    setze ei auf 0
    solange ei < MAX_EXPL:
        wenn nicht expl_aktiv[ei]:
            expl_x[ei] = gx
            expl_y[ei] = gy
            expl_timer[ei] = 20
            expl_aktiv[ei] = wahr
            gib_zurück nichts
        setze ei auf ei + 1

funktion powerup_spawnen(gx, gy):
    setze r auf rng_next() % 100
    wenn r < 30:
        setze pi auf 0
        solange pi < MAX_POWER:
            wenn nicht pow_aktiv[pi]:
                pow_x[pi] = gx
                pow_y[pi] = gy
                pow_typ[pi] = rng_next() % 3
                pow_aktiv[pi] = wahr
                gib_zurück nichts
            setze pi auf pi + 1

funktion explodiere(gx, gy, reichweite):
    explosion_spawnen(gx, gy)
    # 4 Richtungen
    setze dirs_x auf [1, -1, 0, 0]
    setze dirs_y auf [0, 0, 1, -1]
    setze di auf 0
    solange di < 4:
        setze dist auf 1
        solange dist <= reichweite:
            setze nx auf gx + dirs_x[di] * dist
            setze ny auf gy + dirs_y[di] * dist
            wenn nx < 0 oder nx >= GRID_W oder ny < 0 oder ny >= GRID_H:
                setze dist auf reichweite + 1
            sonst:
                setze val auf karte[ny * GRID_W + nx]
                wenn val == 1:
                    setze dist auf reichweite + 1
                sonst:
                    explosion_spawnen(nx, ny)
                    wenn val == 2:
                        karte[ny * GRID_W + nx] = 0
                        powerup_spawnen(nx, ny)
                        setze dist auf reichweite + 1
                    sonst:
                        setze dist auf dist + 1
            setze dist auf dist
        setze di auf di + 1

# Spieler-Bewegung mit Kollision
funktion bewege_spieler(px, py, dx, dy, speed):
    setze nx auf px + dx * speed
    setze ny auf py + dy * speed
    setze gx auf boden(nx + 0.5)
    setze gy auf boden(ny + 0.5)
    wenn tile_frei(gx, gy):
        gib_zurück nx
    gib_zurück px

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Bomberman", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn nicht p1_lebt und nicht p2_lebt:
        stopp

    # === INPUT P1 ===
    wenn p1_lebt:
        wenn taste_gedrückt("w"):
            setze ny auf p1_y - p1_speed
            wenn tile_frei(boden(p1_x + 0.5), boden(ny + 0.3)):
                setze p1_y auf ny
        wenn taste_gedrückt("s"):
            setze ny auf p1_y + p1_speed
            wenn tile_frei(boden(p1_x + 0.5), boden(ny + 0.7)):
                setze p1_y auf ny
        wenn taste_gedrückt("a"):
            setze nx auf p1_x - p1_speed
            wenn tile_frei(boden(nx + 0.3), boden(p1_y + 0.5)):
                setze p1_x auf nx
        wenn taste_gedrückt("d"):
            setze nx auf p1_x + p1_speed
            wenn tile_frei(boden(nx + 0.7), boden(p1_y + 0.5)):
                setze p1_x auf nx
        wenn taste_gedrückt("f") und cooldown_p1 == 0:
            wenn p1_bomben_aktiv < p1_bomben_max:
                setze bgx auf boden(p1_x + 0.5)
                setze bgy auf boden(p1_y + 0.5)
                wenn bombe_legen(bgx, bgy, p1_reichweite, 1):
                    setze p1_bomben_aktiv auf p1_bomben_aktiv + 1
                    setze cooldown_p1 auf 15

    # === INPUT P2 ===
    wenn p2_lebt:
        wenn taste_gedrückt("hoch"):
            setze ny auf p2_y - p2_speed
            wenn tile_frei(boden(p2_x + 0.5), boden(ny + 0.3)):
                setze p2_y auf ny
        wenn taste_gedrückt("runter"):
            setze ny auf p2_y + p2_speed
            wenn tile_frei(boden(p2_x + 0.5), boden(ny + 0.7)):
                setze p2_y auf ny
        wenn taste_gedrückt("links"):
            setze nx auf p2_x - p2_speed
            wenn tile_frei(boden(nx + 0.3), boden(p2_y + 0.5)):
                setze p2_x auf nx
        wenn taste_gedrückt("rechts"):
            setze nx auf p2_x + p2_speed
            wenn tile_frei(boden(nx + 0.7), boden(p2_y + 0.5)):
                setze p2_x auf nx
        wenn taste_gedrückt("k") und cooldown_p2 == 0:
            wenn p2_bomben_aktiv < p2_bomben_max:
                setze bgx auf boden(p2_x + 0.5)
                setze bgy auf boden(p2_y + 0.5)
                wenn bombe_legen(bgx, bgy, p2_reichweite, 2):
                    setze p2_bomben_aktiv auf p2_bomben_aktiv + 1
                    setze cooldown_p2 auf 15

    wenn cooldown_p1 > 0:
        setze cooldown_p1 auf cooldown_p1 - 1
    wenn cooldown_p2 > 0:
        setze cooldown_p2 auf cooldown_p2 - 1

    # === BOMBEN UPDATE ===
    setze bi auf 0
    solange bi < MAX_BOMBEN:
        wenn bom_aktiv[bi]:
            bom_timer[bi] = bom_timer[bi] - 1
            wenn bom_timer[bi] <= 0:
                explodiere(bom_x[bi], bom_y[bi], bom_reichweite[bi])
                bom_aktiv[bi] = falsch
                wenn bom_besitzer[bi] == 1:
                    setze p1_bomben_aktiv auf p1_bomben_aktiv - 1
                sonst:
                    setze p2_bomben_aktiv auf p2_bomben_aktiv - 1
        setze bi auf bi + 1

    # === EXPLOSIONEN UPDATE ===
    setze ei auf 0
    solange ei < MAX_EXPL:
        wenn expl_aktiv[ei]:
            expl_timer[ei] = expl_timer[ei] - 1
            wenn expl_timer[ei] <= 0:
                expl_aktiv[ei] = falsch
            # Spieler treffen?
            wenn p1_lebt:
                wenn boden(p1_x + 0.5) == expl_x[ei] und boden(p1_y + 0.5) == expl_y[ei]:
                    setze p1_lebt auf falsch
            wenn p2_lebt:
                wenn boden(p2_x + 0.5) == expl_x[ei] und boden(p2_y + 0.5) == expl_y[ei]:
                    setze p2_lebt auf falsch
            # Ketten-Explosion (andere Bomben)
            setze bi auf 0
            solange bi < MAX_BOMBEN:
                wenn bom_aktiv[bi]:
                    wenn bom_x[bi] == expl_x[ei] und bom_y[bi] == expl_y[ei]:
                        bom_timer[bi] = 1
                setze bi auf bi + 1
        setze ei auf ei + 1

    # === POWER-UPS EINSAMMELN ===
    setze pi auf 0
    solange pi < MAX_POWER:
        wenn pow_aktiv[pi]:
            # P1
            wenn p1_lebt und boden(p1_x + 0.5) == pow_x[pi] und boden(p1_y + 0.5) == pow_y[pi]:
                wenn pow_typ[pi] == 0:
                    setze p1_reichweite auf p1_reichweite + 1
                wenn pow_typ[pi] == 1:
                    setze p1_bomben_max auf p1_bomben_max + 1
                wenn pow_typ[pi] == 2:
                    setze p1_speed auf p1_speed + 0.01
                pow_aktiv[pi] = falsch
            # P2
            wenn p2_lebt und boden(p2_x + 0.5) == pow_x[pi] und boden(p2_y + 0.5) == pow_y[pi]:
                wenn pow_typ[pi] == 0:
                    setze p2_reichweite auf p2_reichweite + 1
                wenn pow_typ[pi] == 1:
                    setze p2_bomben_max auf p2_bomben_max + 1
                wenn pow_typ[pi] == 2:
                    setze p2_speed auf p2_speed + 0.01
                pow_aktiv[pi] = falsch
        setze pi auf pi + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#1B5E20")

    # Grid
    setze gy auf 0
    solange gy < GRID_H:
        setze gx auf 0
        solange gx < GRID_W:
            setze dx auf OFFSET_X + gx * TILE
            setze dy auf OFFSET_Y + gy * TILE
            setze val auf karte[gy * GRID_W + gx]
            wenn val == 0:
                zeichne_rechteck(win, dx, dy, TILE, TILE, "#4CAF50")
                zeichne_rechteck(win, dx + 1, dy + 1, TILE - 2, TILE - 2, "#66BB6A")
            wenn val == 1:
                zeichne_rechteck(win, dx, dy, TILE, TILE, "#424242")
                zeichne_rechteck(win, dx + 2, dy + 2, TILE - 4, TILE - 4, "#616161")
            wenn val == 2:
                zeichne_rechteck(win, dx, dy, TILE, TILE, "#795548")
                zeichne_rechteck(win, dx + 3, dy + 3, TILE - 6, TILE - 6, "#8D6E63")
                zeichne_linie(win, dx + 5, dy + 5, dx + TILE - 5, dy + TILE - 5, "#6D4C41")
            setze gx auf gx + 1
        setze gy auf gy + 1

    # Power-Ups
    setze pi auf 0
    solange pi < MAX_POWER:
        wenn pow_aktiv[pi]:
            setze dx auf OFFSET_X + pow_x[pi] * TILE + TILE / 2
            setze dy auf OFFSET_Y + pow_y[pi] * TILE + TILE / 2
            wenn pow_typ[pi] == 0:
                zeichne_kreis(win, dx, dy, 10, "#F44336")
            wenn pow_typ[pi] == 1:
                zeichne_kreis(win, dx, dy, 10, "#FF9800")
            wenn pow_typ[pi] == 2:
                zeichne_kreis(win, dx, dy, 10, "#2196F3")
        setze pi auf pi + 1

    # Bomben
    setze bi auf 0
    solange bi < MAX_BOMBEN:
        wenn bom_aktiv[bi]:
            setze dx auf OFFSET_X + bom_x[bi] * TILE + TILE / 2
            setze dy auf OFFSET_Y + bom_y[bi] * TILE + TILE / 2
            setze pulse auf 12 + (bom_timer[bi] % 10)
            zeichne_kreis(win, dx, dy, pulse, "#212121")
            zeichne_kreis(win, dx - 3, dy - 4, 4, "#FF5722")
        setze bi auf bi + 1

    # Explosionen
    setze ei auf 0
    solange ei < MAX_EXPL:
        wenn expl_aktiv[ei]:
            setze dx auf OFFSET_X + expl_x[ei] * TILE
            setze dy auf OFFSET_Y + expl_y[ei] * TILE
            wenn expl_timer[ei] > 10:
                zeichne_rechteck(win, dx + 4, dy + 4, TILE - 8, TILE - 8, "#FF5722")
            sonst:
                zeichne_rechteck(win, dx + 8, dy + 8, TILE - 16, TILE - 16, "#FF9800")
            zeichne_rechteck(win, dx + 12, dy + 12, TILE - 24, TILE - 24, "#FFEB3B")
        setze ei auf ei + 1

    # Spieler
    wenn p1_lebt:
        setze dx auf OFFSET_X + p1_x * TILE + TILE / 2
        setze dy auf OFFSET_Y + p1_y * TILE + TILE / 2
        zeichne_kreis(win, dx, dy, 16, "#1565C0")
        zeichne_kreis(win, dx, dy, 10, "#42A5F5")
        zeichne_kreis(win, dx - 4, dy - 4, 3, "#FFFFFF")

    wenn p2_lebt:
        setze dx auf OFFSET_X + p2_x * TILE + TILE / 2
        setze dy auf OFFSET_Y + p2_y * TILE + TILE / 2
        zeichne_kreis(win, dx, dy, 16, "#B71C1C")
        zeichne_kreis(win, dx, dy, 10, "#EF5350")
        zeichne_kreis(win, dx - 4, dy - 4, 3, "#FFFFFF")

    # Game Over
    wenn nicht p1_lebt oder nicht p2_lebt:
        wenn nicht p1_lebt und p2_lebt:
            zeichne_kreis(win, BREITE / 2, 20, 12, "#EF5350")
        wenn p1_lebt und nicht p2_lebt:
            zeichne_kreis(win, BREITE / 2, 20, 12, "#42A5F5")
        wenn nicht p1_lebt und nicht p2_lebt:
            zeichne_kreis(win, BREITE / 2 - 15, 20, 10, "#42A5F5")
            zeichne_kreis(win, BREITE / 2 + 15, 20, 10, "#EF5350")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn p1_lebt und nicht p2_lebt:
    zeige "SPIELER 1 (BLAU) GEWINNT!"
wenn p2_lebt und nicht p1_lebt:
    zeige "SPIELER 2 (ROT) GEWINNT!"
wenn nicht p1_lebt und nicht p2_lebt:
    zeige "UNENTSCHIEDEN!"

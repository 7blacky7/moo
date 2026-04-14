# ============================================================
# moo Block Dude — TI-83 Puzzle-Klassiker
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/block_dude.moo -o beispiele/block_dude
#   ./beispiele/block_dude
#
# Bedienung:
#   A/D - Links/Rechts
#   W - Bloecke aufheben/ablegen
#   Leertaste - Klettern (auf 1 Tile hohe Kante)
#   R - Neustart
#   Escape - Beenden
#
# Ziel: Tuer erreichen durch Bloecke schieben/stapeln
# 5 Level mit steigender Schwierigkeit
# ============================================================

setze BREITE auf 640
setze HOEHE auf 480
setze TILE auf 32
setze GRID_W auf 20
setze GRID_H auf 15

# Tile-Typen: 0=leer, 1=wand, 2=block (bewegbar), 3=tuer
setze karte auf []
setze gi auf 0
solange gi < GRID_W * GRID_H:
    karte.hinzufügen(0)
    setze gi auf gi + 1

funktion karte_get(x, y):
    wenn x < 0 oder x >= GRID_W oder y < 0 oder y >= GRID_H:
        gib_zurück 1
    gib_zurück karte[y * GRID_W + x]

funktion karte_set(x, y, val):
    wenn x >= 0 und x < GRID_W und y >= 0 und y < GRID_H:
        karte[y * GRID_W + x] = val

# Level-Daten
setze level auf 1
setze spieler_x auf 1
setze spieler_y auf 13
setze spieler_dx auf 1
setze traegt_block auf falsch
setze ziel_x auf 18
setze ziel_y auf 13
setze moves auf 0

funktion level_laden(lvl):
    # Karte reset
    setze gi auf 0
    solange gi < GRID_W * GRID_H:
        karte[gi] = 0
        setze gi auf gi + 1
    # Boden
    setze gx auf 0
    solange gx < GRID_W:
        karte_set(gx, GRID_H - 1, 1)
        setze gx auf gx + 1
    # Raender
    setze gy auf 0
    solange gy < GRID_H:
        karte_set(0, gy, 1)
        karte_set(GRID_W - 1, gy, 1)
        setze gy auf gy + 1
    # Decke
    setze gx auf 0
    solange gx < GRID_W:
        karte_set(gx, 0, 1)
        setze gx auf gx + 1

    setze traegt_block auf falsch

    wenn lvl == 1:
        # Einfach: ein Block und eine Stufe
        karte_set(5, GRID_H - 2, 1)
        karte_set(8, GRID_H - 2, 2)
        karte_set(12, GRID_H - 2, 1)
        karte_set(12, GRID_H - 3, 1)
        setze spieler_x auf 2
        setze spieler_y auf GRID_H - 2
        setze ziel_x auf 16
        setze ziel_y auf GRID_H - 2
    wenn lvl == 2:
        # 2 Bloecke, 2 Stufen
        karte_set(4, GRID_H - 2, 2)
        karte_set(7, GRID_H - 2, 1)
        karte_set(7, GRID_H - 3, 1)
        karte_set(10, GRID_H - 2, 2)
        karte_set(13, GRID_H - 2, 1)
        karte_set(13, GRID_H - 3, 1)
        karte_set(13, GRID_H - 4, 1)
        setze spieler_x auf 2
        setze spieler_y auf GRID_H - 2
        setze ziel_x auf 17
        setze ziel_y auf GRID_H - 2
    wenn lvl == 3:
        # Grube
        karte_set(3, GRID_H - 2, 2)
        karte_set(6, GRID_H - 2, 1)
        karte_set(6, GRID_H - 3, 1)
        # Grube
        karte_set(9, GRID_H - 1, 0)
        karte_set(10, GRID_H - 1, 0)
        karte_set(9, GRID_H, 1)
        karte_set(10, GRID_H, 1)
        karte_set(12, GRID_H - 2, 2)
        karte_set(15, GRID_H - 2, 1)
        karte_set(15, GRID_H - 3, 1)
        setze spieler_x auf 2
        setze spieler_y auf GRID_H - 2
        setze ziel_x auf 17
        setze ziel_y auf GRID_H - 2
    wenn lvl == 4:
        # Mehrstoeckig
        karte_set(4, GRID_H - 2, 2)
        karte_set(5, GRID_H - 2, 2)
        karte_set(8, GRID_H - 2, 1)
        karte_set(8, GRID_H - 3, 1)
        karte_set(8, GRID_H - 4, 1)
        karte_set(9, GRID_H - 4, 1)
        karte_set(10, GRID_H - 4, 1)
        karte_set(11, GRID_H - 2, 2)
        karte_set(14, GRID_H - 2, 1)
        karte_set(14, GRID_H - 3, 1)
        setze spieler_x auf 2
        setze spieler_y auf GRID_H - 2
        setze ziel_x auf 17
        setze ziel_y auf GRID_H - 2
    wenn lvl >= 5:
        # Maximum
        karte_set(3, GRID_H - 2, 2)
        karte_set(4, GRID_H - 2, 2)
        karte_set(6, GRID_H - 1, 0)
        karte_set(6, GRID_H, 1)
        karte_set(8, GRID_H - 2, 1)
        karte_set(8, GRID_H - 3, 1)
        karte_set(8, GRID_H - 4, 1)
        karte_set(8, GRID_H - 5, 1)
        karte_set(9, GRID_H - 5, 1)
        karte_set(10, GRID_H - 5, 1)
        karte_set(11, GRID_H - 2, 2)
        karte_set(12, GRID_H - 2, 2)
        karte_set(14, GRID_H - 2, 1)
        karte_set(14, GRID_H - 3, 1)
        karte_set(14, GRID_H - 4, 1)
        setze spieler_x auf 2
        setze spieler_y auf GRID_H - 2
        setze ziel_x auf 17
        setze ziel_y auf GRID_H - 2
    # Tuer
    karte_set(ziel_x, ziel_y, 3)

level_laden(level)
setze input_cd auf 0

# Schwerkraft
funktion fallen():
    # Alle Blöcke fallen
    setze changed auf wahr
    solange changed:
        setze changed auf falsch
        setze gy auf GRID_H - 2
        solange gy >= 1:
            setze gx auf 1
            solange gx < GRID_W - 1:
                wenn karte_get(gx, gy) == 2:
                    wenn karte_get(gx, gy + 1) == 0:
                        karte_set(gx, gy, 0)
                        karte_set(gx, gy + 1, 2)
                        setze changed auf wahr
                setze gx auf gx + 1
            setze gy auf gy - 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Block Dude", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_gedrückt("r"):
        level_laden(level)
        setze moves auf 0

    wenn input_cd > 0:
        setze input_cd auf input_cd - 1
    sonst:
        setze action auf falsch

        # Links/Rechts
        wenn taste_gedrückt("a"):
            setze spieler_dx auf -1
            setze nx auf spieler_x - 1
            setze tile auf karte_get(nx, spieler_y)
            wenn tile == 0 oder tile == 3:
                setze spieler_x auf nx
                setze action auf wahr
            sonst:
                wenn tile == 2 und nicht traegt_block:
                    # Block schieben
                    setze n2 auf karte_get(nx - 1, spieler_y)
                    wenn n2 == 0:
                        karte_set(nx - 1, spieler_y, 2)
                        karte_set(nx, spieler_y, 0)
                        setze spieler_x auf nx
                        setze action auf wahr
        wenn taste_gedrückt("d"):
            setze spieler_dx auf 1
            setze nx auf spieler_x + 1
            setze tile auf karte_get(nx, spieler_y)
            wenn tile == 0 oder tile == 3:
                setze spieler_x auf nx
                setze action auf wahr
            sonst:
                wenn tile == 2 und nicht traegt_block:
                    setze n2 auf karte_get(nx + 1, spieler_y)
                    wenn n2 == 0:
                        karte_set(nx + 1, spieler_y, 2)
                        karte_set(nx, spieler_y, 0)
                        setze spieler_x auf nx
                        setze action auf wahr

        # Klettern (Leertaste)
        wenn taste_gedrückt("leertaste"):
            setze tx auf spieler_x + spieler_dx
            setze tile auf karte_get(tx, spieler_y)
            wenn tile == 1 oder tile == 2:
                # Pruefe ob oben frei
                wenn karte_get(tx, spieler_y - 1) == 0 und karte_get(spieler_x, spieler_y - 1) == 0:
                    # Mit getragenem Block?
                    wenn traegt_block und karte_get(spieler_x, spieler_y - 2) != 0:
                        # Blockiert
                        setze action auf action
                    sonst:
                        setze spieler_x auf tx
                        setze spieler_y auf spieler_y - 1
                        setze action auf wahr

        # W = Block aufheben/ablegen
        wenn taste_gedrückt("w"):
            setze tx auf spieler_x + spieler_dx
            wenn traegt_block:
                # Ablegen
                setze tile auf karte_get(tx, spieler_y - 1)
                wenn tile == 0:
                    karte_set(tx, spieler_y - 1, 2)
                    setze traegt_block auf falsch
                    setze action auf wahr
            sonst:
                # Aufheben (Block neben mir, auf gleicher Höhe)
                setze tile auf karte_get(tx, spieler_y)
                wenn tile == 2:
                    # Nur wenn Platz ueber mir ist
                    wenn karte_get(spieler_x, spieler_y - 1) == 0:
                        karte_set(tx, spieler_y, 0)
                        setze traegt_block auf wahr
                        setze action auf wahr

        wenn action:
            setze input_cd auf 8
            fallen()
            # Spieler auch fallen
            solange karte_get(spieler_x, spieler_y + 1) == 0:
                setze spieler_y auf spieler_y + 1
            setze moves auf moves + 1

    # Ziel erreicht?
    wenn karte_get(spieler_x, spieler_y) == 3:
        setze level auf level + 1
        wenn level > 5:
            stopp
        level_laden(level)
        setze moves auf 0

    # === ZEICHNEN ===
    fenster_löschen(win, "#1A1A2E")

    # Karte
    setze gy auf 0
    solange gy < GRID_H:
        setze gx auf 0
        solange gx < GRID_W:
            setze dx auf gx * TILE
            setze dy auf gy * TILE
            setze tile auf karte_get(gx, gy)
            wenn tile == 1:
                zeichne_rechteck(win, dx, dy, TILE, TILE, "#424242")
                zeichne_rechteck(win, dx + 2, dy + 2, TILE - 4, TILE - 4, "#616161")
                zeichne_linie(win, dx + 8, dy, dx + 8, dy + TILE, "#333333")
                zeichne_linie(win, dx, dy + 16, dx + TILE, dy + 16, "#333333")
            wenn tile == 2:
                zeichne_rechteck(win, dx + 2, dy + 2, TILE - 4, TILE - 4, "#FF9800")
                zeichne_rechteck(win, dx + 5, dy + 5, TILE - 10, TILE - 10, "#FFB74D")
                zeichne_rechteck(win, dx + 8, dy + 8, TILE - 16, TILE - 16, "#FFCC80")
            wenn tile == 3:
                zeichne_rechteck(win, dx + 4, dy + 4, TILE - 8, TILE - 8, "#795548")
                zeichne_rechteck(win, dx + 7, dy + 10, TILE - 14, TILE - 14, "#5D4037")
                zeichne_kreis(win, dx + TILE - 10, dy + TILE / 2, 2, "#FFD700")
            setze gx auf gx + 1
        setze gy auf gy + 1

    # Spieler
    setze p_dx auf spieler_x * TILE
    setze p_dy auf spieler_y * TILE
    # Koerper
    zeichne_rechteck(win, p_dx + 6, p_dy + 8, 20, 20, "#42A5F5")
    # Kopf
    zeichne_kreis(win, p_dx + 16, p_dy + 6, 6, "#FFCC80")
    # Augen
    zeichne_rechteck(win, p_dx + 12 + spieler_dx * 2, p_dy + 4, 2, 2, "#000000")
    zeichne_rechteck(win, p_dx + 18 + spieler_dx * 2, p_dy + 4, 2, 2, "#000000")
    # Beine
    zeichne_rechteck(win, p_dx + 8, p_dy + TILE - 4, 6, 4, "#1565C0")
    zeichne_rechteck(win, p_dx + 18, p_dy + TILE - 4, 6, 4, "#1565C0")
    # Getragener Block
    wenn traegt_block:
        zeichne_rechteck(win, p_dx + 6, p_dy - 20, TILE - 12, 14, "#FF9800")
        zeichne_rechteck(win, p_dx + 9, p_dy - 17, TILE - 18, 8, "#FFB74D")

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 20, "#0D1B2A")
    # Level
    setze li auf 0
    solange li < level und li < 5:
        zeichne_rechteck(win, 10 + li * 14, 6, 10, 10, "#FFD700")
        setze li auf li + 1
    # Moves
    setze mi auf 0
    solange mi < moves / 5 und mi < 20:
        zeichne_kreis(win, 100 + mi * 10, 10, 3, "#4CAF50")
        setze mi auf mi + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn level > 5:
    zeige "ALLE LEVEL GESCHAFFT!"
sonst:
    zeige "Level " + text(level) + " — Moves: " + text(moves)

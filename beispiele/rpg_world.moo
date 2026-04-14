# ============================================================
# moo RPG World — Open-World Top-Down RPG
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/rpg_world.moo -o beispiele/rpg_world
#   ./beispiele/rpg_world
#
# Bedienung:
#   WASD - Bewegen
#   Leertaste - Angriff
#   E - Item aufheben
#   Escape - Beenden
#
# Features:
#   * 200x200 Tile Welt (~1.3 km²)
#   * Prozedurale Landschaft: Wiesen, Waelder, Berge, Fluesse, Seen
#   * Wandernde NPCs/Monster
#   * Items + Inventar
#   * Kamera scrollt mit Spieler
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600
setze TILE auf 16
setze WELT_W auf 200
setze WELT_H auf 200
setze SICHT_W auf 52
setze SICHT_H auf 40

# Tile-Typen
setze GRAS auf 0
setze BAUM auf 1
setze WASSER auf 2
setze STEIN auf 3
setze WEG auf 4
setze BLUME auf 5
setze SAND auf 6
setze BUSCH auf 7

# Welt-Karte
setze welt auf []

# RNG
setze rng_state auf 424242

funktion rng():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    wenn rng_state < 0:
        setze rng_state auf 0 - rng_state
    gib_zurück rng_state

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

# Einfacher Noise (Hash-basiert, wie welten.moo)
funktion hash_2d(ix, iy):
    setze n auf ((ix + 1) * 374761 + (iy + 1) * 668265 + 42) % 2147483647
    wenn n < 0:
        setze n auf 0 - n
    setze n auf (n ^ (n / 2048)) % 2147483647
    setze n auf (n * 45673) % 2147483647
    setze n auf (n ^ (n / 32768)) % 2147483647
    gib_zurück n

funktion noise_2d(x, y, scale):
    setze ix auf boden(x * scale)
    setze iy auf boden(y * scale)
    gib_zurück (hash_2d(ix, iy) % 1000) / 1000.0

# === WELT GENERIEREN ===
funktion welt_generieren():
    setze idx auf 0
    solange idx < WELT_W * WELT_H:
        welt.hinzufügen(GRAS)
        setze idx auf idx + 1

    setze wy auf 0
    solange wy < WELT_H:
        setze wx auf 0
        solange wx < WELT_W:
            setze hoehe auf noise_2d(wx, wy, 0.03)
            setze feucht auf noise_2d(wx + 500, wy + 500, 0.05)
            setze detail auf noise_2d(wx, wy, 0.15)

            setze tile auf GRAS
            # Wasser (Seen + Fluesse)
            wenn hoehe < 0.25:
                setze tile auf WASSER
            wenn hoehe >= 0.25 und hoehe < 0.3:
                setze tile auf SAND
            # Berge/Steine
            wenn hoehe > 0.75:
                setze tile auf STEIN
            # Wald
            wenn tile == GRAS und feucht > 0.55:
                wenn detail > 0.4:
                    setze tile auf BAUM
                sonst:
                    wenn detail > 0.3:
                        setze tile auf BUSCH
            # Blumen
            wenn tile == GRAS und detail > 0.8:
                setze tile auf BLUME

            welt[wy * WELT_W + wx] = tile
            setze wx auf wx + 1
        setze wy auf wy + 1

    # Wege generieren (horizontal + vertikal Hauptwege)
    # Horizontaler Hauptweg
    setze wy auf WELT_H / 2
    setze wx auf 0
    solange wx < WELT_W:
        setze offset auf boden(noise_2d(wx, 0, 0.08) * 10) - 5
        setze road_y auf wy + offset
        wenn road_y >= 0 und road_y < WELT_H:
            welt[road_y * WELT_W + wx] = WEG
            wenn road_y + 1 < WELT_H:
                welt[(road_y + 1) * WELT_W + wx] = WEG
        setze wx auf wx + 1

    # Vertikaler Hauptweg
    setze wx auf WELT_W / 2
    setze wy auf 0
    solange wy < WELT_H:
        setze offset auf boden(noise_2d(0, wy, 0.08) * 10) - 5
        setze road_x auf wx + offset
        wenn road_x >= 0 und road_x < WELT_W:
            welt[wy * WELT_W + road_x] = WEG
            wenn road_x + 1 < WELT_W:
                welt[wy * WELT_W + road_x + 1] = WEG
        setze wy auf wy + 1

welt_generieren()

funktion welt_get(x, y):
    wenn x < 0 oder x >= WELT_W oder y < 0 oder y >= WELT_H:
        gib_zurück WASSER
    gib_zurück welt[y * WELT_W + x]

funktion ist_begehbar(x, y):
    setze tile auf welt_get(x, y)
    gib_zurück tile != WASSER und tile != BAUM und tile != STEIN und tile != BUSCH

# === SPIELER ===
setze spieler_x auf WELT_W / 2
setze spieler_y auf WELT_H / 2
setze spieler_hp auf 50
setze spieler_max_hp auf 50
setze spieler_dmg auf 8
setze spieler_dx auf 0
setze spieler_dy auf 1
setze score auf 0
setze move_cooldown auf 0
setze angriff_timer auf 0

# === MONSTER ===
setze MAX_MONSTER auf 40
setze mon_x auf []
setze mon_y auf []
setze mon_hp auf []
setze mon_max_hp auf []
setze mon_dmg auf []
setze mon_typ auf []
setze mon_aktiv auf []
setze mon_cooldown auf []

funktion monster_spawnen():
    setze placed auf 0
    setze tries auf 0
    solange placed < MAX_MONSTER und tries < 500:
        setze mx auf 10 + rng() % (WELT_W - 20)
        setze my auf 10 + rng() % (WELT_H - 20)
        wenn ist_begehbar(mx, my):
            setze dist auf abs_wert(mx - spieler_x) + abs_wert(my - spieler_y)
            wenn dist > 15:
                mon_x.hinzufügen(mx)
                mon_y.hinzufügen(my)
                setze typ auf rng() % 4
                mon_typ.hinzufügen(typ)
                mon_hp.hinzufügen(10 + typ * 8)
                mon_max_hp.hinzufügen(10 + typ * 8)
                mon_dmg.hinzufügen(3 + typ * 2)
                mon_aktiv.hinzufügen(wahr)
                mon_cooldown.hinzufügen(0)
                setze placed auf placed + 1
        setze tries auf tries + 1

monster_spawnen()

# === ITEMS ===
setze MAX_ITEMS auf 30
setze item_x auf []
setze item_y auf []
setze item_typ auf []
setze item_aktiv auf []

funktion items_spawnen():
    setze placed auf 0
    setze tries auf 0
    solange placed < MAX_ITEMS und tries < 300:
        setze ix auf 5 + rng() % (WELT_W - 10)
        setze iy auf 5 + rng() % (WELT_H - 10)
        wenn welt_get(ix, iy) == GRAS oder welt_get(ix, iy) == WEG:
            item_x.hinzufügen(ix)
            item_y.hinzufügen(iy)
            item_typ.hinzufügen(rng() % 3)
            item_aktiv.hinzufügen(wahr)
            setze placed auf placed + 1
        setze tries auf tries + 1

items_spawnen()

# === TILE-FARBEN ===
funktion tile_farbe(typ, x, y):
    wenn typ == GRAS:
        wenn (x + y) % 3 == 0:
            gib_zurück "#388E3C"
        gib_zurück "#43A047"
    wenn typ == BAUM:
        gib_zurück "#1B5E20"
    wenn typ == WASSER:
        wenn (x + y) % 5 == 0:
            gib_zurück "#1565C0"
        gib_zurück "#1976D2"
    wenn typ == STEIN:
        gib_zurück "#757575"
    wenn typ == WEG:
        gib_zurück "#8D6E63"
    wenn typ == BLUME:
        wenn x % 3 == 0:
            gib_zurück "#E91E63"
        wenn y % 3 == 0:
            gib_zurück "#FF9800"
        gib_zurück "#FFEB3B"
    wenn typ == SAND:
        gib_zurück "#FFE082"
    wenn typ == BUSCH:
        gib_zurück "#2E7D32"
    gib_zurück "#4CAF50"

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo RPG World (200x200)", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn spieler_hp <= 0:
        stopp

    # === INPUT ===
    wenn move_cooldown > 0:
        setze move_cooldown auf move_cooldown - 1
    sonst:
        setze moved auf falsch
        wenn taste_gedrückt("w"):
            setze spieler_dy auf -1
            setze spieler_dx auf 0
            setze moved auf wahr
        wenn taste_gedrückt("s"):
            setze spieler_dy auf 1
            setze spieler_dx auf 0
            setze moved auf wahr
        wenn taste_gedrückt("a"):
            setze spieler_dx auf -1
            setze spieler_dy auf 0
            setze moved auf wahr
        wenn taste_gedrückt("d"):
            setze spieler_dx auf 1
            setze spieler_dy auf 0
            setze moved auf wahr

        wenn moved:
            setze nx auf spieler_x + spieler_dx
            setze ny auf spieler_y + spieler_dy

            # Monster auf Zielfeld?
            setze hit auf -1
            setze mi auf 0
            solange mi < länge(mon_x):
                wenn mon_aktiv[mi] und mon_x[mi] == nx und mon_y[mi] == ny:
                    setze hit auf mi
                setze mi auf mi + 1

            wenn hit >= 0:
                mon_hp[hit] = mon_hp[hit] - spieler_dmg
                setze angriff_timer auf 8
                wenn mon_hp[hit] <= 0:
                    mon_aktiv[hit] = falsch
                    setze score auf score + (mon_typ[hit] + 1) * 15
            sonst:
                wenn ist_begehbar(nx, ny):
                    setze spieler_x auf nx
                    setze spieler_y auf ny

            # Items einsammeln
            setze ii auf 0
            solange ii < länge(item_x):
                wenn item_aktiv[ii] und item_x[ii] == spieler_x und item_y[ii] == spieler_y:
                    wenn item_typ[ii] == 0:
                        setze spieler_hp auf spieler_hp + 10
                        wenn spieler_hp > spieler_max_hp:
                            setze spieler_hp auf spieler_max_hp
                    wenn item_typ[ii] == 1:
                        setze spieler_dmg auf spieler_dmg + 1
                    wenn item_typ[ii] == 2:
                        setze spieler_max_hp auf spieler_max_hp + 5
                        setze spieler_hp auf spieler_hp + 5
                    item_aktiv[ii] = falsch
                    setze score auf score + 5
                setze ii auf ii + 1

            # Monster bewegen
            setze mi auf 0
            solange mi < länge(mon_x):
                wenn mon_aktiv[mi]:
                    setze dist auf abs_wert(mon_x[mi] - spieler_x) + abs_wert(mon_y[mi] - spieler_y)
                    wenn dist <= 1:
                        # Angriff auf Spieler
                        wenn mon_cooldown[mi] <= 0:
                            setze spieler_hp auf spieler_hp - mon_dmg[mi]
                            mon_cooldown[mi] = 3
                    wenn dist > 1 und dist < 10:
                        setze gdx auf 0
                        setze gdy auf 0
                        wenn mon_x[mi] < spieler_x:
                            setze gdx auf 1
                        wenn mon_x[mi] > spieler_x:
                            setze gdx auf -1
                        wenn mon_y[mi] < spieler_y:
                            setze gdy auf 1
                        wenn mon_y[mi] > spieler_y:
                            setze gdy auf -1
                        wenn abs_wert(mon_x[mi] - spieler_x) >= abs_wert(mon_y[mi] - spieler_y):
                            wenn ist_begehbar(mon_x[mi] + gdx, mon_y[mi]):
                                mon_x[mi] = mon_x[mi] + gdx
                            sonst:
                                wenn ist_begehbar(mon_x[mi], mon_y[mi] + gdy):
                                    mon_y[mi] = mon_y[mi] + gdy
                        sonst:
                            wenn ist_begehbar(mon_x[mi], mon_y[mi] + gdy):
                                mon_y[mi] = mon_y[mi] + gdy
                            sonst:
                                wenn ist_begehbar(mon_x[mi] + gdx, mon_y[mi]):
                                    mon_x[mi] = mon_x[mi] + gdx
                    wenn mon_cooldown[mi] > 0:
                        mon_cooldown[mi] = mon_cooldown[mi] - 1
                setze mi auf mi + 1

            setze move_cooldown auf 4

    wenn angriff_timer > 0:
        setze angriff_timer auf angriff_timer - 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#1B5E20")

    setze cam_x auf spieler_x - SICHT_W / 2
    setze cam_y auf spieler_y - SICHT_H / 2

    # Welt zeichnen
    setze sy auf 0
    solange sy < SICHT_H:
        setze sx auf 0
        solange sx < SICHT_W:
            setze wx auf cam_x + sx
            setze wy auf cam_y + sy
            wenn wx >= 0 und wx < WELT_W und wy >= 0 und wy < WELT_H:
                setze tile auf welt_get(wx, wy)
                setze farbe auf tile_farbe(tile, wx, wy)
                setze draw_x auf sx * TILE
                setze draw_y auf sy * TILE
                zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, farbe)
                # Baum-Detail
                wenn tile == BAUM:
                    zeichne_kreis(win, draw_x + TILE / 2, draw_y + TILE / 2 - 2, 6, "#2E7D32")
                    zeichne_rechteck(win, draw_x + TILE / 2 - 1, draw_y + TILE / 2 + 2, 2, 5, "#5D4037")
                wenn tile == BUSCH:
                    zeichne_kreis(win, draw_x + TILE / 2, draw_y + TILE / 2, 5, "#388E3C")
                wenn tile == BLUME:
                    zeichne_kreis(win, draw_x + TILE / 2, draw_y + TILE / 2, 3, "#FFFFFF")
            setze sx auf sx + 1
        setze sy auf sy + 1

    # Items
    setze ii auf 0
    solange ii < länge(item_x):
        wenn item_aktiv[ii]:
            setze draw_x auf (item_x[ii] - cam_x) * TILE
            setze draw_y auf (item_y[ii] - cam_y) * TILE
            wenn draw_x > -TILE und draw_x < BREITE und draw_y > -TILE und draw_y < HOEHE:
                wenn item_typ[ii] == 0:
                    zeichne_kreis(win, draw_x + TILE / 2, draw_y + TILE / 2, 5, "#F44336")
                wenn item_typ[ii] == 1:
                    zeichne_kreis(win, draw_x + TILE / 2, draw_y + TILE / 2, 5, "#FF9800")
                wenn item_typ[ii] == 2:
                    zeichne_kreis(win, draw_x + TILE / 2, draw_y + TILE / 2, 5, "#4CAF50")
        setze ii auf ii + 1

    # Monster
    setze mi auf 0
    solange mi < länge(mon_x):
        wenn mon_aktiv[mi]:
            setze draw_x auf (mon_x[mi] - cam_x) * TILE
            setze draw_y auf (mon_y[mi] - cam_y) * TILE
            wenn draw_x > -TILE und draw_x < BREITE und draw_y > -TILE und draw_y < HOEHE:
                setze mfarbe auf "#66BB6A"
                wenn mon_typ[mi] == 1:
                    setze mfarbe auf "#42A5F5"
                wenn mon_typ[mi] == 2:
                    setze mfarbe auf "#FF9800"
                wenn mon_typ[mi] == 3:
                    setze mfarbe auf "#F44336"
                zeichne_rechteck(win, draw_x + 2, draw_y + 2, TILE - 4, TILE - 4, mfarbe)
                zeichne_kreis(win, draw_x + 5, draw_y + 5, 2, "#FFFFFF")
                zeichne_kreis(win, draw_x + 11, draw_y + 5, 2, "#FFFFFF")
                # HP
                setze hp_w auf (TILE - 4) * mon_hp[mi] / mon_max_hp[mi]
                zeichne_rechteck(win, draw_x + 2, draw_y - 3, hp_w, 2, "#F44336")
        setze mi auf mi + 1

    # Spieler
    setze draw_x auf (spieler_x - cam_x) * TILE
    setze draw_y auf (spieler_y - cam_y) * TILE
    zeichne_rechteck(win, draw_x + 1, draw_y + 1, TILE - 2, TILE - 2, "#FFD700")
    zeichne_rechteck(win, draw_x + 3, draw_y + 3, TILE - 6, TILE - 6, "#FFC107")
    zeichne_rechteck(win, draw_x + 5 + spieler_dx * 2, draw_y + 4 + spieler_dy * 2, 2, 2, "#000000")
    zeichne_rechteck(win, draw_x + 9 + spieler_dx * 2, draw_y + 4 + spieler_dy * 2, 2, 2, "#000000")
    # Angriffs-Effekt
    wenn angriff_timer > 0:
        setze ax auf draw_x + spieler_dx * TILE
        setze ay auf draw_y + spieler_dy * TILE
        zeichne_rechteck(win, ax + 4, ay + 4, TILE - 8, TILE - 8, "#FFEB3B")

    # HUD
    zeichne_rechteck(win, 0, HOEHE - 24, BREITE, 24, "#1B2631")
    setze hp_w auf spieler_hp * 150 / spieler_max_hp
    zeichne_rechteck(win, 10, HOEHE - 18, 150, 12, "#333333")
    wenn spieler_hp > spieler_max_hp / 3:
        zeichne_rechteck(win, 10, HOEHE - 18, hp_w, 12, "#4CAF50")
    sonst:
        zeichne_rechteck(win, 10, HOEHE - 18, hp_w, 12, "#F44336")
    # Score
    setze si auf 0
    solange si < score / 20 und si < 20:
        zeichne_kreis(win, 180 + si * 14, HOEHE - 12, 4, "#FFD700")
        setze si auf si + 1
    # Position
    setze pos_dots auf spieler_x / 10
    wenn pos_dots > 20:
        setze pos_dots auf 20
    setze pi auf 0
    solange pi < pos_dots:
        zeichne_rechteck(win, BREITE - 200 + pi * 9, HOEHE - 18, 6, 5, "#42A5F5")
        setze pi auf pi + 1
    setze pos_dots auf spieler_y / 10
    wenn pos_dots > 20:
        setze pos_dots auf 20
    setze pi auf 0
    solange pi < pos_dots:
        zeichne_rechteck(win, BREITE - 200 + pi * 9, HOEHE - 10, 6, 5, "#EF5350")
        setze pi auf pi + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "RPG World beendet! Score: " + text(score)

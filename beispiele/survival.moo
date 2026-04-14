# ============================================================
# moo Survival — Crafting + Ueberleben in prozeduraler Welt
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/survival.moo -o beispiele/survival
#   ./beispiele/survival
#
# Bedienung:
#   WASD - Bewegen
#   Leertaste - Ressource abbauen / Angriff
#   1/2/3 - Werkzeug waehlen (Hand/Axt/Spitzhacke)
#   E - Crafting (wenn Materialien vorhanden)
#   Escape - Beenden
#
# Features:
#   * 150x150 prozedurale Welt (Biome, Fluesse, Waelder)
#   * Ressourcen: Holz, Stein, Nahrung, Eisen
#   * Crafting: Axt, Spitzhacke, Schwert, Mauer
#   * Hunger-System (sinkt ueber Zeit, Nahrung heilt)
#   * Tag-Nacht-Zyklus (Nacht = mehr Monster)
#   * Monster spawnen bei Nacht
#   * Mauern bauen zur Verteidigung
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600
setze TILE auf 16
setze WELT_W auf 150
setze WELT_H auf 150
setze SICHT_W auf 52
setze SICHT_H auf 40

# Tiles
setze GRAS auf 0
setze BAUM auf 1
setze WASSER auf 2
setze STEIN auf 3
setze ERZE auf 4
setze BUSCH auf 5
setze MAUER auf 6
setze SAND auf 7

setze welt auf []
setze rng_state auf 98765

funktion rng():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    wenn rng_state < 0:
        setze rng_state auf 0 - rng_state
    gib_zurück rng_state

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

funktion hash_2d(ix, iy):
    setze n auf ((ix + 1) * 374761 + (iy + 1) * 668265 + 77) % 2147483647
    wenn n < 0:
        setze n auf 0 - n
    setze n auf (n ^ (n / 2048)) % 2147483647
    setze n auf (n * 45673) % 2147483647
    gib_zurück n

funktion noise(x, y, scale):
    gib_zurück (hash_2d(boden(x * scale), boden(y * scale)) % 1000) / 1000.0

funktion welt_get(x, y):
    wenn x < 0 oder x >= WELT_W oder y < 0 oder y >= WELT_H:
        gib_zurück WASSER
    gib_zurück welt[y * WELT_W + x]

funktion welt_set(x, y, val):
    wenn x >= 0 und x < WELT_W und y >= 0 und y < WELT_H:
        welt[y * WELT_W + x] = val

funktion ist_begehbar(x, y):
    setze tile auf welt_get(x, y)
    gib_zurück tile == GRAS oder tile == SAND

# === WELT GENERIEREN ===
funktion welt_gen():
    setze idx auf 0
    solange idx < WELT_W * WELT_H:
        welt.hinzufügen(GRAS)
        setze idx auf idx + 1
    setze wy auf 0
    solange wy < WELT_H:
        setze wx auf 0
        solange wx < WELT_W:
            setze hoehe auf noise(wx, wy, 0.03)
            setze feucht auf noise(wx + 500, wy + 500, 0.05)
            setze detail auf noise(wx, wy, 0.15)
            setze tile auf GRAS
            wenn hoehe < 0.22:
                setze tile auf WASSER
            wenn hoehe >= 0.22 und hoehe < 0.28:
                setze tile auf SAND
            wenn hoehe > 0.78:
                setze tile auf STEIN
            wenn tile == GRAS und feucht > 0.55 und detail > 0.4:
                setze tile auf BAUM
            wenn tile == GRAS und feucht > 0.55 und detail > 0.3 und detail <= 0.4:
                setze tile auf BUSCH
            wenn tile == STEIN und detail > 0.7:
                setze tile auf ERZE
            welt_set(wx, wy, tile)
            setze wx auf wx + 1
        setze wy auf wy + 1

welt_gen()

# === SPIELER ===
setze spieler_x auf WELT_W / 2
setze spieler_y auf WELT_H / 2
# Startposition auf begehbarem Tile finden
solange nicht ist_begehbar(spieler_x, spieler_y):
    setze spieler_x auf spieler_x + 1
    wenn spieler_x >= WELT_W:
        setze spieler_x auf 0
        setze spieler_y auf spieler_y + 1

setze spieler_hp auf 100
setze spieler_max_hp auf 100
setze spieler_hunger auf 100
setze spieler_dx auf 0
setze spieler_dy auf 1
setze move_cooldown auf 0
setze angriff_timer auf 0

# Inventar
setze inv_holz auf 0
setze inv_stein auf 0
setze inv_nahrung auf 5
setze inv_eisen auf 0
setze hat_axt auf falsch
setze hat_spitzhacke auf falsch
setze hat_schwert auf falsch
setze werkzeug auf 0
setze score auf 0

# Tag-Nacht
setze tageszeit auf 0.25
setze tag_speed auf 0.0003

# === MONSTER ===
setze MAX_MONSTER auf 30
setze mon_x auf []
setze mon_y auf []
setze mon_hp auf []
setze mon_aktiv auf []
setze mi auf 0
solange mi < MAX_MONSTER:
    mon_x.hinzufügen(0)
    mon_y.hinzufügen(0)
    mon_hp.hinzufügen(0)
    mon_aktiv.hinzufügen(falsch)
    setze mi auf mi + 1

setze spawn_timer auf 0

funktion monster_spawnen(px, py):
    setze mi auf 0
    solange mi < MAX_MONSTER:
        wenn nicht mon_aktiv[mi]:
            setze mx auf px + rng() % 30 - 15
            setze my auf py + rng() % 30 - 15
            wenn ist_begehbar(mx, my):
                setze dist auf abs_wert(mx - px) + abs_wert(my - py)
                wenn dist > 8:
                    mon_x[mi] = mx
                    mon_y[mi] = my
                    mon_hp[mi] = 15
                    mon_aktiv[mi] = wahr
                    gib_zurück nichts
        setze mi auf mi + 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Survival", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn spieler_hp <= 0:
        stopp

    # Tag-Nacht
    setze tageszeit auf tageszeit + tag_speed
    wenn tageszeit >= 1.0:
        setze tageszeit auf tageszeit - 1.0
    setze ist_nacht auf tageszeit > 0.75 oder tageszeit < 0.2

    # Hunger
    setze spieler_hunger auf spieler_hunger - 0.02
    wenn spieler_hunger <= 0:
        setze spieler_hunger auf 0.0
        setze spieler_hp auf spieler_hp - 0.1

    # Werkzeug
    wenn taste_gedrückt("1"):
        setze werkzeug auf 0
    wenn taste_gedrückt("2") und hat_axt:
        setze werkzeug auf 1
    wenn taste_gedrückt("3") und hat_spitzhacke:
        setze werkzeug auf 2

    # Crafting (E)
    wenn taste_gedrückt("e") und move_cooldown == 0:
        # Axt: 5 Holz
        wenn nicht hat_axt und inv_holz >= 5:
            setze hat_axt auf wahr
            setze inv_holz auf inv_holz - 5
        # Spitzhacke: 3 Holz + 3 Stein
        wenn nicht hat_spitzhacke und inv_holz >= 3 und inv_stein >= 3:
            setze hat_spitzhacke auf wahr
            setze inv_holz auf inv_holz - 3
            setze inv_stein auf inv_stein - 3
        # Schwert: 2 Holz + 5 Eisen
        wenn nicht hat_schwert und inv_holz >= 2 und inv_eisen >= 5:
            setze hat_schwert auf wahr
            setze inv_holz auf inv_holz - 2
            setze inv_eisen auf inv_eisen - 5
        setze move_cooldown auf 10

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

        # Leertaste = Abbauen/Angriff
        wenn taste_gedrückt("leertaste"):
            setze tx auf spieler_x + spieler_dx
            setze ty auf spieler_y + spieler_dy
            setze tile auf welt_get(tx, ty)
            setze angriff_timer auf 8

            # Monster treffen
            setze hit auf falsch
            setze mi auf 0
            solange mi < MAX_MONSTER:
                wenn mon_aktiv[mi] und mon_x[mi] == tx und mon_y[mi] == ty:
                    setze dmg auf 3
                    wenn hat_schwert:
                        setze dmg auf 12
                    mon_hp[mi] = mon_hp[mi] - dmg
                    wenn mon_hp[mi] <= 0:
                        mon_aktiv[mi] = falsch
                        setze score auf score + 20
                        # Monster droppen Nahrung
                        setze inv_nahrung auf inv_nahrung + 3
                    setze hit auf wahr
                setze mi auf mi + 1

            wenn nicht hit:
                # Baum abbauen
                wenn tile == BAUM:
                    setze ertrag auf 1
                    wenn hat_axt:
                        setze ertrag auf 3
                    welt_set(tx, ty, GRAS)
                    setze inv_holz auf inv_holz + ertrag
                    setze score auf score + 2
                # Busch = Nahrung
                wenn tile == BUSCH:
                    welt_set(tx, ty, GRAS)
                    setze inv_nahrung auf inv_nahrung + 2
                    setze score auf score + 1
                # Stein abbauen
                wenn tile == STEIN:
                    wenn hat_spitzhacke:
                        welt_set(tx, ty, GRAS)
                        setze inv_stein auf inv_stein + 3
                        setze score auf score + 3
                # Erz abbauen
                wenn tile == ERZE:
                    wenn hat_spitzhacke:
                        welt_set(tx, ty, GRAS)
                        setze inv_eisen auf inv_eisen + 2
                        setze score auf score + 5
                # Mauer bauen (auf Gras, kostet 3 Stein)
                wenn tile == GRAS und inv_stein >= 3:
                    wenn taste_gedrückt("m"):
                        welt_set(tx, ty, MAUER)
                        setze inv_stein auf inv_stein - 3
            setze moved auf wahr
            setze move_cooldown auf 6

        wenn moved und nicht taste_gedrückt("leertaste"):
            setze nx auf spieler_x + spieler_dx
            setze ny auf spieler_y + spieler_dy
            wenn ist_begehbar(nx, ny):
                setze spieler_x auf nx
                setze spieler_y auf ny

            # Essen (automatisch wenn Hunger < 50 und Nahrung da)
            wenn spieler_hunger < 50 und inv_nahrung > 0:
                setze inv_nahrung auf inv_nahrung - 1
                setze spieler_hunger auf spieler_hunger + 25
                wenn spieler_hunger > 100:
                    setze spieler_hunger auf 100.0
                setze spieler_hp auf spieler_hp + 5
                wenn spieler_hp > spieler_max_hp:
                    setze spieler_hp auf spieler_max_hp

            # Monster bewegen
            setze mi auf 0
            solange mi < MAX_MONSTER:
                wenn mon_aktiv[mi]:
                    setze dist auf abs_wert(mon_x[mi] - spieler_x) + abs_wert(mon_y[mi] - spieler_y)
                    wenn dist <= 1:
                        setze spieler_hp auf spieler_hp - 5
                    wenn dist > 1 und dist < 12:
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
                        wenn rng() % 2 == 0:
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
                setze mi auf mi + 1

            setze move_cooldown auf 4

    # Monster spawnen bei Nacht
    wenn ist_nacht:
        setze spawn_timer auf spawn_timer + 1
        wenn spawn_timer > 60:
            setze spawn_timer auf 0
            monster_spawnen(spieler_x, spieler_y)

    wenn angriff_timer > 0:
        setze angriff_timer auf angriff_timer - 1

    # === ZEICHNEN ===
    # Himmelfarbe basierend auf Tageszeit
    setze sky_bright auf 0.5
    wenn tageszeit > 0.2 und tageszeit < 0.75:
        setze sky_bright auf 1.0
    fenster_löschen(win, "#1B5E20")

    setze cam_x auf spieler_x - SICHT_W / 2
    setze cam_y auf spieler_y - SICHT_H / 2

    # Welt
    setze sy auf 0
    solange sy < SICHT_H:
        setze sx auf 0
        solange sx < SICHT_W:
            setze wx auf cam_x + sx
            setze wy auf cam_y + sy
            setze draw_x auf sx * TILE
            setze draw_y auf sy * TILE
            wenn wx >= 0 und wx < WELT_W und wy >= 0 und wy < WELT_H:
                setze tile auf welt_get(wx, wy)
                wenn tile == GRAS:
                    wenn (wx + wy) % 3 == 0:
                        zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#388E3C")
                    sonst:
                        zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#43A047")
                wenn tile == BAUM:
                    zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#2E7D32")
                    zeichne_kreis(win, draw_x + 8, draw_y + 5, 6, "#1B5E20")
                    zeichne_rechteck(win, draw_x + 7, draw_y + 9, 2, 5, "#5D4037")
                wenn tile == WASSER:
                    zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#1565C0")
                wenn tile == STEIN:
                    zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#757575")
                    zeichne_rechteck(win, draw_x + 3, draw_y + 3, 5, 4, "#9E9E9E")
                wenn tile == ERZE:
                    zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#757575")
                    zeichne_kreis(win, draw_x + 5, draw_y + 5, 3, "#FF9800")
                    zeichne_kreis(win, draw_x + 11, draw_y + 10, 2, "#FF9800")
                wenn tile == BUSCH:
                    zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#388E3C")
                    zeichne_kreis(win, draw_x + 8, draw_y + 8, 5, "#66BB6A")
                    zeichne_kreis(win, draw_x + 4, draw_y + 10, 2, "#F44336")
                wenn tile == MAUER:
                    zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#795548")
                    zeichne_rechteck(win, draw_x + 1, draw_y + 1, TILE - 2, TILE - 2, "#8D6E63")
                    zeichne_linie(win, draw_x + 8, draw_y, draw_x + 8, draw_y + TILE, "#6D4C41")
                    zeichne_linie(win, draw_x, draw_y + 8, draw_x + TILE, draw_y + 8, "#6D4C41")
                wenn tile == SAND:
                    zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#FFE082")
            sonst:
                zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#0D47A1")
            setze sx auf sx + 1
        setze sy auf sy + 1

    # Nacht-Overlay
    wenn ist_nacht:
        # Dunkler Rahmen um den Sichtbereich
        setze ni auf 0
        solange ni < SICHT_W:
            zeichne_rechteck(win, ni * TILE, 0, TILE, TILE * 3, "#000033")
            zeichne_rechteck(win, ni * TILE, (SICHT_H - 3) * TILE, TILE, TILE * 3, "#000033")
            setze ni auf ni + 1
        setze ni auf 0
        solange ni < SICHT_H:
            zeichne_rechteck(win, 0, ni * TILE, TILE * 3, TILE, "#000033")
            zeichne_rechteck(win, (SICHT_W - 3) * TILE, ni * TILE, TILE * 3, TILE, "#000033")
            setze ni auf ni + 1

    # Monster
    setze mi auf 0
    solange mi < MAX_MONSTER:
        wenn mon_aktiv[mi]:
            setze draw_x auf (mon_x[mi] - cam_x) * TILE
            setze draw_y auf (mon_y[mi] - cam_y) * TILE
            wenn draw_x > -TILE und draw_x < BREITE und draw_y > -TILE und draw_y < HOEHE:
                zeichne_rechteck(win, draw_x + 2, draw_y + 2, TILE - 4, TILE - 4, "#9C27B0")
                zeichne_kreis(win, draw_x + 5, draw_y + 5, 2, "#E1BEE7")
                zeichne_kreis(win, draw_x + 11, draw_y + 5, 2, "#E1BEE7")
        setze mi auf mi + 1

    # Spieler
    setze draw_x auf (spieler_x - cam_x) * TILE
    setze draw_y auf (spieler_y - cam_y) * TILE
    zeichne_rechteck(win, draw_x + 1, draw_y + 1, TILE - 2, TILE - 2, "#FFD700")
    zeichne_rechteck(win, draw_x + 3, draw_y + 3, TILE - 6, TILE - 6, "#FFC107")
    zeichne_rechteck(win, draw_x + 5 + spieler_dx * 2, draw_y + 4 + spieler_dy * 2, 2, 2, "#000000")
    zeichne_rechteck(win, draw_x + 9 + spieler_dx * 2, draw_y + 4 + spieler_dy * 2, 2, 2, "#000000")
    # Angriff
    wenn angriff_timer > 0:
        setze ax auf draw_x + spieler_dx * TILE
        setze ay auf draw_y + spieler_dy * TILE
        zeichne_rechteck(win, ax + 4, ay + 4, TILE - 8, TILE - 8, "#FFEB3B")

    # HUD
    zeichne_rechteck(win, 0, HOEHE - 40, BREITE, 40, "#1B2631")
    # HP
    setze hp_w auf spieler_hp * 100 / spieler_max_hp
    zeichne_rechteck(win, 10, HOEHE - 35, 100, 10, "#333333")
    zeichne_rechteck(win, 10, HOEHE - 35, hp_w, 10, "#4CAF50")
    # Hunger
    setze hunger_w auf spieler_hunger
    zeichne_rechteck(win, 10, HOEHE - 20, 100, 10, "#333333")
    zeichne_rechteck(win, 10, HOEHE - 20, hunger_w, 10, "#FF9800")
    # Inventar
    setze ix auf 130
    # Holz
    setze hi auf 0
    solange hi < inv_holz und hi < 15:
        zeichne_rechteck(win, ix + hi * 8, HOEHE - 35, 6, 10, "#8D6E63")
        setze hi auf hi + 1
    # Stein
    setze si auf 0
    solange si < inv_stein und si < 15:
        zeichne_rechteck(win, ix + si * 8, HOEHE - 20, 6, 10, "#9E9E9E")
        setze si auf si + 1
    # Nahrung
    setze fi auf 0
    solange fi < inv_nahrung und fi < 10:
        zeichne_kreis(win, 280 + fi * 12, HOEHE - 30, 4, "#F44336")
        setze fi auf fi + 1
    # Eisen
    setze ei auf 0
    solange ei < inv_eisen und ei < 10:
        zeichne_kreis(win, 280 + ei * 12, HOEHE - 14, 4, "#FF9800")
        setze ei auf ei + 1
    # Werkzeug
    setze wx auf 420
    wenn hat_axt:
        zeichne_rechteck(win, wx, HOEHE - 35, 20, 12, "#8D6E63")
        wenn werkzeug == 1:
            zeichne_rechteck(win, wx - 1, HOEHE - 36, 22, 14, "#FFFFFF")
            zeichne_rechteck(win, wx, HOEHE - 35, 20, 12, "#8D6E63")
    wenn hat_spitzhacke:
        zeichne_rechteck(win, wx + 25, HOEHE - 35, 20, 12, "#78909C")
        wenn werkzeug == 2:
            zeichne_rechteck(win, wx + 24, HOEHE - 36, 22, 14, "#FFFFFF")
            zeichne_rechteck(win, wx + 25, HOEHE - 35, 20, 12, "#78909C")
    wenn hat_schwert:
        zeichne_rechteck(win, wx + 50, HOEHE - 35, 20, 12, "#B0BEC5")
    # Tag/Nacht Anzeige
    setze tn_x auf BREITE - 60
    wenn ist_nacht:
        zeichne_kreis(win, tn_x, HOEHE - 20, 10, "#FDD835")
    sonst:
        zeichne_kreis(win, tn_x, HOEHE - 20, 10, "#FF9800")
    # Score
    setze si auf 0
    solange si < score / 10 und si < 15:
        zeichne_kreis(win, BREITE - 100 + si * 10, HOEHE - 35, 3, "#FFD700")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Survival beendet! Score: " + text(score) + " | Holz:" + text(inv_holz) + " Stein:" + text(inv_stein) + " Eisen:" + text(inv_eisen)

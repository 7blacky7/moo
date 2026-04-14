# ============================================================
# moo Dungeon — Roguelike mit prozeduraler Dungeon-Generierung
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/dungeon.moo -o beispiele/dungeon
#   ./beispiele/dungeon
#
# Bedienung:
#   WASD oder Pfeiltasten - Bewegen (Turn-based)
#   Leertaste - Angreifen (Richtung des letzten Move)
#   Escape - Beenden
#
# Features:
#   * Prozedurale Dungeon-Generierung (BSP-artig: Raeume + Gaenge)
#   * Fog-of-War (nur sichtbar was in Reichweite ist)
#   * 5 Gegner-Typen mit steigender Staerke
#   * Items: Heiltrank, Staerke-Trank, Schluessel
#   * Treppen runter (naechstes Level)
#   * Turn-based Bewegung
#   * 10 Dungeon-Level
# ============================================================

setze BREITE auf 800
setze HOEHE auf 640
setze TILE auf 16
setze MAP_W auf 100
setze MAP_H auf 100
setze OFFSET_X auf 0
setze OFFSET_Y auf 32
setze SICHT auf 7

# Tile-Typen
setze WAND auf 0
setze BODEN auf 1
setze TREPPE auf 2
setze TUER auf 3

# Karte
setze karte auf []
setze sichtbar auf []
setze entdeckt auf []

# RNG
setze rng_state auf 777

funktion rng():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    wenn rng_state < 0:
        setze rng_state auf 0 - rng_state
    gib_zurück rng_state

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

funktion karte_get(x, y):
    wenn x < 0 oder x >= MAP_W oder y < 0 oder y >= MAP_H:
        gib_zurück WAND
    gib_zurück karte[y * MAP_W + x]

funktion karte_set(x, y, val):
    wenn x >= 0 und x < MAP_W und y >= 0 und y < MAP_H:
        karte[y * MAP_W + x] = val

# === DUNGEON-GENERIERUNG (Raum-basiert) ===
setze MAX_RAEUME auf 30
setze raum_x auf []
setze raum_y auf []
setze raum_w auf []
setze raum_h auf []
setze raum_count auf 0

funktion dungeon_generieren(level):
    # Karte zuruecksetzen
    setze idx auf 0
    solange idx < MAP_W * MAP_H:
        karte[idx] = WAND
        sichtbar[idx] = falsch
        entdeckt[idx] = falsch
        setze idx auf idx + 1
    setze raum_count auf 0

    # Raeume platzieren
    setze versuch auf 0
    solange versuch < 150 und raum_count < MAX_RAEUME:
        setze rw auf 5 + rng() % 8
        setze rh auf 4 + rng() % 6
        setze rx auf 2 + rng() % (MAP_W - rw - 4)
        setze ry auf 2 + rng() % (MAP_H - rh - 4)

        # Ueberlappung pruefen
        setze ok auf wahr
        setze ri auf 0
        solange ri < raum_count:
            wenn rx < raum_x[ri] + raum_w[ri] + 2 und rx + rw + 2 > raum_x[ri]:
                wenn ry < raum_y[ri] + raum_h[ri] + 2 und ry + rh + 2 > raum_y[ri]:
                    setze ok auf falsch
            setze ri auf ri + 1

        wenn ok:
            # Raum graben
            setze dy auf 0
            solange dy < rh:
                setze dx auf 0
                solange dx < rw:
                    karte_set(rx + dx, ry + dy, BODEN)
                    setze dx auf dx + 1
                setze dy auf dy + 1

            wenn raum_count < MAX_RAEUME:
                raum_x.hinzufügen(rx)
                raum_y.hinzufügen(ry)
                raum_w.hinzufügen(rw)
                raum_h.hinzufügen(rh)
                setze raum_count auf raum_count + 1
        setze versuch auf versuch + 1

    # Gaenge zwischen aufeinanderfolgenden Raeumen
    setze ri auf 1
    solange ri < raum_count:
        setze cx1 auf raum_x[ri - 1] + raum_w[ri - 1] / 2
        setze cy1 auf raum_y[ri - 1] + raum_h[ri - 1] / 2
        setze cx2 auf raum_x[ri] + raum_w[ri] / 2
        setze cy2 auf raum_y[ri] + raum_h[ri] / 2

        # L-foermiger Gang
        wenn rng() % 2 == 0:
            # Erst horizontal, dann vertikal
            setze gx auf cx1
            solange gx != cx2:
                karte_set(gx, cy1, BODEN)
                wenn gx < cx2:
                    setze gx auf gx + 1
                sonst:
                    setze gx auf gx - 1
            setze gy auf cy1
            solange gy != cy2:
                karte_set(cx2, gy, BODEN)
                wenn gy < cy2:
                    setze gy auf gy + 1
                sonst:
                    setze gy auf gy - 1
        sonst:
            # Erst vertikal, dann horizontal
            setze gy auf cy1
            solange gy != cy2:
                karte_set(cx1, gy, BODEN)
                wenn gy < cy2:
                    setze gy auf gy + 1
                sonst:
                    setze gy auf gy - 1
            setze gx auf cx1
            solange gx != cx2:
                karte_set(gx, cy2, BODEN)
                wenn gx < cx2:
                    setze gx auf gx + 1
                sonst:
                    setze gx auf gx - 1
        setze ri auf ri + 1

    # Treppe im letzten Raum
    wenn raum_count > 0:
        setze last auf raum_count - 1
        karte_set(raum_x[last] + raum_w[last] / 2, raum_y[last] + raum_h[last] / 2, TREPPE)

# === GEGNER ===
setze MAX_GEGNER auf 20
setze geg_x auf []
setze geg_y auf []
setze geg_hp auf []
setze geg_max_hp auf []
setze geg_dmg auf []
setze geg_typ auf []
setze geg_aktiv auf []
setze gi auf 0
solange gi < MAX_GEGNER:
    geg_x.hinzufügen(0)
    geg_y.hinzufügen(0)
    geg_hp.hinzufügen(0)
    geg_max_hp.hinzufügen(0)
    geg_dmg.hinzufügen(0)
    geg_typ.hinzufügen(0)
    geg_aktiv.hinzufügen(falsch)
    setze gi auf gi + 1

# === ITEMS ===
setze MAX_ITEMS auf 15
setze item_x auf []
setze item_y auf []
setze item_typ auf []
setze item_aktiv auf []
setze ii auf 0
solange ii < MAX_ITEMS:
    item_x.hinzufügen(0)
    item_y.hinzufügen(0)
    item_typ.hinzufügen(0)
    item_aktiv.hinzufügen(falsch)
    setze ii auf ii + 1

# === SPIELER ===
setze spieler_x auf 0
setze spieler_y auf 0
setze spieler_hp auf 30
setze spieler_max_hp auf 30
setze spieler_dmg auf 5
setze spieler_dx auf 0
setze spieler_dy auf 1
setze level auf 1
setze score auf 0
setze move_cooldown auf 0

funktion gegner_platzieren(level):
    setze count auf 3 + level * 2
    wenn count > MAX_GEGNER:
        setze count auf MAX_GEGNER
    setze placed auf 0
    setze tries auf 0
    solange placed < count und tries < 200:
        setze ri auf rng() % raum_count
        wenn ri > 0:
            setze gx auf raum_x[ri] + rng() % raum_w[ri]
            setze gy auf raum_y[ri] + rng() % raum_h[ri]
            wenn karte_get(gx, gy) == BODEN:
                setze gi auf 0
                solange gi < MAX_GEGNER:
                    wenn nicht geg_aktiv[gi]:
                        geg_x[gi] = gx
                        geg_y[gi] = gy
                        setze typ auf rng() % (1 + level / 2)
                        wenn typ > 4:
                            setze typ auf 4
                        geg_typ[gi] = typ
                        geg_hp[gi] = 8 + typ * 5 + level * 2
                        geg_max_hp[gi] = geg_hp[gi]
                        geg_dmg[gi] = 3 + typ * 2 + level
                        geg_aktiv[gi] = wahr
                        setze placed auf placed + 1
                        setze gi auf MAX_GEGNER
                    setze gi auf gi + 1
        setze tries auf tries + 1

funktion items_platzieren(level):
    setze count auf 3 + level
    wenn count > MAX_ITEMS:
        setze count auf MAX_ITEMS
    setze placed auf 0
    setze tries auf 0
    solange placed < count und tries < 100:
        setze ri auf rng() % raum_count
        setze ix auf raum_x[ri] + rng() % raum_w[ri]
        setze iy auf raum_y[ri] + rng() % raum_h[ri]
        wenn karte_get(ix, iy) == BODEN:
            setze ii auf 0
            solange ii < MAX_ITEMS:
                wenn nicht item_aktiv[ii]:
                    item_x[ii] = ix
                    item_y[ii] = iy
                    item_typ[ii] = rng() % 3
                    item_aktiv[ii] = wahr
                    setze placed auf placed + 1
                    setze ii auf MAX_ITEMS
                setze ii auf ii + 1
        setze tries auf tries + 1

funktion sicht_berechnen():
    # Reset Sichtbarkeit
    setze idx auf 0
    solange idx < MAP_W * MAP_H:
        sichtbar[idx] = falsch
        setze idx auf idx + 1
    # Einfacher Sichtkreis
    setze dy auf 0 - SICHT
    solange dy <= SICHT:
        setze dx auf 0 - SICHT
        solange dx <= SICHT:
            wenn dx * dx + dy * dy <= SICHT * SICHT:
                setze tx auf spieler_x + dx
                setze ty auf spieler_y + dy
                wenn tx >= 0 und tx < MAP_W und ty >= 0 und ty < MAP_H:
                    sichtbar[ty * MAP_W + tx] = wahr
                    entdeckt[ty * MAP_W + tx] = wahr
            setze dx auf dx + 1
        setze dy auf dy + 1

funktion neues_level():
    # Gegner + Items reset
    setze gi auf 0
    solange gi < MAX_GEGNER:
        geg_aktiv[gi] = falsch
        setze gi auf gi + 1
    setze ii auf 0
    solange ii < MAX_ITEMS:
        item_aktiv[ii] = falsch
        setze ii auf ii + 1
    # Raeume reset
    setze raum_x auf []
    setze raum_y auf []
    setze raum_w auf []
    setze raum_h auf []
    setze raum_count auf 0
    # Generieren
    dungeon_generieren(level)
    gegner_platzieren(level)
    items_platzieren(level)
    # Spieler in ersten Raum
    wenn raum_count > 0:
        setze spieler_x auf raum_x[0] + raum_w[0] / 2
        setze spieler_y auf raum_y[0] + raum_h[0] / 2
    sicht_berechnen()

# Initialisierung
setze idx auf 0
solange idx < MAP_W * MAP_H:
    karte.hinzufügen(WAND)
    sichtbar.hinzufügen(falsch)
    entdeckt.hinzufügen(falsch)
    setze idx auf idx + 1

neues_level()

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Dungeon — Level " + text(level), BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn spieler_hp <= 0:
        stopp
    wenn level > 10:
        stopp

    # === INPUT (Turn-based mit Cooldown) ===
    wenn move_cooldown > 0:
        setze move_cooldown auf move_cooldown - 1
    sonst:
        setze moved auf falsch
        setze ndx auf 0
        setze ndy auf 0
        wenn taste_gedrückt("w") oder taste_gedrückt("hoch"):
            setze ndy auf -1
            setze moved auf wahr
        wenn taste_gedrückt("s") oder taste_gedrückt("runter"):
            setze ndy auf 1
            setze moved auf wahr
        wenn taste_gedrückt("a") oder taste_gedrückt("links"):
            setze ndx auf -1
            setze moved auf wahr
        wenn taste_gedrückt("d") oder taste_gedrückt("rechts"):
            setze ndx auf 1
            setze moved auf wahr

        wenn moved:
            setze spieler_dx auf ndx
            setze spieler_dy auf ndy
            setze nx auf spieler_x + ndx
            setze ny auf spieler_y + ndy

            # Gegner auf Zielfeld?
            setze gegner_da auf -1
            setze gi auf 0
            solange gi < MAX_GEGNER:
                wenn geg_aktiv[gi] und geg_x[gi] == nx und geg_y[gi] == ny:
                    setze gegner_da auf gi
                setze gi auf gi + 1

            wenn gegner_da >= 0:
                # Angriff!
                geg_hp[gegner_da] = geg_hp[gegner_da] - spieler_dmg
                wenn geg_hp[gegner_da] <= 0:
                    geg_aktiv[gegner_da] = falsch
                    setze score auf score + (geg_typ[gegner_da] + 1) * 20
            sonst:
                wenn karte_get(nx, ny) != WAND:
                    setze spieler_x auf nx
                    setze spieler_y auf ny

            # Treppe?
            wenn karte_get(spieler_x, spieler_y) == TREPPE:
                setze level auf level + 1
                neues_level()

            # Items einsammeln
            setze ii auf 0
            solange ii < MAX_ITEMS:
                wenn item_aktiv[ii] und item_x[ii] == spieler_x und item_y[ii] == spieler_y:
                    wenn item_typ[ii] == 0:
                        # Heiltrank
                        setze spieler_hp auf spieler_hp + 15
                        wenn spieler_hp > spieler_max_hp:
                            setze spieler_hp auf spieler_max_hp
                    wenn item_typ[ii] == 1:
                        # Staerke
                        setze spieler_dmg auf spieler_dmg + 2
                    wenn item_typ[ii] == 2:
                        # Max-HP
                        setze spieler_max_hp auf spieler_max_hp + 5
                        setze spieler_hp auf spieler_hp + 5
                    item_aktiv[ii] = falsch
                    setze score auf score + 10
                setze ii auf ii + 1

            # Gegner bewegen (einfache KI: zum Spieler wenn sichtbar)
            setze gi auf 0
            solange gi < MAX_GEGNER:
                wenn geg_aktiv[gi]:
                    setze dist auf abs_wert(geg_x[gi] - spieler_x) + abs_wert(geg_y[gi] - spieler_y)
                    wenn dist < SICHT und dist > 1:
                        setze gdx auf 0
                        setze gdy auf 0
                        wenn geg_x[gi] < spieler_x:
                            setze gdx auf 1
                        wenn geg_x[gi] > spieler_x:
                            setze gdx auf -1
                        wenn geg_y[gi] < spieler_y:
                            setze gdy auf 1
                        wenn geg_y[gi] > spieler_y:
                            setze gdy auf -1
                        # Bevorzuge groessere Distanz-Achse
                        wenn abs_wert(geg_x[gi] - spieler_x) >= abs_wert(geg_y[gi] - spieler_y):
                            wenn karte_get(geg_x[gi] + gdx, geg_y[gi]) == BODEN:
                                geg_x[gi] = geg_x[gi] + gdx
                            sonst:
                                wenn karte_get(geg_x[gi], geg_y[gi] + gdy) == BODEN:
                                    geg_y[gi] = geg_y[gi] + gdy
                        sonst:
                            wenn karte_get(geg_x[gi], geg_y[gi] + gdy) == BODEN:
                                geg_y[gi] = geg_y[gi] + gdy
                            sonst:
                                wenn karte_get(geg_x[gi] + gdx, geg_y[gi]) == BODEN:
                                    geg_x[gi] = geg_x[gi] + gdx
                    # Gegner neben Spieler → Angriff
                    wenn abs_wert(geg_x[gi] - spieler_x) + abs_wert(geg_y[gi] - spieler_y) == 1:
                        setze spieler_hp auf spieler_hp - geg_dmg[gi]
                setze gi auf gi + 1

            sicht_berechnen()
            setze move_cooldown auf 6

    # === ZEICHNEN ===
    fenster_löschen(win, "#0A0A0A")

    # Karte (mit Kamera zentriert auf Spieler)
    setze cam_x auf spieler_x - MAP_W / 4
    setze cam_y auf spieler_y - MAP_H / 4

    setze sy auf 0
    solange sy < MAP_H:
        setze sx auf 0
        solange sx < MAP_W:
            setze screen_x auf (sx - cam_x) * TILE + OFFSET_X
            setze screen_y auf (sy - cam_y) * TILE + OFFSET_Y
            wenn screen_x > -TILE und screen_x < BREITE und screen_y > -TILE und screen_y < HOEHE:
                setze idx auf sy * MAP_W + sx
                setze ist_sichtbar auf sichtbar[idx]
                setze ist_entdeckt auf entdeckt[idx]
                setze val auf karte[idx]
                wenn ist_sichtbar:
                    wenn val == WAND:
                        zeichne_rechteck(win, screen_x, screen_y, TILE, TILE, "#37474F")
                    wenn val == BODEN:
                        zeichne_rechteck(win, screen_x, screen_y, TILE, TILE, "#263238")
                        wenn (sx + sy) % 2 == 0:
                            zeichne_rechteck(win, screen_x + 1, screen_y + 1, TILE - 2, TILE - 2, "#2C3E50")
                    wenn val == TREPPE:
                        zeichne_rechteck(win, screen_x, screen_y, TILE, TILE, "#263238")
                        zeichne_rechteck(win, screen_x + 3, screen_y + 3, TILE - 6, TILE - 6, "#FFD700")
                        zeichne_rechteck(win, screen_x + 5, screen_y + 5, TILE - 10, TILE - 10, "#FFA000")
                sonst:
                    wenn ist_entdeckt:
                        wenn val == WAND:
                            zeichne_rechteck(win, screen_x, screen_y, TILE, TILE, "#1A1A2E")
                        wenn val == BODEN oder val == TREPPE:
                            zeichne_rechteck(win, screen_x, screen_y, TILE, TILE, "#111122")
            setze sx auf sx + 1
        setze sy auf sy + 1

    # Items (nur sichtbare)
    setze ii auf 0
    solange ii < MAX_ITEMS:
        wenn item_aktiv[ii]:
            setze ix auf item_x[ii]
            setze iy auf item_y[ii]
            wenn ix >= 0 und ix < MAP_W und iy >= 0 und iy < MAP_H:
                wenn sichtbar[iy * MAP_W + ix]:
                    setze screen_x auf (ix - cam_x) * TILE + OFFSET_X + TILE / 2
                    setze screen_y auf (iy - cam_y) * TILE + OFFSET_Y + TILE / 2
                    wenn item_typ[ii] == 0:
                        zeichne_kreis(win, screen_x, screen_y, 5, "#F44336")
                    wenn item_typ[ii] == 1:
                        zeichne_kreis(win, screen_x, screen_y, 5, "#FF9800")
                    wenn item_typ[ii] == 2:
                        zeichne_kreis(win, screen_x, screen_y, 5, "#4CAF50")
        setze ii auf ii + 1

    # Gegner (nur sichtbare)
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn geg_aktiv[gi]:
            setze gx auf geg_x[gi]
            setze gy auf geg_y[gi]
            wenn gx >= 0 und gx < MAP_W und gy >= 0 und gy < MAP_H:
                wenn sichtbar[gy * MAP_W + gx]:
                    setze screen_x auf (gx - cam_x) * TILE + OFFSET_X
                    setze screen_y auf (gy - cam_y) * TILE + OFFSET_Y
                    setze gfarbe auf "#66BB6A"
                    wenn geg_typ[gi] == 1:
                        setze gfarbe auf "#42A5F5"
                    wenn geg_typ[gi] == 2:
                        setze gfarbe auf "#FF9800"
                    wenn geg_typ[gi] == 3:
                        setze gfarbe auf "#F44336"
                    wenn geg_typ[gi] >= 4:
                        setze gfarbe auf "#9C27B0"
                    zeichne_rechteck(win, screen_x + 2, screen_y + 2, TILE - 4, TILE - 4, gfarbe)
                    # HP-Balken
                    setze hp_w auf (TILE - 4) * geg_hp[gi] / geg_max_hp[gi]
                    zeichne_rechteck(win, screen_x + 2, screen_y - 2, hp_w, 2, "#F44336")
        setze gi auf gi + 1

    # Spieler
    setze screen_x auf (spieler_x - cam_x) * TILE + OFFSET_X
    setze screen_y auf (spieler_y - cam_y) * TILE + OFFSET_Y
    zeichne_rechteck(win, screen_x + 1, screen_y + 1, TILE - 2, TILE - 2, "#FFD700")
    zeichne_rechteck(win, screen_x + 3, screen_y + 3, TILE - 6, TILE - 6, "#FFC107")
    # Augen (Blickrichtung)
    zeichne_rechteck(win, screen_x + 5 + spieler_dx * 3, screen_y + 4 + spieler_dy * 3, 2, 2, "#000000")
    zeichne_rechteck(win, screen_x + 9 + spieler_dx * 3, screen_y + 4 + spieler_dy * 3, 2, 2, "#000000")

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 28, "#1B2631")
    # HP-Balken
    setze hp_w auf spieler_hp * 150 / spieler_max_hp
    zeichne_rechteck(win, 10, 6, 150, 14, "#333333")
    wenn spieler_hp > spieler_max_hp / 3:
        zeichne_rechteck(win, 10, 6, hp_w, 14, "#4CAF50")
    sonst:
        zeichne_rechteck(win, 10, 6, hp_w, 14, "#F44336")
    # Level
    setze li auf 0
    solange li < level und li < 10:
        zeichne_rechteck(win, 180 + li * 14, 8, 10, 10, "#FFD700")
        setze li auf li + 1
    # Score
    setze si auf 0
    solange si < score / 50 und si < 20:
        zeichne_kreis(win, BREITE - 200 + si * 14, 14, 4, "#FFD700")
        setze si auf si + 1
    # DMG Anzeige
    setze di auf 0
    solange di < spieler_dmg / 2 und di < 10:
        zeichne_rechteck(win, BREITE - 20 - di * 12, 8, 8, 10, "#FF9800")
        setze di auf di + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn level > 10:
    zeige "SIEG! Alle 10 Level geschafft! Score: " + text(score)
sonst:
    zeige "GAME OVER auf Level " + text(level) + "! Score: " + text(score)

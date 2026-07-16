# ============================================================
# moo Tower Defense — Klassisches Turm-Verteidigungsspiel
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/tower_defense.moo -o beispiele/tower_defense
#   ./beispiele/tower_defense
#
# Bedienung:
#   Maus-Klick   - Turm platzieren (auf gruenem Feld)
#   1/2/3        - Turm-Typ waehlen (Pfeil/Kanone/Laser)
#   Escape       - Beenden
#
# Features:
#   * Grid-basiertes Spielfeld mit Pfad
#   * 3 Turm-Typen mit verschiedenen Stats
#   * Gegner-Wellen mit steigender Staerke
#   * Gold + Score System
#   * Lebens-System (Gegner am Ziel = -1 Leben)
# ============================================================

# === KONSTANTEN ===
setze BREITE auf 800
setze HOEHE auf 600
setze TILE auf 40
setze GRID_W auf 20
setze GRID_H auf 15
setze MAX_GEGNER auf 40
setze MAX_TUERME auf 50
setze MAX_GESCHOSSE auf 60

# === SPIELFELD (0=leer, 1=Pfad, 2=Start, 3=Ziel, 9=Turm) ===
setze karte auf []
# Initialisiere leeres Feld
setze mi auf 0
solange mi < GRID_W * GRID_H:
    karte.hinzufügen(0)
    setze mi auf mi + 1

# Pfad definieren (Start links, Ziel rechts, Serpentine)
funktion pfad_setzen(x, y, typ):
    karte[y * GRID_W + x] = typ

# Serpentinen-Pfad
setze px auf 0
solange px < 18:
    pfad_setzen(px, 2, 1)
    setze px auf px + 1
pfad_setzen(0, 2, 2)
setze py auf 2
solange py < 6:
    pfad_setzen(17, py, 1)
    setze py auf py + 1
setze px auf 2
solange px < 18:
    pfad_setzen(px, 6, 1)
    setze px auf px + 1
setze py auf 6
solange py < 10:
    pfad_setzen(2, py, 1)
    setze py auf py + 1
setze px auf 2
solange px < 18:
    pfad_setzen(px, 10, 1)
    setze px auf px + 1
pfad_setzen(17, 10, 3)

# Pfad-Wegpunkte (Reihenfolge fuer Gegner-Bewegung)
setze wp_x auf [0, 17, 17, 2, 2, 17]
setze wp_y auf [2, 2, 6, 6, 10, 10]
setze wp_count auf 6

# === TURM-TYPEN ===
# Typ 0: Pfeil (schnell, wenig Schaden, billig)
# Typ 1: Kanone (langsam, viel Schaden, mittel)
# Typ 2: Laser (mittel, mittel Schaden, teuer, grosse Reichweite)
setze turm_kosten auf [50, 100, 150]
setze turm_schaden auf [8, 25, 15]
setze turm_reichweite auf [3.0, 2.5, 5.0]
setze turm_feuerrate auf [15, 40, 25]

# === SPIELZUSTAND ===
setze gold auf 200
setze leben auf 20
setze punkte auf 0
setze welle auf 0
setze welle_timer auf 0
setze welle_interval auf 180
setze gewaehlt auf 0

# === TÜRME ===
setze turm_x auf []
setze turm_y auf []
setze turm_typ auf []
setze turm_cooldown auf []
setze turm_count auf 0

# === GEGNER ===
setze geg_x auf []
setze geg_y auf []
setze geg_hp auf []
setze geg_max_hp auf []
setze geg_wp auf []
setze geg_aktiv auf []
setze geg_speed auf []
setze gi auf 0
solange gi < MAX_GEGNER:
    geg_x.hinzufügen(0.0)
    geg_y.hinzufügen(0.0)
    geg_hp.hinzufügen(0)
    geg_max_hp.hinzufügen(0)
    geg_wp.hinzufügen(0)
    geg_aktiv.hinzufügen(falsch)
    geg_speed.hinzufügen(0.0)
    setze gi auf gi + 1

# === GESCHOSSE ===
setze proj_x auf []
setze proj_y auf []
setze proj_ziel auf []
setze proj_dmg auf []
setze proj_aktiv auf []
setze pi auf 0
solange pi < MAX_GESCHOSSE:
    proj_x.hinzufügen(0.0)
    proj_y.hinzufügen(0.0)
    proj_ziel.hinzufügen(0)
    proj_dmg.hinzufügen(0)
    proj_aktiv.hinzufügen(falsch)
    setze pi auf pi + 1

# === HILFSFUNKTIONEN ===
funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

funktion distanz(x1, y1, x2, y2):
    setze dx auf x2 - x1
    setze dy auf y2 - y1
    gib_zurück wurzel(dx * dx + dy * dy)

funktion gegner_spawnen(hp, speed):
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn nicht geg_aktiv[gi]:
            geg_x[gi] = wp_x[0] * TILE + TILE / 2.0
            geg_y[gi] = wp_y[0] * TILE + TILE / 2.0
            geg_hp[gi] = hp
            geg_max_hp[gi] = hp
            geg_wp[gi] = 1
            geg_aktiv[gi] = wahr
            geg_speed[gi] = speed
            gib_zurück nichts
        setze gi auf gi + 1

funktion geschoss_feuern(x, y, ziel, dmg):
    setze pi auf 0
    solange pi < MAX_GESCHOSSE:
        wenn nicht proj_aktiv[pi]:
            proj_x[pi] = x
            proj_y[pi] = y
            proj_ziel[pi] = ziel
            proj_dmg[pi] = dmg
            proj_aktiv[pi] = wahr
            gib_zurück nichts
        setze pi auf pi + 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Tower Defense", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn leben <= 0:
        stopp

    # === TURM-TYP WÄHLEN ===
    wenn taste_gedrückt("1"):
        setze gewaehlt auf 0
    wenn taste_gedrückt("2"):
        setze gewaehlt auf 1
    wenn taste_gedrückt("3"):
        setze gewaehlt auf 2

    # === MAUS-KLICK: TURM PLATZIEREN ===
    wenn maus_gedrückt(win):
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        setze gx auf boden(mx / TILE)
        setze gy auf boden(my / TILE)
        wenn gx >= 0 und gx < GRID_W und gy >= 0 und gy < GRID_H:
            setze tile_idx auf gy * GRID_W + gx
            wenn karte[tile_idx] == 0:
                wenn gold >= turm_kosten[gewaehlt]:
                    setze gold auf gold - turm_kosten[gewaehlt]
                    karte[tile_idx] = 9
                    turm_x.hinzufügen(gx)
                    turm_y.hinzufügen(gy)
                    turm_typ.hinzufügen(gewaehlt)
                    turm_cooldown.hinzufügen(0)
                    setze turm_count auf turm_count + 1

    # === WELLEN-SYSTEM ===
    setze welle_timer auf welle_timer + 1
    wenn welle_timer >= welle_interval:
        setze welle_timer auf 0
        setze welle auf welle + 1
        setze spawn_count auf 3 + welle * 2
        wenn spawn_count > 15:
            setze spawn_count auf 15
        setze spawn_hp auf 30 + welle * 15
        setze spawn_speed auf 1.0 + welle * 0.1
        wenn spawn_speed > 3.0:
            setze spawn_speed auf 3.0
        setze spawn_i auf 0
        solange spawn_i < spawn_count:
            gegner_spawnen(spawn_hp, spawn_speed)
            setze spawn_i auf spawn_i + 1

    # === GEGNER BEWEGEN ===
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn geg_aktiv[gi]:
            setze ziel_wp auf geg_wp[gi]
            wenn ziel_wp < wp_count:
                setze zx auf wp_x[ziel_wp] * TILE + TILE / 2.0
                setze zy auf wp_y[ziel_wp] * TILE + TILE / 2.0
                setze dx auf zx - geg_x[gi]
                setze dy auf zy - geg_y[gi]
                setze dist auf distanz(geg_x[gi], geg_y[gi], zx, zy)
                wenn dist < geg_speed[gi] * 2:
                    geg_wp[gi] = geg_wp[gi] + 1
                sonst:
                    geg_x[gi] = geg_x[gi] + dx / dist * geg_speed[gi]
                    geg_y[gi] = geg_y[gi] + dy / dist * geg_speed[gi]
            sonst:
                # Gegner hat Ziel erreicht
                geg_aktiv[gi] = falsch
                setze leben auf leben - 1
        setze gi auf gi + 1

    # === TÜRME SCHIESSEN ===
    setze ti auf 0
    solange ti < turm_count:
        wenn turm_cooldown[ti] > 0:
            turm_cooldown[ti] = turm_cooldown[ti] - 1
        sonst:
            # Naechsten Gegner in Reichweite finden
            setze tx auf turm_x[ti] * TILE + TILE / 2.0
            setze ty auf turm_y[ti] * TILE + TILE / 2.0
            setze best auf -1
            setze best_dist auf 999.0
            setze gi auf 0
            solange gi < MAX_GEGNER:
                wenn geg_aktiv[gi]:
                    setze dist auf distanz(tx, ty, geg_x[gi], geg_y[gi])
                    wenn dist < turm_reichweite[turm_typ[ti]] * TILE:
                        wenn dist < best_dist:
                            setze best auf gi
                            setze best_dist auf dist
                setze gi auf gi + 1
            wenn best >= 0:
                geschoss_feuern(tx, ty, best, turm_schaden[turm_typ[ti]])
                turm_cooldown[ti] = turm_feuerrate[turm_typ[ti]]
        setze ti auf ti + 1

    # === GESCHOSSE BEWEGEN ===
    setze pi auf 0
    solange pi < MAX_GESCHOSSE:
        wenn proj_aktiv[pi]:
            setze ziel auf proj_ziel[pi]
            wenn nicht geg_aktiv[ziel]:
                proj_aktiv[pi] = falsch
            sonst:
                setze dx auf geg_x[ziel] - proj_x[pi]
                setze dy auf geg_y[ziel] - proj_y[pi]
                setze dist auf distanz(proj_x[pi], proj_y[pi], geg_x[ziel], geg_y[ziel])
                wenn dist < 8:
                    # Treffer
                    geg_hp[ziel] = geg_hp[ziel] - proj_dmg[pi]
                    proj_aktiv[pi] = falsch
                    wenn geg_hp[ziel] <= 0:
                        geg_aktiv[ziel] = falsch
                        setze gold auf gold + 10
                        setze punkte auf punkte + 50
                sonst:
                    proj_x[pi] = proj_x[pi] + dx / dist * 6
                    proj_y[pi] = proj_y[pi] + dy / dist * 6
        setze pi auf pi + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#1B5E20")

    # Grid zeichnen
    setze gy auf 0
    solange gy < GRID_H:
        setze gx auf 0
        solange gx < GRID_W:
            setze tile_idx auf gy * GRID_W + gx
            setze tile_val auf karte[tile_idx]
            setze draw_x auf gx * TILE
            setze draw_y auf gy * TILE
            wenn tile_val == 1 oder tile_val == 2 oder tile_val == 3:
                zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#795548")
                zeichne_rechteck(win, draw_x + 1, draw_y + 1, TILE - 2, TILE - 2, "#8D6E63")
            wenn tile_val == 2:
                zeichne_kreis(win, draw_x + TILE / 2, draw_y + TILE / 2, 12, "#4CAF50")
            wenn tile_val == 3:
                zeichne_kreis(win, draw_x + TILE / 2, draw_y + TILE / 2, 12, "#F44336")
            wenn tile_val == 0:
                zeichne_rechteck(win, draw_x, draw_y, TILE, TILE, "#2E7D32")
                zeichne_rechteck(win, draw_x + 1, draw_y + 1, TILE - 2, TILE - 2, "#388E3C")
            setze gx auf gx + 1
        setze gy auf gy + 1

    # Türme
    setze ti auf 0
    solange ti < turm_count:
        setze tx auf turm_x[ti] * TILE
        setze ty auf turm_y[ti] * TILE
        wenn turm_typ[ti] == 0:
            # Pfeil-Turm (grau)
            zeichne_rechteck(win, tx + 8, ty + 8, TILE - 16, TILE - 16, "#78909C")
            zeichne_rechteck(win, tx + 14, ty + 4, 12, 8, "#546E7A")
        wenn turm_typ[ti] == 1:
            # Kanone (dunkel)
            zeichne_rechteck(win, tx + 6, ty + 6, TILE - 12, TILE - 12, "#37474F")
            zeichne_kreis(win, tx + TILE / 2, ty + TILE / 2, 8, "#263238")
        wenn turm_typ[ti] == 2:
            # Laser (blau)
            zeichne_rechteck(win, tx + 10, ty + 10, TILE - 20, TILE - 20, "#1565C0")
            zeichne_kreis(win, tx + TILE / 2, ty + TILE / 2, 6, "#42A5F5")
        # Reichweite anzeigen wenn Maus drüber
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        wenn mx > tx und mx < tx + TILE und my > ty und my < ty + TILE:
            zeichne_kreis(win, tx + TILE / 2, ty + TILE / 2, turm_reichweite[turm_typ[ti]] * TILE, "#FFFFFF")
        setze ti auf ti + 1

    # Gegner
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn geg_aktiv[gi]:
            zeichne_rechteck(win, geg_x[gi] - 8, geg_y[gi] - 8, 16, 16, "#F44336")
            # HP-Balken
            setze hp_pct auf geg_hp[gi] * 16 / geg_max_hp[gi]
            zeichne_rechteck(win, geg_x[gi] - 8, geg_y[gi] - 14, 16, 3, "#333333")
            zeichne_rechteck(win, geg_x[gi] - 8, geg_y[gi] - 14, hp_pct, 3, "#4CAF50")
        setze gi auf gi + 1

    # Geschosse
    setze pi auf 0
    solange pi < MAX_GESCHOSSE:
        wenn proj_aktiv[pi]:
            zeichne_kreis(win, proj_x[pi], proj_y[pi], 3, "#FFEB3B")
        setze pi auf pi + 1

    # HUD
    zeichne_rechteck(win, 0, HOEHE - 40, BREITE, 40, "#212121")
    # Gold (gelbe Punkte)
    setze gold_dots auf gold / 25
    wenn gold_dots > 20:
        setze gold_dots auf 20
    setze gi auf 0
    solange gi < gold_dots:
        zeichne_kreis(win, 15 + gi * 14, HOEHE - 28, 4, "#FFD700")
        setze gi auf gi + 1
    # Leben (rote Kreise)
    setze li auf 0
    solange li < leben und li < 20:
        zeichne_kreis(win, 15 + li * 14, HOEHE - 12, 4, "#F44336")
        setze li auf li + 1
    # Welle
    setze wave_i auf 0
    solange wave_i < welle und wave_i < 15:
        zeichne_rechteck(win, BREITE - 200 + wave_i * 12, HOEHE - 30, 8, 8, "#FF9800")
        setze wave_i auf wave_i + 1
    # Gewählter Turm (Anzeige)
    zeichne_rechteck(win, BREITE / 2 - 60, HOEHE - 36, 120, 32, "#333333")
    wenn gewaehlt == 0:
        zeichne_rechteck(win, BREITE / 2 - 50, HOEHE - 30, 30, 20, "#78909C")
    wenn gewaehlt == 1:
        zeichne_rechteck(win, BREITE / 2 - 10, HOEHE - 30, 30, 20, "#37474F")
    wenn gewaehlt == 2:
        zeichne_rechteck(win, BREITE / 2 + 30, HOEHE - 30, 30, 20, "#1565C0")
    # Auswahlrahmen
    setze sel_x auf BREITE / 2 - 52 + gewaehlt * 40
    zeichne_rechteck(win, sel_x, HOEHE - 32, 34, 24, "#FFFFFF")
    wenn gewaehlt == 0:
        zeichne_rechteck(win, sel_x + 2, HOEHE - 30, 30, 20, "#78909C")
    wenn gewaehlt == 1:
        zeichne_rechteck(win, sel_x + 2, HOEHE - 30, 30, 20, "#37474F")
    wenn gewaehlt == 2:
        zeichne_rechteck(win, sel_x + 2, HOEHE - 30, 30, 20, "#1565C0")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "GAME OVER! Punkte: " + text(punkte) + " | Welle: " + text(welle)

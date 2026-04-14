# ============================================================
# td_procgen.moo — Tower Defense mit prozeduraler Karte
#
# Kompilieren: moo-compiler compile beispiele/td_procgen.moo -o beispiele/td_procgen
# Starten:     ./beispiele/td_procgen
#
# Steuerung:
#   1/2/3      - Turm-Typ waehlen (Pfeil/Kanone/Eis)
#   Mausklick  - Turm platzieren
#   N          - Naechste Welle starten
#   R          - Neue Karte generieren
#   Escape     - Beenden
#
# Features:
#   * Prozedural generierter Pfad (Random Walk)
#   * 3 Biome: Wald, Wueste, Schnee
#   * 3 Turm-Typen: Pfeil, Kanone, Eis
#   * Wellen mit steigender Schwierigkeit
# ============================================================

konstante TILE auf 32
konstante MAP_W auf 20
konstante MAP_H auf 15
konstante WIN_W auf 640
konstante WIN_H auf 528
konstante HUD_Y auf 480
konstante MAX_FEINDE auf 30
konstante MAX_TUERME auf 40
konstante MAX_SHOTS auf 50

# Tile: 0=baubar, 1=pfad, 2=start, 3=ziel, 4=turm
# Biome: 0=wald, 1=wueste, 2=schnee

# === PRNG ===
setze td_seed auf 42

funktion td_rand(max_val):
    setze td_seed auf (td_seed * 1103515245 + 12345) % 2147483648
    gib_zurück td_seed % max_val

# === Biom-Farben ===
funktion biom_gras(biom):
    wenn biom == 0:
        gib_zurück "#4CAF50"
    wenn biom == 1:
        gib_zurück "#F9A825"
    wenn biom == 2:
        gib_zurück "#E0E0E0"
    gib_zurück "#4CAF50"

funktion biom_gras2(biom):
    wenn biom == 0:
        gib_zurück "#66BB6A"
    wenn biom == 1:
        gib_zurück "#FDD835"
    wenn biom == 2:
        gib_zurück "#F5F5F5"
    gib_zurück "#66BB6A"

funktion biom_pfad(biom):
    wenn biom == 0:
        gib_zurück "#795548"
    wenn biom == 1:
        gib_zurück "#D4A056"
    wenn biom == 2:
        gib_zurück "#90A4AE"
    gib_zurück "#795548"

funktion biom_pfad2(biom):
    wenn biom == 0:
        gib_zurück "#8D6E63"
    wenn biom == 1:
        gib_zurück "#E6B76A"
    wenn biom == 2:
        gib_zurück "#B0BEC5"
    gib_zurück "#8D6E63"

# === Pfad generieren (Random Walk) ===
funktion gen_pfad():
    setze karte auf []
    setze pfad_x auf []
    setze pfad_y auf []
    setze idx auf 0
    solange idx < MAP_W * MAP_H:
        karte.hinzufügen(0)
        setze idx auf idx + 1

    # Start links, Mitte
    setze cur_x auf 0
    setze cur_y auf MAP_H / 2
    karte[cur_y * MAP_W + cur_x] = 2
    pfad_x.hinzufügen(cur_x)
    pfad_y.hinzufügen(cur_y)

    # Random Walk nach rechts
    solange cur_x < MAP_W - 1:
        setze richtung auf td_rand(10)
        wenn richtung < 6:
            # Rechts (60%)
            setze cur_x auf cur_x + 1
        sonst wenn richtung < 8:
            # Hoch (20%)
            wenn cur_y > 1:
                setze cur_y auf cur_y - 1
            sonst:
                setze cur_x auf cur_x + 1
        sonst:
            # Runter (20%)
            wenn cur_y < MAP_H - 2:
                setze cur_y auf cur_y + 1
            sonst:
                setze cur_x auf cur_x + 1

        setze tile_idx auf cur_y * MAP_W + cur_x
        wenn karte[tile_idx] == 0:
            karte[tile_idx] = 1
            pfad_x.hinzufügen(cur_x)
            pfad_y.hinzufügen(cur_y)

    # Ziel
    karte[cur_y * MAP_W + cur_x] = 3
    pfad_x.hinzufügen(cur_x)
    pfad_y.hinzufügen(cur_y)

    setze result auf {}
    result["karte"] = karte
    result["pfad_x"] = pfad_x
    result["pfad_y"] = pfad_y
    gib_zurück result

# === Karte zeichnen ===
funktion zeichne_karte(win, karte, biom):
    setze row auf 0
    solange row < MAP_H:
        setze col auf 0
        solange col < MAP_W:
            setze tile_val auf karte[row * MAP_W + col]
            setze dx auf col * TILE
            setze dy auf row * TILE

            wenn tile_val == 0 oder tile_val == 4:
                # Gras / Baubar
                zeichne_rechteck(win, dx, dy, TILE, TILE, biom_gras(biom))
                # Gras-Detail
                wenn (col + row) % 3 == 0:
                    zeichne_rechteck(win, dx + 8, dy + 12, 3, 8, biom_gras2(biom))
                wenn (col + row) % 5 == 1:
                    zeichne_rechteck(win, dx + 20, dy + 6, 2, 6, biom_gras2(biom))
            sonst:
                # Pfad / Start / Ziel
                zeichne_rechteck(win, dx, dy, TILE, TILE, biom_pfad(biom))
                zeichne_rechteck(win, dx + 2, dy + 2, TILE - 4, TILE - 4, biom_pfad2(biom))

            wenn tile_val == 2:
                # Start-Markierung
                zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 10, "#4CAF50")
                zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 6, "#66BB6A")
            wenn tile_val == 3:
                # Ziel-Markierung
                zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 10, "#F44336")
                zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 6, "#EF5350")

            setze col auf col + 1
        setze row auf row + 1

# === Tuerme ===
# Typ 0=Pfeil (schnell/schwach), 1=Kanone (langsam/stark), 2=Eis (verlangsamt)
setze turm_x auf []
setze turm_y auf []
setze turm_typ auf []
setze turm_cd auf []
setze turm_count auf 0

funktion turm_farbe(typ):
    wenn typ == 0:
        gib_zurück "#2196F3"
    wenn typ == 1:
        gib_zurück "#FF5722"
    wenn typ == 2:
        gib_zurück "#00BCD4"
    gib_zurück "#FFFFFF"

funktion turm_reichweite(typ):
    wenn typ == 0:
        gib_zurück 96
    wenn typ == 1:
        gib_zurück 80
    wenn typ == 2:
        gib_zurück 112
    gib_zurück 80

funktion turm_schaden(typ):
    wenn typ == 0:
        gib_zurück 8
    wenn typ == 1:
        gib_zurück 25
    wenn typ == 2:
        gib_zurück 3
    gib_zurück 5

funktion turm_feuerrate(typ):
    wenn typ == 0:
        gib_zurück 15
    wenn typ == 1:
        gib_zurück 45
    wenn typ == 2:
        gib_zurück 20
    gib_zurück 20

funktion turm_kosten(typ):
    wenn typ == 0:
        gib_zurück 25
    wenn typ == 1:
        gib_zurück 50
    wenn typ == 2:
        gib_zurück 35
    gib_zurück 30

funktion zeichne_tuerme(win):
    setze ti auf 0
    solange ti < turm_count:
        setze tx auf turm_x[ti] * TILE
        setze ty auf turm_y[ti] * TILE
        setze typ auf turm_typ[ti]
        setze farbe auf turm_farbe(typ)
        # Basis
        zeichne_rechteck(win, tx + 4, ty + 4, TILE - 8, TILE - 8, "#424242")
        zeichne_rechteck(win, tx + 6, ty + 6, TILE - 12, TILE - 12, "#616161")
        # Turm-Kopf
        zeichne_kreis(win, tx + TILE / 2, ty + TILE / 2, 10, farbe)
        wenn typ == 0:
            # Pfeil: spitz
            zeichne_rechteck(win, tx + TILE / 2, ty + 4, 3, 12, farbe)
        wenn typ == 1:
            # Kanone: breit
            zeichne_rechteck(win, tx + 6, ty + TILE / 2 - 3, 20, 6, "#BF360C")
        wenn typ == 2:
            # Eis: Kristall
            zeichne_kreis(win, tx + TILE / 2, ty + TILE / 2, 6, "#E0F7FA")
        setze ti auf ti + 1

# === Feinde ===
setze feind_px auf []
setze feind_py auf []
setze feind_pfad_idx auf []
setze feind_hp auf []
setze feind_max_hp auf []
setze feind_speed auf []
setze feind_slow auf []
setze feind_aktiv auf []
setze feind_count auf 0

funktion spawn_feind(hp_val, speed_val):
    wenn feind_count < MAX_FEINDE:
        feind_px.hinzufügen(0.0)
        feind_py.hinzufügen(0.0)
        feind_pfad_idx.hinzufügen(0)
        feind_hp.hinzufügen(hp_val)
        feind_max_hp.hinzufügen(hp_val)
        feind_speed.hinzufügen(speed_val)
        feind_slow.hinzufügen(0)
        feind_aktiv.hinzufügen(wahr)
        setze feind_count auf feind_count + 1

funktion update_feinde(pfad_x_arr, pfad_y_arr):
    setze fi auf 0
    solange fi < feind_count:
        wenn feind_aktiv[fi]:
            setze pi auf feind_pfad_idx[fi]
            wenn pi < länge(pfad_x_arr):
                setze ziel_x auf pfad_x_arr[pi] * TILE + TILE / 2
                setze ziel_y auf pfad_y_arr[pi] * TILE + TILE / 2
                setze diff_x auf ziel_x - feind_px[fi]
                setze diff_y auf ziel_y - feind_py[fi]
                setze dist auf wurzel(diff_x * diff_x + diff_y * diff_y)
                setze spd auf feind_speed[fi]
                wenn feind_slow[fi] > 0:
                    setze spd auf spd * 0.4
                    feind_slow[fi] = feind_slow[fi] - 1
                wenn dist < spd + 2:
                    feind_pfad_idx[fi] = pi + 1
                sonst:
                    feind_px[fi] = feind_px[fi] + diff_x / dist * spd
                    feind_py[fi] = feind_py[fi] + diff_y / dist * spd
            sonst:
                # Ziel erreicht
                feind_aktiv[fi] = falsch
                gib_zurück -1
        setze fi auf fi + 1
    gib_zurück 0

funktion zeichne_feinde(win):
    setze fi auf 0
    solange fi < feind_count:
        wenn feind_aktiv[fi]:
            setze fx auf feind_px[fi]
            setze fy auf feind_py[fi]
            # Koerper
            setze feind_farbe auf "#F44336"
            wenn feind_slow[fi] > 0:
                setze feind_farbe auf "#81D4FA"
            zeichne_kreis(win, fx, fy, 10, feind_farbe)
            zeichne_kreis(win, fx, fy, 6, "#D32F2F")
            # HP-Balken
            setze hp_ratio auf feind_hp[fi] / feind_max_hp[fi]
            zeichne_rechteck(win, fx - 10, fy - 16, 20, 3, "#424242")
            wenn hp_ratio > 0:
                zeichne_rechteck(win, fx - 10, fy - 16, 20 * hp_ratio, 3, "#4CAF50")
        setze fi auf fi + 1

# === Schüsse ===
setze shot_x auf []
setze shot_y auf []
setze shot_tx auf []
setze shot_ty auf []
setze shot_dmg auf []
setze shot_typ auf []
setze shot_aktiv auf []
setze shot_count auf 0

funktion add_shot(sx_val, sy_val, target_x, target_y, dmg, typ):
    wenn shot_count < MAX_SHOTS:
        shot_x.hinzufügen(sx_val)
        shot_y.hinzufügen(sy_val)
        shot_tx.hinzufügen(target_x)
        shot_ty.hinzufügen(target_y)
        shot_dmg.hinzufügen(dmg)
        shot_typ.hinzufügen(typ)
        shot_aktiv.hinzufügen(wahr)
        setze shot_count auf shot_count + 1

funktion update_shots():
    setze si auf 0
    solange si < shot_count:
        wenn shot_aktiv[si]:
            setze diff_x auf shot_tx[si] - shot_x[si]
            setze diff_y auf shot_ty[si] - shot_y[si]
            setze dist auf wurzel(diff_x * diff_x + diff_y * diff_y)
            wenn dist < 8:
                # Treffer — suche naechsten Feind
                setze fi auf 0
                solange fi < feind_count:
                    wenn feind_aktiv[fi]:
                        setze fdx auf feind_px[fi] - shot_x[si]
                        setze fdy auf feind_py[fi] - shot_y[si]
                        setze fdist auf wurzel(fdx * fdx + fdy * fdy)
                        wenn fdist < 16:
                            feind_hp[fi] = feind_hp[fi] - shot_dmg[si]
                            wenn shot_typ[si] == 2:
                                feind_slow[fi] = 60
                            wenn feind_hp[fi] <= 0:
                                feind_aktiv[fi] = falsch
                            setze fi auf feind_count
                    setze fi auf fi + 1
                shot_aktiv[si] = falsch
            sonst:
                shot_x[si] = shot_x[si] + diff_x / dist * 6
                shot_y[si] = shot_y[si] + diff_y / dist * 6
        setze si auf si + 1

funktion zeichne_shots(win):
    setze si auf 0
    solange si < shot_count:
        wenn shot_aktiv[si]:
            setze shot_farbe auf "#FFD700"
            wenn shot_typ[si] == 1:
                setze shot_farbe auf "#FF5722"
            wenn shot_typ[si] == 2:
                setze shot_farbe auf "#00BCD4"
            zeichne_kreis(win, shot_x[si], shot_y[si], 3, shot_farbe)
        setze si auf si + 1

# === Turm-Targeting ===
funktion tuerme_feuern():
    setze ti auf 0
    solange ti < turm_count:
        wenn turm_cd[ti] > 0:
            turm_cd[ti] = turm_cd[ti] - 1
        sonst:
            # Suche naechsten Feind in Reichweite
            setze tx auf turm_x[ti] * TILE + TILE / 2
            setze ty auf turm_y[ti] * TILE + TILE / 2
            setze rw auf turm_reichweite(turm_typ[ti])
            setze best_fi auf -1
            setze best_dist auf 99999.0
            setze fi auf 0
            solange fi < feind_count:
                wenn feind_aktiv[fi] und feind_hp[fi] > 0:
                    setze fdx auf feind_px[fi] - tx
                    setze fdy auf feind_py[fi] - ty
                    setze fdist auf wurzel(fdx * fdx + fdy * fdy)
                    wenn fdist < rw und fdist < best_dist:
                        setze best_fi auf fi
                        setze best_dist auf fdist
                setze fi auf fi + 1
            wenn best_fi >= 0:
                add_shot(tx, ty, feind_px[best_fi], feind_py[best_fi], turm_schaden(turm_typ[ti]), turm_typ[ti])
                turm_cd[ti] = turm_feuerrate(turm_typ[ti])
        setze ti auf ti + 1

# === HUD ===
funktion zeichne_td_hud(win, gold, leben, welle, sel_typ):
    zeichne_rechteck(win, 0, HUD_Y, WIN_W, WIN_H - HUD_Y, "#1A1A2E")
    # Gold
    setze gi auf 0
    solange gi < gold und gi < 30:
        zeichne_kreis(win, 16 + gi * 8, HUD_Y + 16, 3, "#FFD700")
        setze gi auf gi + 1
    # Leben
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, 16 + li * 18, HUD_Y + 34, 6, "#E91E63")
        setze li auf li + 1
    # Welle
    setze wi_idx auf 0
    solange wi_idx < welle:
        zeichne_kreis(win, WIN_W / 2 + wi_idx * 10, HUD_Y + 16, 3, "#90A4AE")
        setze wi_idx auf wi_idx + 1
    # Turm-Auswahl
    setze ti auf 0
    solange ti < 3:
        setze bx auf WIN_W - 150 + ti * 48
        setze by auf HUD_Y + 6
        setze sel_farbe auf "#424242"
        wenn ti == sel_typ:
            setze sel_farbe auf "#FFFFFF"
        zeichne_rechteck(win, bx, by, 40, 36, sel_farbe)
        zeichne_kreis(win, bx + 20, by + 18, 12, turm_farbe(ti))
        setze ti auf ti + 1

# === Hauptprogramm ===
zeige "=== moo Tower Defense (Prozedural) ==="
zeige "1/2/3=Turm, Klick=Platzieren, N=Welle, R=Neu"

setze win auf fenster_erstelle("moo TD Procgen", WIN_W, WIN_H)
setze td_seed auf zeit_ms() % 99991

# Generiere Karte
setze map_data auf gen_pfad()
setze karte auf map_data["karte"]
setze pfad_x_arr auf map_data["pfad_x"]
setze pfad_y_arr auf map_data["pfad_y"]
setze biom auf td_rand(3)

# State
setze gold auf 100
setze leben auf 10
setze welle auf 0
setze welle_aktiv auf falsch
setze spawn_cd auf 0
setze spawned auf 0
setze sel_typ auf 0
setze klick_cd auf 0

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Turm-Auswahl
    wenn taste_gedrückt("1"):
        setze sel_typ auf 0
    wenn taste_gedrückt("2"):
        setze sel_typ auf 1
    wenn taste_gedrückt("3"):
        setze sel_typ auf 2

    # Neue Karte
    wenn taste_gedrückt("r"):
        setze td_seed auf zeit_ms() % 99991
        setze map_data auf gen_pfad()
        setze karte auf map_data["karte"]
        setze pfad_x_arr auf map_data["pfad_x"]
        setze pfad_y_arr auf map_data["pfad_y"]
        setze biom auf td_rand(3)
        setze gold auf 100
        setze leben auf 10
        setze welle auf 0
        setze welle_aktiv auf falsch
        setze turm_count auf 0
        setze feind_count auf 0
        setze shot_count auf 0
        setze turm_x auf []
        setze turm_y auf []
        setze turm_typ auf []
        setze turm_cd auf []

    # Neue Welle
    wenn taste_gedrückt("n") und welle_aktiv == falsch:
        setze welle auf welle + 1
        setze welle_aktiv auf wahr
        setze spawned auf 0
        setze spawn_cd auf 0
        # Reset Feinde
        setze feind_count auf 0
        setze feind_px auf []
        setze feind_py auf []
        setze feind_pfad_idx auf []
        setze feind_hp auf []
        setze feind_max_hp auf []
        setze feind_speed auf []
        setze feind_slow auf []
        setze feind_aktiv auf []
        # Reset Shots
        setze shot_count auf 0
        setze shot_x auf []
        setze shot_y auf []
        setze shot_tx auf []
        setze shot_ty auf []
        setze shot_dmg auf []
        setze shot_typ auf []
        setze shot_aktiv auf []

    # Turm platzieren
    wenn klick_cd > 0:
        setze klick_cd auf klick_cd - 1
    wenn maus_gedrückt(win) und klick_cd == 0:
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        setze tcol auf boden(mx / TILE)
        setze trow auf boden(my / TILE)
        wenn tcol >= 0 und tcol < MAP_W und trow >= 0 und trow < MAP_H:
            setze tidx auf trow * MAP_W + tcol
            wenn karte[tidx] == 0 und gold >= turm_kosten(sel_typ):
                karte[tidx] = 4
                turm_x.hinzufügen(tcol)
                turm_y.hinzufügen(trow)
                turm_typ.hinzufügen(sel_typ)
                turm_cd.hinzufügen(0)
                setze turm_count auf turm_count + 1
                setze gold auf gold - turm_kosten(sel_typ)
        setze klick_cd auf 10

    # Welle: Feinde spawnen
    wenn welle_aktiv:
        setze max_spawn auf 5 + welle * 3
        wenn spawned < max_spawn:
            setze spawn_cd auf spawn_cd + 1
            wenn spawn_cd >= 20:
                setze spawn_cd auf 0
                setze hp_val auf 30 + welle * 15
                setze spd_val auf 1.5 + welle * 0.1
                spawn_feind(hp_val, spd_val)
                # Startposition setzen
                setze last_idx auf feind_count - 1
                wenn länge(pfad_x_arr) > 0:
                    feind_px[last_idx] = pfad_x_arr[0] * TILE + TILE / 2
                    feind_py[last_idx] = pfad_y_arr[0] * TILE + TILE / 2
                    feind_pfad_idx[last_idx] = 1
                setze spawned auf spawned + 1

        # Welle fertig?
        wenn spawned >= max_spawn:
            setze alle_tot auf wahr
            setze fi auf 0
            solange fi < feind_count:
                wenn feind_aktiv[fi]:
                    setze alle_tot auf falsch
                setze fi auf fi + 1
            wenn alle_tot:
                setze welle_aktiv auf falsch
                setze gold auf gold + 20 + welle * 5

    # Feinde bewegen
    setze result auf update_feinde(pfad_x_arr, pfad_y_arr)
    wenn result == -1:
        setze leben auf leben - 1
        wenn leben <= 0:
            zeige "GAME OVER! Welle: " + text(welle)

    # Tuerme feuern
    tuerme_feuern()
    update_shots()

    # Tote Feinde = Gold
    setze fi auf 0
    solange fi < feind_count:
        wenn feind_aktiv[fi] == falsch und feind_hp[fi] <= 0:
            setze gold auf gold + 5
            feind_hp[fi] = -999
        setze fi auf fi + 1

    # === Zeichnen ===
    fenster_löschen(win, "#1A1A2E")
    zeichne_karte(win, karte, biom)
    zeichne_tuerme(win)
    zeichne_feinde(win)
    zeichne_shots(win)

    # Hover: Turm-Vorschau
    setze mx auf maus_x(win)
    setze my auf maus_y(win)
    setze hcol auf boden(mx / TILE)
    setze hrow auf boden(my / TILE)
    wenn hcol >= 0 und hcol < MAP_W und hrow >= 0 und hrow < MAP_H:
        wenn karte[hrow * MAP_W + hcol] == 0:
            setze hx auf hcol * TILE
            setze hy auf hrow * TILE
            zeichne_rechteck(win, hx, hy, TILE, TILE, "#FFFFFF")
            zeichne_kreis(win, hx + TILE / 2, hy + TILE / 2, 10, turm_farbe(sel_typ))

    zeichne_td_hud(win, gold, leben, welle, sel_typ)
    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Tower Defense beendet. Welle: " + text(welle)

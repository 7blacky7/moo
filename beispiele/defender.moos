# ============================================================
# moo Defender — Horizontaler Scrolling-Shooter
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/defender.moo -o beispiele/defender
#   ./beispiele/defender
#
# Bedienung:
#   W/S - Hoch/Runter
#   A/D - Links/Rechts (Richtung wechseln)
#   Leertaste - Schiessen
#   Escape - Beenden
#
# Features:
#   * Horizontales Scrolling (Wrap-Around Welt)
#   * Gegner entfuehren Menschen vom Boden
#   * Rette Menschen bevor sie verschleppt werden
#   * Radar/Minimap oben
#   * Mehrere Gegner-Typen
#   * Score + Wellen
# ============================================================

setze BREITE auf 800
setze HOEHE auf 500
setze WELT_BREITE auf 3200
setze BODEN_Y auf 420
setze MAX_SCHUESSE auf 10
setze MAX_GEGNER auf 25
setze MAX_MENSCHEN auf 10
setze MAX_EXPL auf 15

setze rng_state auf 88888
funktion rng():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    wenn rng_state < 0:
        setze rng_state auf 0 - rng_state
    gib_zurück rng_state

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

# Wrap-Distanz
funktion wrap_dist(ax, bx):
    setze dx auf bx - ax
    wenn dx > WELT_BREITE / 2:
        setze dx auf dx - WELT_BREITE
    wenn dx < 0 - WELT_BREITE / 2:
        setze dx auf dx + WELT_BREITE
    gib_zurück dx

# === SPIELER ===
setze spieler_x auf 400.0
setze spieler_y auf 200.0
setze spieler_richtung auf 1
setze spieler_speed auf 5.0
setze leben auf 3
setze score auf 0
setze feuer_cd auf 0
setze unverwundbar auf 0

# Schuesse
setze sch_x auf []
setze sch_y auf []
setze sch_dx auf []
setze sch_aktiv auf []
setze si auf 0
solange si < MAX_SCHUESSE:
    sch_x.hinzufügen(0.0)
    sch_y.hinzufügen(0.0)
    sch_dx.hinzufügen(0.0)
    sch_aktiv.hinzufügen(falsch)
    setze si auf si + 1

# Gegner
setze geg_x auf []
setze geg_y auf []
setze geg_vy auf []
setze geg_typ auf []
setze geg_aktiv auf []
setze geg_ziel auf []
setze gi auf 0
solange gi < MAX_GEGNER:
    geg_x.hinzufügen(0.0)
    geg_y.hinzufügen(0.0)
    geg_vy.hinzufügen(0.0)
    geg_typ.hinzufügen(0)
    geg_aktiv.hinzufügen(falsch)
    geg_ziel.hinzufügen(-1)
    setze gi auf gi + 1

# Menschen
setze mensch_x auf []
setze mensch_y auf []
setze mensch_aktiv auf []
setze mensch_entfuehrt auf []
setze mi auf 0
solange mi < MAX_MENSCHEN:
    mensch_x.hinzufügen(rng() % WELT_BREITE * 1.0)
    mensch_y.hinzufügen(BODEN_Y * 1.0)
    mensch_aktiv.hinzufügen(wahr)
    mensch_entfuehrt.hinzufügen(-1)
    setze mi auf mi + 1

# Explosionen
setze expl_x auf []
setze expl_y auf []
setze expl_timer auf []
setze expl_aktiv auf []
setze ei auf 0
solange ei < MAX_EXPL:
    expl_x.hinzufügen(0.0)
    expl_y.hinzufügen(0.0)
    expl_timer.hinzufügen(0)
    expl_aktiv.hinzufügen(falsch)
    setze ei auf ei + 1

# Terrain (Huegel-Profil fuer Boden)
setze terrain auf []
setze ti auf 0
solange ti < 80:
    setze th auf BODEN_Y + (rng() % 30)
    terrain.hinzufügen(th)
    setze ti auf ti + 1

setze welle auf 1
setze welle_timer auf 0
setze kamera_x auf spieler_x - BREITE / 2

funktion explosion(x, y):
    setze ei auf 0
    solange ei < MAX_EXPL:
        wenn nicht expl_aktiv[ei]:
            expl_x[ei] = x
            expl_y[ei] = y
            expl_timer[ei] = 15
            expl_aktiv[ei] = wahr
            gib_zurück nichts
        setze ei auf ei + 1

funktion gegner_spawn(x, y, typ):
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn nicht geg_aktiv[gi]:
            geg_x[gi] = x
            geg_y[gi] = y
            geg_vy[gi] = 0.0
            geg_typ[gi] = typ
            geg_aktiv[gi] = wahr
            geg_ziel[gi] = -1
            gib_zurück nichts
        setze gi auf gi + 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Defender", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn leben <= 0:
        stopp

    # === INPUT ===
    wenn taste_gedrückt("d"):
        setze spieler_x auf spieler_x + spieler_speed
        setze spieler_richtung auf 1
    wenn taste_gedrückt("a"):
        setze spieler_x auf spieler_x - spieler_speed
        setze spieler_richtung auf -1
    wenn taste_gedrückt("w"):
        setze spieler_y auf spieler_y - 3
    wenn taste_gedrückt("s"):
        setze spieler_y auf spieler_y + 3

    wenn spieler_y < 30:
        setze spieler_y auf 30.0
    wenn spieler_y > BODEN_Y - 20:
        setze spieler_y auf (BODEN_Y - 20) * 1.0

    # Wrap
    wenn spieler_x < 0:
        setze spieler_x auf spieler_x + WELT_BREITE
    wenn spieler_x >= WELT_BREITE:
        setze spieler_x auf spieler_x - WELT_BREITE

    # Feuern
    wenn taste_gedrückt("leertaste") und feuer_cd <= 0:
        setze si auf 0
        solange si < MAX_SCHUESSE:
            wenn nicht sch_aktiv[si]:
                sch_x[si] = spieler_x
                sch_y[si] = spieler_y
                sch_dx[si] = spieler_richtung * 10.0
                sch_aktiv[si] = wahr
                setze feuer_cd auf 6
                setze si auf MAX_SCHUESSE
            setze si auf si + 1
    wenn feuer_cd > 0:
        setze feuer_cd auf feuer_cd - 1
    wenn unverwundbar > 0:
        setze unverwundbar auf unverwundbar - 1

    # Kamera folgt Spieler
    setze kamera_x auf spieler_x - BREITE / 2

    # === WELLEN ===
    setze welle_timer auf welle_timer + 1
    wenn welle_timer > 180:
        setze welle_timer auf 0
        setze spawn_count auf 3 + welle * 2
        wenn spawn_count > 12:
            setze spawn_count auf 12
        setze spawn_i auf 0
        solange spawn_i < spawn_count:
            setze sx auf (spieler_x + 500 + rng() % 1500) % WELT_BREITE
            setze sy auf 20 + rng() % 80
            setze typ auf rng() % 3
            gegner_spawn(sx * 1.0, sy * 1.0, typ)
            setze spawn_i auf spawn_i + 1
        setze welle auf welle + 1

    # === SCHUESSE ===
    setze si auf 0
    solange si < MAX_SCHUESSE:
        wenn sch_aktiv[si]:
            sch_x[si] = sch_x[si] + sch_dx[si]
            wenn sch_x[si] < 0:
                sch_x[si] = sch_x[si] + WELT_BREITE
            wenn sch_x[si] >= WELT_BREITE:
                sch_x[si] = sch_x[si] - WELT_BREITE
            # Reichweite (max 400px)
            setze dist auf abs_wert(wrap_dist(spieler_x, sch_x[si]))
            wenn dist > 400:
                sch_aktiv[si] = falsch
        setze si auf si + 1

    # === GEGNER UPDATE ===
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn geg_aktiv[gi]:
            wenn geg_typ[gi] == 0:
                # Lander: fliegt runter, entfuehrt Menschen
                wenn geg_ziel[gi] < 0:
                    # Suche naechsten Menschen
                    setze best auf -1
                    setze best_dist auf 9999.0
                    setze mi auf 0
                    solange mi < MAX_MENSCHEN:
                        wenn mensch_aktiv[mi] und mensch_entfuehrt[mi] < 0:
                            setze dist auf abs_wert(wrap_dist(geg_x[gi], mensch_x[mi]))
                            wenn dist < best_dist:
                                setze best auf mi
                                setze best_dist auf dist
                        setze mi auf mi + 1
                    geg_ziel[gi] = best
                # Fliege zum Ziel
                wenn geg_ziel[gi] >= 0:
                    setze ziel_mensch auf geg_ziel[gi]
                    wenn mensch_aktiv[ziel_mensch]:
                        setze dx auf wrap_dist(geg_x[gi], mensch_x[ziel_mensch])
                        wenn abs_wert(dx) > 3:
                            wenn dx > 0:
                                geg_x[gi] = geg_x[gi] + 1.5
                            sonst:
                                geg_x[gi] = geg_x[gi] - 1.5
                        # Runter zum Menschen
                        wenn geg_y[gi] < BODEN_Y - 10:
                            geg_y[gi] = geg_y[gi] + 1
                        sonst:
                            # Entfuehren!
                            wenn abs_wert(dx) < 10:
                                mensch_entfuehrt[ziel_mensch] = gi
                                geg_vy[gi] = -1.5
                    sonst:
                        geg_ziel[gi] = -1
                # Mensch nach oben tragen
                setze mi auf 0
                solange mi < MAX_MENSCHEN:
                    wenn mensch_entfuehrt[mi] == gi:
                        geg_y[gi] = geg_y[gi] + geg_vy[gi]
                        mensch_x[mi] = geg_x[gi]
                        mensch_y[mi] = geg_y[gi] + 15
                        wenn geg_y[gi] < -20:
                            mensch_aktiv[mi] = falsch
                            geg_aktiv[gi] = falsch
                    setze mi auf mi + 1

            wenn geg_typ[gi] == 1:
                # Bomber: fliegt horizontal, schiesst
                geg_x[gi] = geg_x[gi] + 2
                geg_y[gi] = geg_y[gi] + sinus(geg_x[gi] * 0.02) * 0.5

            wenn geg_typ[gi] == 2:
                # Schwirrer: kreist um Spieler
                setze dx auf wrap_dist(geg_x[gi], spieler_x)
                setze dy auf spieler_y - geg_y[gi]
                wenn abs_wert(dx) > 5:
                    wenn dx > 0:
                        geg_x[gi] = geg_x[gi] + 2
                    sonst:
                        geg_x[gi] = geg_x[gi] - 2
                wenn abs_wert(dy) > 5:
                    wenn dy > 0:
                        geg_y[gi] = geg_y[gi] + 1.5
                    sonst:
                        geg_y[gi] = geg_y[gi] - 1.5

            # Wrap
            wenn geg_x[gi] < 0:
                geg_x[gi] = geg_x[gi] + WELT_BREITE
            wenn geg_x[gi] >= WELT_BREITE:
                geg_x[gi] = geg_x[gi] - WELT_BREITE

            # Kollision mit Spieler
            wenn unverwundbar == 0:
                setze dx auf abs_wert(wrap_dist(spieler_x, geg_x[gi]))
                wenn dx < 20 und abs_wert(spieler_y - geg_y[gi]) < 15:
                    geg_aktiv[gi] = falsch
                    explosion(geg_x[gi], geg_y[gi])
                    setze leben auf leben - 1
                    setze unverwundbar auf 60
                    # Mensch freigeben
                    setze mi auf 0
                    solange mi < MAX_MENSCHEN:
                        wenn mensch_entfuehrt[mi] == gi:
                            mensch_entfuehrt[mi] = -1
                        setze mi auf mi + 1
        setze gi auf gi + 1

    # === TREFFER ===
    setze si auf 0
    solange si < MAX_SCHUESSE:
        wenn sch_aktiv[si]:
            setze gi auf 0
            solange gi < MAX_GEGNER:
                wenn geg_aktiv[gi]:
                    setze dx auf abs_wert(wrap_dist(sch_x[si], geg_x[gi]))
                    wenn dx < 15 und abs_wert(sch_y[si] - geg_y[gi]) < 15:
                        sch_aktiv[si] = falsch
                        geg_aktiv[gi] = falsch
                        explosion(geg_x[gi], geg_y[gi])
                        setze score auf score + (geg_typ[gi] + 1) * 150
                        # Mensch freigeben
                        setze mi auf 0
                        solange mi < MAX_MENSCHEN:
                            wenn mensch_entfuehrt[mi] == gi:
                                mensch_entfuehrt[mi] = -1
                                mensch_y[mi] = geg_y[gi] + 15
                            setze mi auf mi + 1
                setze gi auf gi + 1
        setze si auf si + 1

    # Menschen fallen (wenn Entfuehrer zerstoert)
    setze mi auf 0
    solange mi < MAX_MENSCHEN:
        wenn mensch_aktiv[mi] und mensch_entfuehrt[mi] < 0 und mensch_y[mi] < BODEN_Y:
            mensch_y[mi] = mensch_y[mi] + 2
            wenn mensch_y[mi] >= BODEN_Y:
                mensch_y[mi] = BODEN_Y * 1.0
        setze mi auf mi + 1

    # Explosionen
    setze ei auf 0
    solange ei < MAX_EXPL:
        wenn expl_aktiv[ei]:
            expl_timer[ei] = expl_timer[ei] - 1
            wenn expl_timer[ei] <= 0:
                expl_aktiv[ei] = falsch
        setze ei auf ei + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#000022")

    # Sterne
    setze star_i auf 0
    solange star_i < 40:
        setze sx auf ((star_i * 347 + 89) % WELT_BREITE) - kamera_x
        wenn sx < 0:
            setze sx auf sx + WELT_BREITE
        wenn sx >= WELT_BREITE:
            setze sx auf sx - WELT_BREITE
        wenn sx >= 0 und sx < BREITE:
            setze sy auf (star_i * 571 + 23) % (BODEN_Y - 50)
            zeichne_pixel(win, sx, sy, "#FFFFFF")
        setze star_i auf star_i + 1

    # Terrain/Boden
    setze bx auf 0
    solange bx < BREITE:
        setze wx auf boden((bx + kamera_x) / 40) % 80
        wenn wx < 0:
            setze wx auf wx + 80
        setze th auf terrain[wx]
        zeichne_rechteck(win, bx, th, 10, HOEHE - th, "#1B5E20")
        zeichne_rechteck(win, bx, th, 10, 3, "#4CAF50")
        setze bx auf bx + 10

    # Menschen
    setze mi auf 0
    solange mi < MAX_MENSCHEN:
        wenn mensch_aktiv[mi]:
            setze dx auf mensch_x[mi] - kamera_x
            wenn dx < -WELT_BREITE / 2:
                setze dx auf dx + WELT_BREITE
            wenn dx > WELT_BREITE / 2:
                setze dx auf dx - WELT_BREITE
            wenn dx > -20 und dx < BREITE + 20:
                zeichne_rechteck(win, dx - 3, mensch_y[mi] - 10, 6, 10, "#E91E63")
                zeichne_kreis(win, dx, mensch_y[mi] - 13, 3, "#FFCC80")
        setze mi auf mi + 1

    # Gegner
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn geg_aktiv[gi]:
            setze dx auf geg_x[gi] - kamera_x
            wenn dx < -WELT_BREITE / 2:
                setze dx auf dx + WELT_BREITE
            wenn dx > WELT_BREITE / 2:
                setze dx auf dx - WELT_BREITE
            wenn dx > -30 und dx < BREITE + 30:
                wenn geg_typ[gi] == 0:
                    zeichne_rechteck(win, dx - 10, geg_y[gi] - 8, 20, 16, "#4CAF50")
                    zeichne_rechteck(win, dx - 6, geg_y[gi] + 4, 4, 8, "#388E3C")
                    zeichne_rechteck(win, dx + 2, geg_y[gi] + 4, 4, 8, "#388E3C")
                wenn geg_typ[gi] == 1:
                    zeichne_rechteck(win, dx - 12, geg_y[gi] - 6, 24, 12, "#FF9800")
                    zeichne_rechteck(win, dx - 4, geg_y[gi] - 10, 8, 6, "#F57C00")
                wenn geg_typ[gi] == 2:
                    zeichne_kreis(win, dx, geg_y[gi], 10, "#9C27B0")
                    zeichne_kreis(win, dx, geg_y[gi], 5, "#CE93D8")
        setze gi auf gi + 1

    # Schuesse
    setze si auf 0
    solange si < MAX_SCHUESSE:
        wenn sch_aktiv[si]:
            setze dx auf sch_x[si] - kamera_x
            wenn dx < -WELT_BREITE / 2:
                setze dx auf dx + WELT_BREITE
            wenn dx > WELT_BREITE / 2:
                setze dx auf dx - WELT_BREITE
            wenn dx > -10 und dx < BREITE + 10:
                zeichne_rechteck(win, dx - 6, sch_y[si] - 1, 12, 2, "#00E5FF")
        setze si auf si + 1

    # Explosionen
    setze ei auf 0
    solange ei < MAX_EXPL:
        wenn expl_aktiv[ei]:
            setze dx auf expl_x[ei] - kamera_x
            wenn dx < -WELT_BREITE / 2:
                setze dx auf dx + WELT_BREITE
            wenn dx > WELT_BREITE / 2:
                setze dx auf dx - WELT_BREITE
            setze er auf 15 - expl_timer[ei]
            zeichne_kreis(win, dx, expl_y[ei], er, "#FF9800")
            wenn er > 5:
                zeichne_kreis(win, dx, expl_y[ei], er - 5, "#FFEB3B")
        setze ei auf ei + 1

    # Spieler
    wenn unverwundbar == 0 oder (unverwundbar / 3) % 2 == 0:
        setze px auf BREITE / 2
        wenn spieler_richtung == 1:
            zeichne_rechteck(win, px - 12, spieler_y - 6, 24, 12, "#42A5F5")
            zeichne_rechteck(win, px + 10, spieler_y - 3, 8, 6, "#64B5F6")
            zeichne_rechteck(win, px - 6, spieler_y + 6, 8, 4, "#FF9800")
        sonst:
            zeichne_rechteck(win, px - 12, spieler_y - 6, 24, 12, "#42A5F5")
            zeichne_rechteck(win, px - 18, spieler_y - 3, 8, 6, "#64B5F6")
            zeichne_rechteck(win, px - 2, spieler_y + 6, 8, 4, "#FF9800")

    # Radar/Minimap
    zeichne_rechteck(win, 0, 0, BREITE, 25, "#000044")
    # Spieler auf Radar
    setze radar_x auf (spieler_x * BREITE) / WELT_BREITE
    zeichne_rechteck(win, radar_x - 2, 8, 4, 8, "#42A5F5")
    # Gegner auf Radar
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn geg_aktiv[gi]:
            setze rx auf (geg_x[gi] * BREITE) / WELT_BREITE
            zeichne_pixel(win, rx, 12, "#F44336")
        setze gi auf gi + 1
    # Menschen auf Radar
    setze mi auf 0
    solange mi < MAX_MENSCHEN:
        wenn mensch_aktiv[mi]:
            setze rx auf (mensch_x[mi] * BREITE) / WELT_BREITE
            zeichne_pixel(win, rx, 20, "#E91E63")
        setze mi auf mi + 1

    # HUD unten
    zeichne_rechteck(win, 0, HOEHE - 25, BREITE, 25, "#000044")
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, 15 + li * 20, HOEHE - 12, 6, "#42A5F5")
        setze li auf li + 1
    # Gerettete Menschen
    setze alive auf 0
    setze mi auf 0
    solange mi < MAX_MENSCHEN:
        wenn mensch_aktiv[mi]:
            setze alive auf alive + 1
        setze mi auf mi + 1
    setze mi auf 0
    solange mi < alive und mi < 10:
        zeichne_kreis(win, 100 + mi * 14, HOEHE - 12, 4, "#E91E63")
        setze mi auf mi + 1
    # Score
    setze si auf 0
    solange si < score / 200 und si < 20:
        zeichne_kreis(win, BREITE - 200 + si * 14, HOEHE - 12, 4, "#FFD700")
        setze si auf si + 1
    # Welle
    setze wave_i auf 0
    solange wave_i < welle und wave_i < 10:
        zeichne_rechteck(win, BREITE / 2 - 45 + wave_i * 10, HOEHE - 18, 8, 8, "#FF9800")
        setze wave_i auf wave_i + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "GAME OVER! Score: " + text(score) + " | Welle: " + text(welle)

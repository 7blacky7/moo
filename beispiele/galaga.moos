# ============================================================
# moo Galaga — Klassischer Arcade Shoot-em-up
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/galaga.moo -o beispiele/galaga
#   ./beispiele/galaga
#
# Bedienung:
#   Links/Rechts oder A/D - Schiff bewegen
#   Leertaste - Schiessen
#   Escape - Beenden
#
# Features:
#   * Gegner in Formation (5x8 Grid)
#   * Gegner fliegen Angriffs-Muster (Dive-Bombing)
#   * 3 Gegner-Typen (Biene, Schmetterling, Boss)
#   * Formation schwingt seitlich
#   * Bonus-Runden (nur Gegner, keine Schuesse)
#   * Score + Level + Leben
# ============================================================

setze BREITE auf 500
setze HOEHE auf 650
setze MAX_SCHUESSE auf 5
setze MAX_GEGNER auf 40
setze MAX_FEIND_SCHUESSE auf 10
setze MAX_EXPLOSIONEN auf 15

setze rng_state auf 33333
funktion rng():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    wenn rng_state < 0:
        setze rng_state auf 0 - rng_state
    gib_zurück rng_state

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

# === SPIELER ===
setze spieler_x auf BREITE / 2.0
setze spieler_y auf HOEHE - 50
setze leben auf 3
setze score auf 0
setze level auf 1
setze feuer_cooldown auf 0
setze unverwundbar auf 0

# === SCHUESSE ===
setze sch_x auf []
setze sch_y auf []
setze sch_aktiv auf []
setze si auf 0
solange si < MAX_SCHUESSE:
    sch_x.hinzufügen(0.0)
    sch_y.hinzufügen(0.0)
    sch_aktiv.hinzufügen(falsch)
    setze si auf si + 1

# === GEGNER ===
setze geg_x auf []
setze geg_y auf []
setze geg_typ auf []
setze geg_hp auf []
setze geg_aktiv auf []
setze geg_diving auf []
setze geg_div_x auf []
setze geg_div_y auf []
setze geg_home_x auf []
setze geg_home_y auf []
setze gi auf 0
solange gi < MAX_GEGNER:
    geg_x.hinzufügen(0.0)
    geg_y.hinzufügen(0.0)
    geg_typ.hinzufügen(0)
    geg_hp.hinzufügen(0)
    geg_aktiv.hinzufügen(falsch)
    geg_diving.hinzufügen(falsch)
    geg_div_x.hinzufügen(0.0)
    geg_div_y.hinzufügen(0.0)
    geg_home_x.hinzufügen(0.0)
    geg_home_y.hinzufügen(0.0)
    setze gi auf gi + 1

# === FEIND-SCHUESSE ===
setze fsch_x auf []
setze fsch_y auf []
setze fsch_aktiv auf []
setze fi auf 0
solange fi < MAX_FEIND_SCHUESSE:
    fsch_x.hinzufügen(0.0)
    fsch_y.hinzufügen(0.0)
    fsch_aktiv.hinzufügen(falsch)
    setze fi auf fi + 1

# === EXPLOSIONEN ===
setze expl_x auf []
setze expl_y auf []
setze expl_timer auf []
setze expl_aktiv auf []
setze ei auf 0
solange ei < MAX_EXPLOSIONEN:
    expl_x.hinzufügen(0.0)
    expl_y.hinzufügen(0.0)
    expl_timer.hinzufügen(0)
    expl_aktiv.hinzufügen(falsch)
    setze ei auf ei + 1

# Formation
setze form_offset_x auf 0.0
setze form_dir auf 1.0
setze form_speed auf 0.5
setze dive_timer auf 0
setze gegner_total auf 0

funktion explosion_start(x, y):
    setze ei auf 0
    solange ei < MAX_EXPLOSIONEN:
        wenn nicht expl_aktiv[ei]:
            expl_x[ei] = x
            expl_y[ei] = y
            expl_timer[ei] = 12
            expl_aktiv[ei] = wahr
            gib_zurück nichts
        setze ei auf ei + 1

funktion formation_bauen():
    setze gegner_total auf 0
    # 5 Reihen
    setze ry auf 0
    solange ry < 5:
        setze rx auf 0
        setze cols auf 8
        solange rx < cols und gegner_total < MAX_GEGNER:
            setze gi auf gegner_total
            setze hx auf 80 + rx * 45.0
            setze hy auf 60 + ry * 35.0
            geg_home_x[gi] = hx
            geg_home_y[gi] = hy
            geg_x[gi] = hx
            geg_y[gi] = -30 - ry * 20.0
            geg_aktiv[gi] = wahr
            geg_diving[gi] = falsch
            # Typ basierend auf Reihe
            wenn ry == 0:
                geg_typ[gi] = 2
                geg_hp[gi] = 2
            wenn ry == 1 oder ry == 2:
                geg_typ[gi] = 1
                geg_hp[gi] = 1
            wenn ry >= 3:
                geg_typ[gi] = 0
                geg_hp[gi] = 1
            setze gegner_total auf gegner_total + 1
            setze rx auf rx + 1
        setze ry auf ry + 1

formation_bauen()

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Galaga", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn leben <= 0:
        stopp

    # === INPUT ===
    wenn taste_gedrückt("links") oder taste_gedrückt("a"):
        setze spieler_x auf spieler_x - 5
    wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
        setze spieler_x auf spieler_x + 5
    wenn spieler_x < 20:
        setze spieler_x auf 20.0
    wenn spieler_x > BREITE - 20:
        setze spieler_x auf (BREITE - 20) * 1.0

    wenn taste_gedrückt("leertaste") und feuer_cooldown <= 0:
        setze si auf 0
        solange si < MAX_SCHUESSE:
            wenn nicht sch_aktiv[si]:
                sch_x[si] = spieler_x
                sch_y[si] = spieler_y - 15.0
                sch_aktiv[si] = wahr
                setze feuer_cooldown auf 10
                setze si auf MAX_SCHUESSE
            setze si auf si + 1
    wenn feuer_cooldown > 0:
        setze feuer_cooldown auf feuer_cooldown - 1

    wenn unverwundbar > 0:
        setze unverwundbar auf unverwundbar - 1

    # === FORMATION BEWEGEN ===
    setze form_offset_x auf form_offset_x + form_dir * form_speed
    wenn form_offset_x > 40:
        setze form_dir auf -1.0
    wenn form_offset_x < -40:
        setze form_dir auf 1.0

    # Gegner zur Formation-Position bewegen
    setze lebende auf 0
    setze gi auf 0
    solange gi < gegner_total:
        wenn geg_aktiv[gi]:
            setze lebende auf lebende + 1
            wenn nicht geg_diving[gi]:
                # Sanft zur Home-Position + Offset
                setze ziel_x auf geg_home_x[gi] + form_offset_x
                setze ziel_y auf geg_home_y[gi]
                geg_x[gi] = geg_x[gi] + (ziel_x - geg_x[gi]) * 0.05
                geg_y[gi] = geg_y[gi] + (ziel_y - geg_y[gi]) * 0.05
            sonst:
                # Dive-Angriff
                geg_x[gi] = geg_x[gi] + geg_div_x[gi]
                geg_y[gi] = geg_y[gi] + geg_div_y[gi]
                # Zurueck zur Formation wenn unter Bildschirm
                wenn geg_y[gi] > HOEHE + 30:
                    geg_y[gi] = -30.0
                    geg_diving[gi] = falsch
        setze gi auf gi + 1

    # Alle besiegt → neues Level
    wenn lebende == 0:
        setze level auf level + 1
        setze form_speed auf 0.5 + level * 0.2
        formation_bauen()

    # Dive-Angriff starten (zufaellig)
    setze dive_timer auf dive_timer + 1
    wenn dive_timer > 40 - level * 2:
        setze dive_timer auf 0
        # Zufaelligen Gegner waehlen
        setze tries auf 0
        solange tries < 10:
            setze gi auf rng() % gegner_total
            wenn geg_aktiv[gi] und nicht geg_diving[gi]:
                geg_diving[gi] = wahr
                # Richtung zum Spieler
                setze dx auf spieler_x - geg_x[gi]
                setze dy auf spieler_y - geg_y[gi]
                setze dist auf wurzel(dx * dx + dy * dy)
                wenn dist > 0:
                    geg_div_x[gi] = dx / dist * (3.0 + level * 0.3)
                    geg_div_y[gi] = dy / dist * (3.0 + level * 0.3)
                # Feind-Schuss
                setze fi auf 0
                solange fi < MAX_FEIND_SCHUESSE:
                    wenn nicht fsch_aktiv[fi]:
                        fsch_x[fi] = geg_x[gi]
                        fsch_y[fi] = geg_y[gi]
                        fsch_aktiv[fi] = wahr
                        setze fi auf MAX_FEIND_SCHUESSE
                    setze fi auf fi + 1
                setze tries auf 10
            setze tries auf tries + 1

    # === SCHUESSE BEWEGEN ===
    setze si auf 0
    solange si < MAX_SCHUESSE:
        wenn sch_aktiv[si]:
            sch_y[si] = sch_y[si] - 8
            wenn sch_y[si] < -10:
                sch_aktiv[si] = falsch
        setze si auf si + 1

    # === FEIND-SCHUESSE ===
    setze fi auf 0
    solange fi < MAX_FEIND_SCHUESSE:
        wenn fsch_aktiv[fi]:
            fsch_y[fi] = fsch_y[fi] + 4
            wenn fsch_y[fi] > HOEHE:
                fsch_aktiv[fi] = falsch
            # Spieler treffen
            wenn unverwundbar == 0:
                wenn abs_wert(fsch_x[fi] - spieler_x) < 15 und abs_wert(fsch_y[fi] - spieler_y) < 15:
                    fsch_aktiv[fi] = falsch
                    setze leben auf leben - 1
                    setze unverwundbar auf 60
                    explosion_start(spieler_x, spieler_y)
        setze fi auf fi + 1

    # === TREFFER-CHECK ===
    setze si auf 0
    solange si < MAX_SCHUESSE:
        wenn sch_aktiv[si]:
            setze gi auf 0
            solange gi < gegner_total:
                wenn geg_aktiv[gi]:
                    wenn abs_wert(sch_x[si] - geg_x[gi]) < 18 und abs_wert(sch_y[si] - geg_y[gi]) < 14:
                        geg_hp[gi] = geg_hp[gi] - 1
                        sch_aktiv[si] = falsch
                        wenn geg_hp[gi] <= 0:
                            geg_aktiv[gi] = falsch
                            explosion_start(geg_x[gi], geg_y[gi])
                            setze score auf score + (geg_typ[gi] + 1) * 100
                setze gi auf gi + 1
        setze si auf si + 1

    # Gegner kollidiert mit Spieler
    wenn unverwundbar == 0:
        setze gi auf 0
        solange gi < gegner_total:
            wenn geg_aktiv[gi]:
                wenn abs_wert(geg_x[gi] - spieler_x) < 20 und abs_wert(geg_y[gi] - spieler_y) < 20:
                    geg_aktiv[gi] = falsch
                    explosion_start(geg_x[gi], geg_y[gi])
                    setze leben auf leben - 1
                    setze unverwundbar auf 60
            setze gi auf gi + 1

    # Explosionen
    setze ei auf 0
    solange ei < MAX_EXPLOSIONEN:
        wenn expl_aktiv[ei]:
            expl_timer[ei] = expl_timer[ei] - 1
            wenn expl_timer[ei] <= 0:
                expl_aktiv[ei] = falsch
        setze ei auf ei + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#000011")

    # Sterne (statisch, basierend auf Position)
    setze star_i auf 0
    solange star_i < 50:
        setze sx auf (star_i * 347 + 89) % BREITE
        setze sy auf (star_i * 571 + 23) % HOEHE
        zeichne_pixel(win, sx, sy, "#FFFFFF")
        setze star_i auf star_i + 1

    # Gegner
    setze gi auf 0
    solange gi < gegner_total:
        wenn geg_aktiv[gi]:
            setze gx auf geg_x[gi]
            setze gy auf geg_y[gi]
            wenn geg_typ[gi] == 0:
                # Biene (gelb)
                zeichne_rechteck(win, gx - 10, gy - 8, 20, 16, "#FDD835")
                zeichne_rechteck(win, gx - 14, gy - 4, 6, 8, "#FFB300")
                zeichne_rechteck(win, gx + 8, gy - 4, 6, 8, "#FFB300")
            wenn geg_typ[gi] == 1:
                # Schmetterling (rot)
                zeichne_rechteck(win, gx - 8, gy - 10, 16, 20, "#E53935")
                zeichne_rechteck(win, gx - 16, gy - 6, 10, 12, "#EF5350")
                zeichne_rechteck(win, gx + 6, gy - 6, 10, 12, "#EF5350")
            wenn geg_typ[gi] == 2:
                # Boss (gruen, groesser)
                zeichne_rechteck(win, gx - 14, gy - 12, 28, 24, "#2E7D32")
                zeichne_rechteck(win, gx - 10, gy - 16, 20, 6, "#388E3C")
                zeichne_kreis(win, gx - 6, gy - 4, 4, "#FFEB3B")
                zeichne_kreis(win, gx + 6, gy - 4, 4, "#FFEB3B")
        setze gi auf gi + 1

    # Spieler-Schuesse
    setze si auf 0
    solange si < MAX_SCHUESSE:
        wenn sch_aktiv[si]:
            zeichne_rechteck(win, sch_x[si] - 2, sch_y[si] - 6, 4, 12, "#00E5FF")
        setze si auf si + 1

    # Feind-Schuesse
    setze fi auf 0
    solange fi < MAX_FEIND_SCHUESSE:
        wenn fsch_aktiv[fi]:
            zeichne_kreis(win, fsch_x[fi], fsch_y[fi], 3, "#FF5722")
        setze fi auf fi + 1

    # Explosionen
    setze ei auf 0
    solange ei < MAX_EXPLOSIONEN:
        wenn expl_aktiv[ei]:
            setze er auf 12 - expl_timer[ei]
            zeichne_kreis(win, expl_x[ei], expl_y[ei], er, "#FF9800")
            wenn er > 4:
                zeichne_kreis(win, expl_x[ei], expl_y[ei], er - 4, "#FFEB3B")
        setze ei auf ei + 1

    # Spieler
    wenn unverwundbar == 0 oder (unverwundbar / 3) % 2 == 0:
        zeichne_rechteck(win, spieler_x - 12, spieler_y - 8, 24, 20, "#42A5F5")
        zeichne_rechteck(win, spieler_x - 4, spieler_y - 18, 8, 12, "#64B5F6")
        zeichne_rechteck(win, spieler_x - 18, spieler_y, 8, 10, "#1E88E5")
        zeichne_rechteck(win, spieler_x + 10, spieler_y, 8, 10, "#1E88E5")
        zeichne_rechteck(win, spieler_x - 4, spieler_y + 12, 8, 5, "#FF9800")

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 30, "#000022")
    setze si auf 0
    solange si < score / 200 und si < 25:
        zeichne_kreis(win, 15 + si * 16, 15, 5, "#FFD700")
        setze si auf si + 1
    # Leben
    setze li auf 0
    solange li < leben:
        zeichne_rechteck(win, BREITE - 20 - li * 25, 8, 16, 14, "#42A5F5")
        setze li auf li + 1
    # Level
    setze lvi auf 0
    solange lvi < level und lvi < 10:
        zeichne_rechteck(win, BREITE / 2 - 45 + lvi * 10, 10, 8, 8, "#FF9800")
        setze lvi auf lvi + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "GAME OVER! Score: " + text(score) + " | Level: " + text(level)

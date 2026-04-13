# ============================================================
# moo Space Shooter — Vertikaler Scrolling Shooter
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/space_shooter.moo -o beispiele/space_shooter
#   ./beispiele/space_shooter
#
# Bedienung:
#   Links/Rechts oder A/D - Schiff bewegen
#   Leertaste - Schiessen (Auto-Fire)
#   Escape - Beenden
#
# Features:
#   * Scrollender Sternen-Hintergrund (3 Schichten Parallax)
#   * Gegner-Wellen mit steigender Schwierigkeit
#   * Power-Ups: Schnellfeuer (gelb), Schild (blau)
#   * Explosions-Animationen
#   * Highscore + Leben
# ============================================================

# === KONSTANTEN ===
setze BREITE auf 600
setze HOEHE auf 800
setze SPIELER_W auf 32
setze SPIELER_H auf 24
setze SPIELER_SPEED auf 6
setze SCHUSS_SPEED auf 8
setze MAX_SCHUESSE auf 30
setze FEUER_RATE auf 8
setze MAX_GEGNER auf 30
setze MAX_STERNE auf 80
setze MAX_EXPLOSIONEN auf 20
setze MAX_POWERUPS auf 5

# === SPIELER ===
setze spieler_x auf BREITE / 2.0
setze spieler_y auf HOEHE - 80.0
setze leben auf 3
setze punkte auf 0
setze schild auf 0
setze feuer_cooldown auf 0
setze feuer_rate auf FEUER_RATE
setze unverwundbar auf 0

# === SCHÜSSE ===
setze schuss_x auf []
setze schuss_y auf []
setze schuss_aktiv auf []
setze si auf 0
solange si < MAX_SCHUESSE:
    schuss_x.hinzufügen(0.0)
    schuss_y.hinzufügen(0.0)
    schuss_aktiv.hinzufügen(falsch)
    setze si auf si + 1

# === GEGNER ===
setze geg_x auf []
setze geg_y auf []
setze geg_typ auf []
setze geg_hp auf []
setze geg_aktiv auf []
setze geg_speed auf []
setze gi auf 0
solange gi < MAX_GEGNER:
    geg_x.hinzufügen(0.0)
    geg_y.hinzufügen(0.0)
    geg_typ.hinzufügen(0)
    geg_hp.hinzufügen(0)
    geg_aktiv.hinzufügen(falsch)
    geg_speed.hinzufügen(0.0)
    setze gi auf gi + 1

# === STERNE (3-Schichten Parallax) ===
setze stern_x auf []
setze stern_y auf []
setze stern_speed auf []
setze stern_groesse auf []
setze si auf 0
solange si < MAX_STERNE:
    stern_x.hinzufügen((si * 347 + 123) % BREITE)
    stern_y.hinzufügen((si * 571 + 89) % HOEHE)
    setze schicht auf si % 3
    wenn schicht == 0:
        stern_speed.hinzufügen(1.0)
        stern_groesse.hinzufügen(1)
    wenn schicht == 1:
        stern_speed.hinzufügen(2.0)
        stern_groesse.hinzufügen(2)
    wenn schicht == 2:
        stern_speed.hinzufügen(3.0)
        stern_groesse.hinzufügen(3)
    setze si auf si + 1

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

# === POWER-UPS ===
setze pow_x auf []
setze pow_y auf []
setze pow_typ auf []
setze pow_aktiv auf []
setze pi auf 0
solange pi < MAX_POWERUPS:
    pow_x.hinzufügen(0.0)
    pow_y.hinzufügen(0.0)
    pow_typ.hinzufügen(0)
    pow_aktiv.hinzufügen(falsch)
    setze pi auf pi + 1

# === WELLEN ===
setze welle auf 1
setze welle_timer auf 0
setze welle_interval auf 120

# === HILFSFUNKTIONEN ===
funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

funktion schuss_feuern():
    setze si auf 0
    solange si < MAX_SCHUESSE:
        wenn nicht schuss_aktiv[si]:
            schuss_x[si] = spieler_x
            schuss_y[si] = spieler_y - SPIELER_H / 2
            schuss_aktiv[si] = wahr
            gib_zurück nichts
        setze si auf si + 1

funktion gegner_spawnen(x, typ):
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn nicht geg_aktiv[gi]:
            geg_x[gi] = x * 1.0
            geg_y[gi] = -30.0
            geg_typ[gi] = typ
            geg_aktiv[gi] = wahr
            wenn typ == 0:
                geg_hp[gi] = 1
                geg_speed[gi] = 2.0
            wenn typ == 1:
                geg_hp[gi] = 3
                geg_speed[gi] = 1.5
            wenn typ == 2:
                geg_hp[gi] = 5
                geg_speed[gi] = 1.0
            gib_zurück nichts
        setze gi auf gi + 1

funktion explosion_starten(x, y):
    setze ei auf 0
    solange ei < MAX_EXPLOSIONEN:
        wenn nicht expl_aktiv[ei]:
            expl_x[ei] = x
            expl_y[ei] = y
            expl_timer[ei] = 15
            expl_aktiv[ei] = wahr
            gib_zurück nichts
        setze ei auf ei + 1

funktion powerup_spawnen(x, y):
    setze pi auf 0
    solange pi < MAX_POWERUPS:
        wenn nicht pow_aktiv[pi]:
            pow_x[pi] = x
            pow_y[pi] = y
            pow_typ[pi] = (punkte / 500) % 2
            pow_aktiv[pi] = wahr
            gib_zurück nichts
        setze pi auf pi + 1

# === SPIELER ZEICHNEN (Schiff-Form) ===
funktion zeichne_schiff(win, x, y):
    # Rumpf
    zeichne_rechteck(win, x - 12, y - 8, 24, 20, "#42A5F5")
    # Spitze
    zeichne_rechteck(win, x - 4, y - 18, 8, 12, "#64B5F6")
    # Flügel
    zeichne_rechteck(win, x - 20, y, 8, 12, "#1E88E5")
    zeichne_rechteck(win, x + 12, y, 8, 12, "#1E88E5")
    # Triebwerk
    zeichne_rechteck(win, x - 6, y + 12, 12, 6, "#FF9800")
    zeichne_rechteck(win, x - 3, y + 18, 6, 4, "#FFEB3B")
    # Schild-Anzeige
    wenn schild > 0:
        zeichne_kreis(win, x, y, 22, "#00BCD4")

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Space Shooter", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # === INPUT ===
    wenn taste_gedrückt("links") oder taste_gedrückt("a"):
        setze spieler_x auf spieler_x - SPIELER_SPEED
    wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
        setze spieler_x auf spieler_x + SPIELER_SPEED

    wenn spieler_x < 20:
        setze spieler_x auf 20.0
    wenn spieler_x > BREITE - 20:
        setze spieler_x auf (BREITE - 20) * 1.0

    # Auto-Fire
    wenn taste_gedrückt("leertaste"):
        wenn feuer_cooldown <= 0:
            schuss_feuern()
            setze feuer_cooldown auf feuer_rate
    wenn feuer_cooldown > 0:
        setze feuer_cooldown auf feuer_cooldown - 1

    # === WELLEN-SYSTEM ===
    setze welle_timer auf welle_timer + 1
    wenn welle_timer >= welle_interval:
        setze welle_timer auf 0
        setze spawn_count auf 3 + welle
        wenn spawn_count > 8:
            setze spawn_count auf 8
        setze spawn_i auf 0
        solange spawn_i < spawn_count:
            setze spawn_x auf 50 + (spawn_i * (BREITE - 100)) / spawn_count
            setze typ auf 0
            wenn welle > 3 und spawn_i % 3 == 0:
                setze typ auf 1
            wenn welle > 6 und spawn_i % 5 == 0:
                setze typ auf 2
            gegner_spawnen(spawn_x, typ)
            setze spawn_i auf spawn_i + 1
        setze welle auf welle + 1
        setze welle_interval auf welle_interval - 5
        wenn welle_interval < 60:
            setze welle_interval auf 60

    # === SCHÜSSE BEWEGEN ===
    setze si auf 0
    solange si < MAX_SCHUESSE:
        wenn schuss_aktiv[si]:
            schuss_y[si] = schuss_y[si] - SCHUSS_SPEED
            wenn schuss_y[si] < -10:
                schuss_aktiv[si] = falsch
        setze si auf si + 1

    # === GEGNER BEWEGEN ===
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn geg_aktiv[gi]:
            geg_y[gi] = geg_y[gi] + geg_speed[gi]
            # Typ 1: Sinuswelle
            wenn geg_typ[gi] == 1:
                geg_x[gi] = geg_x[gi] + sinus(geg_y[gi] * 0.05) * 2
            # Typ 2: Auf Spieler zielen
            wenn geg_typ[gi] == 2:
                wenn geg_x[gi] < spieler_x:
                    geg_x[gi] = geg_x[gi] + 0.5
                wenn geg_x[gi] > spieler_x:
                    geg_x[gi] = geg_x[gi] - 0.5
            # Aus dem Bildschirm
            wenn geg_y[gi] > HOEHE + 30:
                geg_aktiv[gi] = falsch
        setze gi auf gi + 1

    # === KOLLISION: Schuss trifft Gegner ===
    setze si auf 0
    solange si < MAX_SCHUESSE:
        wenn schuss_aktiv[si]:
            setze gi auf 0
            solange gi < MAX_GEGNER:
                wenn geg_aktiv[gi]:
                    wenn abs_wert(schuss_x[si] - geg_x[gi]) < 18 und abs_wert(schuss_y[si] - geg_y[gi]) < 18:
                        geg_hp[gi] = geg_hp[gi] - 1
                        schuss_aktiv[si] = falsch
                        wenn geg_hp[gi] <= 0:
                            geg_aktiv[gi] = falsch
                            explosion_starten(geg_x[gi], geg_y[gi])
                            setze punkte auf punkte + (geg_typ[gi] + 1) * 100
                            # Power-Up Chance
                            wenn punkte % 1000 < 100:
                                powerup_spawnen(geg_x[gi], geg_y[gi])
                setze gi auf gi + 1
        setze si auf si + 1

    # === KOLLISION: Gegner trifft Spieler ===
    wenn unverwundbar <= 0:
        setze gi auf 0
        solange gi < MAX_GEGNER:
            wenn geg_aktiv[gi]:
                wenn abs_wert(spieler_x - geg_x[gi]) < 24 und abs_wert(spieler_y - geg_y[gi]) < 24:
                    geg_aktiv[gi] = falsch
                    explosion_starten(geg_x[gi], geg_y[gi])
                    wenn schild > 0:
                        setze schild auf schild - 1
                    sonst:
                        setze leben auf leben - 1
                        setze unverwundbar auf 60
                    wenn leben <= 0:
                        stopp
            setze gi auf gi + 1
    wenn unverwundbar > 0:
        setze unverwundbar auf unverwundbar - 1

    # === POWER-UPS BEWEGEN + EINSAMMELN ===
    setze pi auf 0
    solange pi < MAX_POWERUPS:
        wenn pow_aktiv[pi]:
            pow_y[pi] = pow_y[pi] + 2
            wenn pow_y[pi] > HOEHE + 20:
                pow_aktiv[pi] = falsch
            wenn abs_wert(spieler_x - pow_x[pi]) < 24 und abs_wert(spieler_y - pow_y[pi]) < 24:
                pow_aktiv[pi] = falsch
                wenn pow_typ[pi] == 0:
                    setze feuer_rate auf 3
                wenn pow_typ[pi] == 1:
                    setze schild auf schild + 3
        setze pi auf pi + 1

    # === EXPLOSIONEN ===
    setze ei auf 0
    solange ei < MAX_EXPLOSIONEN:
        wenn expl_aktiv[ei]:
            expl_timer[ei] = expl_timer[ei] - 1
            wenn expl_timer[ei] <= 0:
                expl_aktiv[ei] = falsch
        setze ei auf ei + 1

    # === STERNE BEWEGEN ===
    setze si auf 0
    solange si < MAX_STERNE:
        stern_y[si] = stern_y[si] + stern_speed[si]
        wenn stern_y[si] > HOEHE:
            stern_y[si] = 0.0
            stern_x[si] = (stern_x[si] * 347 + 123) % BREITE
        setze si auf si + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#0A0A1A")

    # Sterne
    setze si auf 0
    solange si < MAX_STERNE:
        setze helligkeit auf "#444466"
        wenn stern_groesse[si] == 2:
            setze helligkeit auf "#7777AA"
        wenn stern_groesse[si] == 3:
            setze helligkeit auf "#AAAADD"
        zeichne_rechteck(win, stern_x[si], stern_y[si], stern_groesse[si], stern_groesse[si], helligkeit)
        setze si auf si + 1

    # Power-Ups
    setze pi auf 0
    solange pi < MAX_POWERUPS:
        wenn pow_aktiv[pi]:
            wenn pow_typ[pi] == 0:
                zeichne_rechteck(win, pow_x[pi] - 8, pow_y[pi] - 8, 16, 16, "#FFEB3B")
                zeichne_rechteck(win, pow_x[pi] - 4, pow_y[pi] - 4, 8, 8, "#FF9800")
            wenn pow_typ[pi] == 1:
                zeichne_kreis(win, pow_x[pi], pow_y[pi], 10, "#00BCD4")
                zeichne_kreis(win, pow_x[pi], pow_y[pi], 5, "#4DD0E1")
        setze pi auf pi + 1

    # Gegner
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn geg_aktiv[gi]:
            wenn geg_typ[gi] == 0:
                # Klein, schnell (grün)
                zeichne_rechteck(win, geg_x[gi] - 10, geg_y[gi] - 8, 20, 16, "#66BB6A")
                zeichne_rechteck(win, geg_x[gi] - 14, geg_y[gi] - 4, 6, 8, "#43A047")
                zeichne_rechteck(win, geg_x[gi] + 8, geg_y[gi] - 4, 6, 8, "#43A047")
            wenn geg_typ[gi] == 1:
                # Mittel, Sinuswelle (orange)
                zeichne_rechteck(win, geg_x[gi] - 14, geg_y[gi] - 10, 28, 20, "#FF9800")
                zeichne_rechteck(win, geg_x[gi] - 6, geg_y[gi] - 16, 12, 8, "#F57C00")
            wenn geg_typ[gi] == 2:
                # Groß, zielend (rot)
                zeichne_rechteck(win, geg_x[gi] - 18, geg_y[gi] - 14, 36, 28, "#F44336")
                zeichne_rechteck(win, geg_x[gi] - 8, geg_y[gi] - 20, 16, 8, "#D32F2F")
                zeichne_rechteck(win, geg_x[gi] - 22, geg_y[gi] - 6, 8, 12, "#E53935")
                zeichne_rechteck(win, geg_x[gi] + 14, geg_y[gi] - 6, 8, 12, "#E53935")
        setze gi auf gi + 1

    # Schüsse
    setze si auf 0
    solange si < MAX_SCHUESSE:
        wenn schuss_aktiv[si]:
            zeichne_rechteck(win, schuss_x[si] - 2, schuss_y[si] - 6, 4, 12, "#FFEB3B")
        setze si auf si + 1

    # Explosionen
    setze ei auf 0
    solange ei < MAX_EXPLOSIONEN:
        wenn expl_aktiv[ei]:
            setze radius auf 15 - expl_timer[ei]
            wenn radius > 0:
                zeichne_kreis(win, expl_x[ei], expl_y[ei], radius, "#FF9800")
                wenn radius > 5:
                    zeichne_kreis(win, expl_x[ei], expl_y[ei], radius - 5, "#FFEB3B")
        setze ei auf ei + 1

    # Spieler
    wenn unverwundbar == 0 oder (unverwundbar / 3) % 2 == 0:
        zeichne_schiff(win, spieler_x, spieler_y)

    # HUD
    # Leben (Herzen)
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, 20 + li * 25, 20, 8, "#F44336")
        setze li auf li + 1

    # Schild
    setze si auf 0
    solange si < schild:
        zeichne_kreis(win, 20 + si * 18, 45, 6, "#00BCD4")
        setze si auf si + 1

    # Score (Punkte-Balken)
    setze score_bar auf punkte / 50
    wenn score_bar > 200:
        setze score_bar auf 200
    zeichne_rechteck(win, BREITE - 210, 10, 200, 8, "#333333")
    zeichne_rechteck(win, BREITE - 210, 10, score_bar, 8, "#FFD700")

    # Welle-Anzeige
    setze wave_i auf 0
    solange wave_i < welle und wave_i < 15:
        zeichne_rechteck(win, BREITE - 210 + wave_i * 13, 25, 10, 10, "#FF9800")
        setze wave_i auf wave_i + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "GAME OVER! Punkte: " + text(punkte) + " | Welle: " + text(welle)

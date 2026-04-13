# ============================================================
# moo Brawler — Streets of Rage Style (2-Spieler Coop)
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/brawler.moo -o beispiele/brawler
#   ./beispiele/brawler
#
# Spieler 1 (BLAU):  A/D=Bewegen, W=Springen, F=Schlag, G=Kick
# Spieler 2 (ROT):   Links/Rechts=Bewegen, Hoch=Springen, K=Schlag, L=Kick
# Escape = Beenden
#
# Features:
#   * 2-Spieler Coop auf einer Tastatur
#   * Auto-Scrolling nach rechts
#   * Gegner-Wellen mit zunehmender Schwierigkeit
#   * Verschiedene Moves (Schlag, Kick, Sprung-Angriff)
#   * Lebensbalken fuer beide Spieler
#   * Punkte-System
# ============================================================

# === ARENA ===
setze BREITE auf 800
setze HOEHE auf 600
setze BODEN_Y auf 460
setze LEVEL_LAENGE auf 4000

# === SPIELER-KONSTANTEN ===
setze SPIELER_W auf 30
setze SPIELER_H auf 50
setze SPIELER_SPEED auf 3
setze SPRUNG_KRAFT auf 9.0
setze GRAV auf 0.4
setze SCHLAG_DAUER auf 10
setze KICK_DAUER auf 14
setze SCHLAG_DMG auf 12
setze KICK_DMG auf 18
setze SPIELER_MAX_HP auf 100
setze ANGRIFF_REICHWEITE auf 45

# === GEGNER-KONSTANTEN ===
setze GEGNER_W auf 28
setze GEGNER_H auf 45
setze GEGNER_SPEED auf 1
setze GEGNER_HP auf 40
setze GEGNER_DMG auf 5
setze GEGNER_ANGRIFF_DAUER auf 20
setze MAX_GEGNER auf 20

# === SPIELER-STATE ===
setze p1_x auf 100.0
setze p1_y auf BODEN_Y * 1.0
setze p1_vy auf 0.0
setze p1_hp auf SPIELER_MAX_HP
setze p1_schlag auf 0
setze p1_kick auf 0
setze p1_cooldown auf 0
setze p1_punkte auf 0
setze p1_blinken auf 0

setze p2_x auf 160.0
setze p2_y auf BODEN_Y * 1.0
setze p2_vy auf 0.0
setze p2_hp auf SPIELER_MAX_HP
setze p2_schlag auf 0
setze p2_kick auf 0
setze p2_cooldown auf 0
setze p2_punkte auf 0
setze p2_blinken auf 0

# === KAMERA ===
setze kamera_x auf 0.0

# === GEGNER-SYSTEM (parallele Listen) ===
setze g_x auf []
setze g_y auf []
setze g_vy auf []
setze g_hp auf []
setze g_aktiv auf []
setze g_angriff auf []
setze g_typ auf []
setze gegner_count auf 0

# === WELLEN-SYSTEM ===
setze welle auf 1
setze welle_timer auf 0
setze welle_gegner auf 3
setze naechste_welle_x auf 300

# === HILFSFUNKTIONEN ===
funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

funktion gegner_spawnen(wx, typ):
    wenn gegner_count >= MAX_GEGNER:
        gib_zurück nichts
    g_x.hinzufügen(wx * 1.0)
    g_y.hinzufügen(BODEN_Y * 1.0)
    g_vy.hinzufügen(0.0)
    g_hp.hinzufügen(GEGNER_HP + typ * 20)
    g_aktiv.hinzufügen(wahr)
    g_angriff.hinzufügen(0)
    g_typ.hinzufügen(typ)
    setze gegner_count auf gegner_count + 1

funktion naechster_spieler_x(gx):
    setze d1 auf abs_wert(gx - p1_x)
    setze d2 auf abs_wert(gx - p2_x)
    wenn p1_hp <= 0:
        gib_zurück p2_x
    wenn p2_hp <= 0:
        gib_zurück p1_x
    wenn d1 < d2:
        gib_zurück p1_x
    gib_zurück p2_x

funktion treffer_check(ax, ay, richtung, reichweite, gx, gy, gw, gh):
    setze fx auf ax + richtung * reichweite / 2
    setze fy auf ay - SPIELER_H / 2
    # Einfache Distanz-Prüfung
    wenn abs_wert(fx - (gx + gw / 2)) < reichweite und abs_wert(fy - (gy - gh / 2)) < gh:
        gib_zurück wahr
    gib_zurück falsch

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Brawler — Streets of Moo", BREITE, HOEHE)

# === SPRITES LADEN ===
setze spr_pfad auf "beispiele/assets/sprites/kenney_platformer/PNG/"
setze spr_p1_idle auf sprite_laden(win, spr_pfad + "Characters/platformChar_idle.png")
setze spr_p1_walk1 auf sprite_laden(win, spr_pfad + "Characters/platformChar_walk1.png")
setze spr_p1_walk2 auf sprite_laden(win, spr_pfad + "Characters/platformChar_walk2.png")
setze spr_p1_attack auf sprite_laden(win, spr_pfad + "Characters/platformChar_jump.png")
setze spr_p2_idle auf sprite_laden(win, spr_pfad + "Characters/platformChar_happy.png")
setze spr_p2_walk1 auf sprite_laden(win, spr_pfad + "Characters/platformChar_climb1.png")
setze spr_p2_walk2 auf sprite_laden(win, spr_pfad + "Characters/platformChar_climb2.png")
setze spr_p2_attack auf sprite_laden(win, spr_pfad + "Characters/platformChar_duck.png")
setze spr_feind auf sprite_laden(win, spr_pfad + "Characters/platformChar_happy.png")
setze p1_walk_frame auf 0
setze p1_walk_timer auf 0
setze p2_walk_frame auf 0
setze p2_walk_timer auf 0

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Game Over Check
    wenn p1_hp <= 0 und p2_hp <= 0:
        stopp

    # === WELLEN-SYSTEM ===
    wenn kamera_x > naechste_welle_x:
        setze spawn_x auf kamera_x + BREITE + 50
        setze wellen_i auf 0
        solange wellen_i < welle_gegner:
            setze typ auf 0
            wenn welle > 3:
                setze typ auf 1
            gegner_spawnen(spawn_x + wellen_i * 60, typ)
            setze wellen_i auf wellen_i + 1
        setze welle auf welle + 1
        setze welle_gegner auf 3 + welle
        wenn welle_gegner > 8:
            setze welle_gegner auf 8
        setze naechste_welle_x auf naechste_welle_x + 400

    # === SPIELER 1 INPUT ===
    wenn p1_hp > 0:
        wenn taste_gedrückt("a"):
            setze p1_x auf p1_x - SPIELER_SPEED
        wenn taste_gedrückt("d"):
            setze p1_x auf p1_x + SPIELER_SPEED
        wenn taste_gedrückt("w") und p1_y >= BODEN_Y:
            setze p1_vy auf 0 - SPRUNG_KRAFT
        wenn taste_gedrückt("f") und p1_schlag == 0 und p1_kick == 0 und p1_cooldown == 0:
            setze p1_schlag auf SCHLAG_DAUER
        wenn taste_gedrückt("g") und p1_schlag == 0 und p1_kick == 0 und p1_cooldown == 0:
            setze p1_kick auf KICK_DAUER

    # === SPIELER 2 INPUT ===
    wenn p2_hp > 0:
        wenn taste_gedrückt("links"):
            setze p2_x auf p2_x - SPIELER_SPEED
        wenn taste_gedrückt("rechts"):
            setze p2_x auf p2_x + SPIELER_SPEED
        wenn taste_gedrückt("hoch") und p2_y >= BODEN_Y:
            setze p2_vy auf 0 - SPRUNG_KRAFT
        wenn taste_gedrückt("k") und p2_schlag == 0 und p2_kick == 0 und p2_cooldown == 0:
            setze p2_schlag auf SCHLAG_DAUER
        wenn taste_gedrückt("l") und p2_schlag == 0 und p2_kick == 0 und p2_cooldown == 0:
            setze p2_kick auf KICK_DAUER

    # Walk-Animation
    wenn taste_gedrückt("a") oder taste_gedrückt("d"):
        setze p1_walk_timer auf p1_walk_timer + 1
        wenn p1_walk_timer > 8:
            setze p1_walk_timer auf 0
            setze p1_walk_frame auf 1 - p1_walk_frame
    sonst:
        setze p1_walk_frame auf 0
    wenn taste_gedrückt("links") oder taste_gedrückt("rechts"):
        setze p2_walk_timer auf p2_walk_timer + 1
        wenn p2_walk_timer > 8:
            setze p2_walk_timer auf 0
            setze p2_walk_frame auf 1 - p2_walk_frame
    sonst:
        setze p2_walk_frame auf 0

    # === PHYSIK SPIELER ===
    setze p1_vy auf p1_vy + GRAV
    setze p1_y auf p1_y + p1_vy
    wenn p1_y > BODEN_Y:
        setze p1_y auf BODEN_Y * 1.0
        setze p1_vy auf 0.0
    setze p2_vy auf p2_vy + GRAV
    setze p2_y auf p2_y + p2_vy
    wenn p2_y > BODEN_Y:
        setze p2_y auf BODEN_Y * 1.0
        setze p2_vy auf 0.0

    # Spieler im Bildschirm halten
    wenn p1_x < kamera_x + 20:
        setze p1_x auf kamera_x + 20
    wenn p1_x > kamera_x + BREITE - 20:
        setze p1_x auf kamera_x + BREITE - 20
    wenn p2_x < kamera_x + 20:
        setze p2_x auf kamera_x + 20
    wenn p2_x > kamera_x + BREITE - 20:
        setze p2_x auf kamera_x + BREITE - 20

    # === ANGRIFFS-TIMER ===
    wenn p1_schlag > 0:
        setze p1_schlag auf p1_schlag - 1
        wenn p1_schlag == 0:
            setze p1_cooldown auf 6
    wenn p1_kick > 0:
        setze p1_kick auf p1_kick - 1
        wenn p1_kick == 0:
            setze p1_cooldown auf 8
    wenn p1_cooldown > 0:
        setze p1_cooldown auf p1_cooldown - 1
    wenn p1_blinken > 0:
        setze p1_blinken auf p1_blinken - 1

    wenn p2_schlag > 0:
        setze p2_schlag auf p2_schlag - 1
        wenn p2_schlag == 0:
            setze p2_cooldown auf 6
    wenn p2_kick > 0:
        setze p2_kick auf p2_kick - 1
        wenn p2_kick == 0:
            setze p2_cooldown auf 8
    wenn p2_cooldown > 0:
        setze p2_cooldown auf p2_cooldown - 1
    wenn p2_blinken > 0:
        setze p2_blinken auf p2_blinken - 1

    # === GEGNER UPDATE ===
    setze gi auf 0
    solange gi < gegner_count:
        wenn g_aktiv[gi]:
            # Physik
            g_vy[gi] = g_vy[gi] + GRAV
            g_y[gi] = g_y[gi] + g_vy[gi]
            wenn g_y[gi] > BODEN_Y:
                g_y[gi] = BODEN_Y * 1.0
                g_vy[gi] = 0.0

            # KI: Laufe zum naechsten Spieler
            setze ziel_x auf naechster_spieler_x(g_x[gi])
            setze dist auf ziel_x - g_x[gi]
            setze g_speed auf GEGNER_SPEED + g_typ[gi]
            wenn abs_wert(dist) > ANGRIFF_REICHWEITE:
                wenn dist > 0:
                    g_x[gi] = g_x[gi] + g_speed
                sonst:
                    g_x[gi] = g_x[gi] - g_speed
            sonst:
                # Angreifen
                wenn g_angriff[gi] == 0:
                    g_angriff[gi] = GEGNER_ANGRIFF_DAUER

            # Gegner Angriff
            wenn g_angriff[gi] > 0:
                g_angriff[gi] = g_angriff[gi] - 1
                wenn g_angriff[gi] == 5:
                    # Treffer-Check gegen beide Spieler
                    wenn abs_wert(g_x[gi] - p1_x) < ANGRIFF_REICHWEITE und abs_wert(g_y[gi] - p1_y) < SPIELER_H und p1_blinken == 0 und p1_hp > 0:
                        setze p1_hp auf p1_hp - GEGNER_DMG
                        setze p1_blinken auf 20
                    wenn abs_wert(g_x[gi] - p2_x) < ANGRIFF_REICHWEITE und abs_wert(g_y[gi] - p2_y) < SPIELER_H und p2_blinken == 0 und p2_hp > 0:
                        setze p2_hp auf p2_hp - GEGNER_DMG
                        setze p2_blinken auf 20

            # Spieler trifft Gegner
            setze richtung1 auf 1
            wenn p1_x > g_x[gi]:
                setze richtung1 auf -1
            setze richtung2 auf 1
            wenn p2_x > g_x[gi]:
                setze richtung2 auf -1

            wenn p1_schlag > 0 und p1_schlag < SCHLAG_DAUER - 2 und p1_hp > 0:
                wenn treffer_check(p1_x, p1_y, 0 - richtung1, ANGRIFF_REICHWEITE, g_x[gi] - GEGNER_W / 2, g_y[gi], GEGNER_W, GEGNER_H):
                    g_hp[gi] = g_hp[gi] - SCHLAG_DMG
                    g_x[gi] = g_x[gi] - richtung1 * 10
                    setze p1_schlag auf 1

            wenn p1_kick > 0 und p1_kick < KICK_DAUER - 2 und p1_hp > 0:
                wenn treffer_check(p1_x, p1_y, 0 - richtung1, ANGRIFF_REICHWEITE + 10, g_x[gi] - GEGNER_W / 2, g_y[gi], GEGNER_W, GEGNER_H):
                    g_hp[gi] = g_hp[gi] - KICK_DMG
                    g_x[gi] = g_x[gi] - richtung1 * 20
                    setze p1_kick auf 1

            wenn p2_schlag > 0 und p2_schlag < SCHLAG_DAUER - 2 und p2_hp > 0:
                wenn treffer_check(p2_x, p2_y, 0 - richtung2, ANGRIFF_REICHWEITE, g_x[gi] - GEGNER_W / 2, g_y[gi], GEGNER_W, GEGNER_H):
                    g_hp[gi] = g_hp[gi] - SCHLAG_DMG
                    g_x[gi] = g_x[gi] - richtung2 * 10
                    setze p2_schlag auf 1

            wenn p2_kick > 0 und p2_kick < KICK_DAUER - 2 und p2_hp > 0:
                wenn treffer_check(p2_x, p2_y, 0 - richtung2, ANGRIFF_REICHWEITE + 10, g_x[gi] - GEGNER_W / 2, g_y[gi], GEGNER_W, GEGNER_H):
                    g_hp[gi] = g_hp[gi] - KICK_DMG
                    g_x[gi] = g_x[gi] - richtung2 * 20
                    setze p2_kick auf 1

            # Gegner besiegt
            wenn g_hp[gi] <= 0:
                g_aktiv[gi] = falsch
                setze p1_punkte auf p1_punkte + 50
                setze p2_punkte auf p2_punkte + 50
        setze gi auf gi + 1

    # === KAMERA AUTO-SCROLL ===
    setze avg_x auf p1_x
    wenn p1_hp > 0 und p2_hp > 0:
        setze avg_x auf (p1_x + p2_x) / 2
    wenn p1_hp <= 0:
        setze avg_x auf p2_x
    setze ziel_kamera auf avg_x - BREITE / 2
    wenn ziel_kamera > kamera_x:
        setze kamera_x auf kamera_x + 1.5
    wenn kamera_x < 0:
        setze kamera_x auf 0.0
    wenn kamera_x > LEVEL_LAENGE - BREITE:
        setze kamera_x auf (LEVEL_LAENGE - BREITE) * 1.0

    # === ZEICHNEN ===
    fenster_löschen(win, "#16213E")

    # Hintergrund-Gebäude (parallax-artig)
    setze bi auf 0
    solange bi < 8:
        setze bx auf bi * 200 - kamera_x * 0.3
        zeichne_rechteck(win, bx, 200, 80, 260, "#1A1A2E")
        zeichne_rechteck(win, bx + 100, 280, 60, 180, "#1A1A2E")
        setze bi auf bi + 1

    # Boden
    zeichne_rechteck(win, 0, BODEN_Y + SPIELER_H, BREITE, HOEHE - BODEN_Y - SPIELER_H, "#2C3E50")
    zeichne_linie(win, 0, BODEN_Y + SPIELER_H, BREITE, BODEN_Y + SPIELER_H, "#34495E")

    # Gegner
    setze gi auf 0
    solange gi < gegner_count:
        wenn g_aktiv[gi]:
            setze sx auf g_x[gi] - kamera_x
            setze sy auf g_y[gi]
            setze gfarbe auf "#558B2F"
            wenn g_typ[gi] > 0:
                setze gfarbe auf "#BF360C"
            # Körper
            sprite_zeichnen_skaliert(win, spr_feind, sx - GEGNER_W / 2, sy - GEGNER_H - 10, GEGNER_W + 6, GEGNER_H + 10)
            # HP-Balken
            setze ghp_w auf g_hp[gi] * 30 / (GEGNER_HP + g_typ[gi] * 20)
            zeichne_rechteck(win, sx - 15, sy - GEGNER_H - 25, ghp_w, 4, "#F44336")
        setze gi auf gi + 1

    # Spieler 1 (Blau) — Sprites
    wenn p1_hp > 0:
        setze sx auf p1_x - kamera_x
        wenn p1_blinken == 0 oder p1_blinken % 4 < 2:
            setze spr1 auf spr_p1_idle
            wenn p1_schlag > 0 oder p1_kick > 0:
                setze spr1 auf spr_p1_attack
            sonst:
                wenn taste_gedrückt("a") oder taste_gedrückt("d"):
                    wenn p1_walk_frame == 0:
                        setze spr1 auf spr_p1_walk1
                    sonst:
                        setze spr1 auf spr_p1_walk2
            sprite_zeichnen_skaliert(win, spr1, sx - SPIELER_W / 2, p1_y - SPIELER_H - 10, 36, 60)
            wenn p1_schlag > 0:
                zeichne_rechteck(win, sx + 18, p1_y - SPIELER_H / 2, 25, 10, "#FFEB3B")
            wenn p1_kick > 0:
                zeichne_rechteck(win, sx + 12, p1_y - 5, 30, 8, "#FF9800")

    # Spieler 2 (Rot) — Sprites
    wenn p2_hp > 0:
        setze sx auf p2_x - kamera_x
        wenn p2_blinken == 0 oder p2_blinken % 4 < 2:
            setze spr2 auf spr_p2_idle
            wenn p2_schlag > 0 oder p2_kick > 0:
                setze spr2 auf spr_p2_attack
            sonst:
                wenn taste_gedrückt("links") oder taste_gedrückt("rechts"):
                    wenn p2_walk_frame == 0:
                        setze spr2 auf spr_p2_walk1
                    sonst:
                        setze spr2 auf spr_p2_walk2
            sprite_zeichnen_skaliert(win, spr2, sx - SPIELER_W / 2, p2_y - SPIELER_H - 10, 36, 60)
            wenn p2_schlag > 0:
                zeichne_rechteck(win, sx + 18, p2_y - SPIELER_H / 2, 25, 10, "#FFEB3B")
            wenn p2_kick > 0:
                zeichne_rechteck(win, sx + 12, p2_y - 5, 30, 8, "#FF9800")

    # HUD
    # P1 HP
    zeichne_rechteck(win, 10, 10, 200, 15, "#424242")
    setze hp1_w auf p1_hp * 200 / SPIELER_MAX_HP
    wenn hp1_w < 0:
        setze hp1_w auf 0
    zeichne_rechteck(win, 10, 10, hp1_w, 15, "#42A5F5")
    # P2 HP
    zeichne_rechteck(win, BREITE - 210, 10, 200, 15, "#424242")
    setze hp2_w auf p2_hp * 200 / SPIELER_MAX_HP
    wenn hp2_w < 0:
        setze hp2_w auf 0
    zeichne_rechteck(win, BREITE - 210, 10, hp2_w, 15, "#EF5350")

    # Score als Punkte-Reihe
    setze si auf 0
    solange si < p1_punkte / 100 und si < 20:
        zeichne_kreis(win, 15 + si * 12, 35, 4, "#FFD700")
        setze si auf si + 1

    # Welle-Anzeige
    setze wellen_i auf 0
    solange wellen_i < welle und wellen_i < 10:
        zeichne_rechteck(win, BREITE / 2 - 50 + wellen_i * 10, 10, 8, 8, "#FF9800")
        setze wellen_i auf wellen_i + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "GAME OVER! Punkte: " + text(p1_punkte + p2_punkte) + " | Welle: " + text(welle)

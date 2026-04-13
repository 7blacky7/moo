# ============================================================
# moo Fighter — 2-Spieler Kampfspiel (geteilte Tastatur)
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/fighter.moo -o beispiele/fighter
#   ./beispiele/fighter
#
# Spieler 1 (BLAU):  W=Springen, A/D=Bewegen, F=Schlagen, G=Blocken
# Spieler 2 (ROT):   Hoch=Springen, Links/Rechts=Bewegen, K=Schlagen, L=Blocken
# Escape = Beenden
# ============================================================

# === ARENA ===
setze BREITE auf 800
setze HOEHE auf 600
setze BODEN_Y auf 480
setze WAND_LINKS auf 30
setze WAND_RECHTS auf 770

# === SPIELER-KONSTANTEN ===
setze KOERPER_W auf 40
setze KOERPER_H auf 60
setze KOPF_R auf 18
setze FAUST_W auf 30
setze FAUST_H auf 12
setze GESCHWINDIGKEIT auf 4
setze SPRUNG_KRAFT auf 10.0
setze GRAVITATION auf 0.5
setze SCHLAG_DAUER auf 12
setze SCHLAG_SCHADEN auf 10
setze BLOCK_SCHADEN auf 4
setze KNOCKBACK auf 15
setze MAX_HP auf 100
setze HP_BALKEN_W auf 300
setze HP_BALKEN_H auf 20

# === SPIELER-STATE ===
# Spieler 1
setze p1_x auf 200.0
setze p1_y auf BODEN_Y * 1.0
setze p1_vy auf 0.0
setze p1_hp auf MAX_HP
setze p1_schlag auf 0
setze p1_block auf falsch
setze p1_richtung auf 1
setze p1_hit_cooldown auf 0

# Spieler 2
setze p2_x auf 560.0
setze p2_y auf BODEN_Y * 1.0
setze p2_vy auf 0.0
setze p2_hp auf MAX_HP
setze p2_schlag auf 0
setze p2_block auf falsch
setze p2_richtung auf -1
setze p2_hit_cooldown auf 0

# Runden
setze runde_vorbei auf falsch

# === KOLLISION (AABB) ===
funktion aabb_kollidiert(x1, y1, w1, h1, x2, y2, w2, h2):
    wenn x1 + w1 < x2:
        gib_zurück falsch
    wenn x2 + w2 < x1:
        gib_zurück falsch
    wenn y1 + h1 < y2:
        gib_zurück falsch
    wenn y2 + h2 < y1:
        gib_zurück falsch
    gib_zurück wahr

# === KÄMPFER ZEICHNEN (mit Sprites) ===
funktion zeichne_kaempfer(win, x, y, richtung, farbe, kopf_farbe, schlag_timer, blockt, s_idle, s_walk, s_jump, s_duck, am_boden):
    # Sprite wählen
    setze spr auf s_idle
    wenn nicht am_boden:
        setze spr auf s_jump
    wenn blockt:
        setze spr auf s_duck
    wenn schlag_timer > 0:
        setze spr auf s_walk
    # Sprite zeichnen (48x64)
    sprite_zeichnen_skaliert(win, spr, x - 24, y - KOERPER_H - 10, 48, 70)
    # Faust (Schlag-Effekt)
    wenn schlag_timer > 0:
        setze faust_x auf x + richtung * (KOERPER_W / 2 + FAUST_W / 2)
        setze faust_y auf y - KOERPER_H / 2 - FAUST_H / 2
        zeichne_rechteck(win, faust_x - FAUST_W / 2, faust_y, FAUST_W, FAUST_H, "#FFEB3B")
    wenn blockt:
        setze schild_x auf x + richtung * (KOERPER_W / 2 + 5)
        zeichne_rechteck(win, schild_x - 3, y - KOERPER_H + 5, 6, KOERPER_H - 10, "#90CAF9")

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Fighter", BREITE, HOEHE)

# Sprites laden
setze spr_pfad auf "beispiele/assets/sprites/kenney_platformer/PNG/Characters/"
setze p1_spr_idle auf sprite_laden(win, spr_pfad + "platformChar_idle.png")
setze p1_spr_walk auf sprite_laden(win, spr_pfad + "platformChar_walk1.png")
setze p1_spr_jump auf sprite_laden(win, spr_pfad + "platformChar_jump.png")
setze p1_spr_duck auf sprite_laden(win, spr_pfad + "platformChar_duck.png")
setze p2_spr_idle auf sprite_laden(win, spr_pfad + "platformChar_happy.png")
setze p2_spr_walk auf sprite_laden(win, spr_pfad + "platformChar_walk2.png")
setze p2_spr_jump auf sprite_laden(win, spr_pfad + "platformChar_climb1.png")
setze p2_spr_duck auf sprite_laden(win, spr_pfad + "platformChar_climb2.png")

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn nicht runde_vorbei:
        # === SPIELER 1 INPUT ===
        setze p1_block auf falsch
        wenn taste_gedrückt("a"):
            setze p1_x auf p1_x - GESCHWINDIGKEIT
            setze p1_richtung auf -1
        wenn taste_gedrückt("d"):
            setze p1_x auf p1_x + GESCHWINDIGKEIT
            setze p1_richtung auf 1
        wenn taste_gedrückt("w") und p1_y >= BODEN_Y:
            setze p1_vy auf 0 - SPRUNG_KRAFT
        wenn taste_gedrückt("f") und p1_schlag == 0 und p1_hit_cooldown == 0:
            setze p1_schlag auf SCHLAG_DAUER
        wenn taste_gedrückt("g"):
            setze p1_block auf wahr

        # === SPIELER 2 INPUT ===
        setze p2_block auf falsch
        wenn taste_gedrückt("links"):
            setze p2_x auf p2_x - GESCHWINDIGKEIT
            setze p2_richtung auf -1
        wenn taste_gedrückt("rechts"):
            setze p2_x auf p2_x + GESCHWINDIGKEIT
            setze p2_richtung auf 1
        wenn taste_gedrückt("hoch") und p2_y >= BODEN_Y:
            setze p2_vy auf 0 - SPRUNG_KRAFT
        wenn taste_gedrückt("k") und p2_schlag == 0 und p2_hit_cooldown == 0:
            setze p2_schlag auf SCHLAG_DAUER
        wenn taste_gedrückt("l"):
            setze p2_block auf wahr

        # === PHYSIK ===
        # Spieler 1
        setze p1_vy auf p1_vy + GRAVITATION
        setze p1_y auf p1_y + p1_vy
        wenn p1_y > BODEN_Y:
            setze p1_y auf BODEN_Y * 1.0
            setze p1_vy auf 0.0
        wenn p1_x < WAND_LINKS:
            setze p1_x auf WAND_LINKS * 1.0
        wenn p1_x > WAND_RECHTS:
            setze p1_x auf WAND_RECHTS * 1.0

        # Spieler 2
        setze p2_vy auf p2_vy + GRAVITATION
        setze p2_y auf p2_y + p2_vy
        wenn p2_y > BODEN_Y:
            setze p2_y auf BODEN_Y * 1.0
            setze p2_vy auf 0.0
        wenn p2_x < WAND_LINKS:
            setze p2_x auf WAND_LINKS * 1.0
        wenn p2_x > WAND_RECHTS:
            setze p2_x auf WAND_RECHTS * 1.0

        # === SCHLAG-TIMER ===
        wenn p1_schlag > 0:
            setze p1_schlag auf p1_schlag - 1
            wenn p1_schlag == 0:
                setze p1_hit_cooldown auf 8
        wenn p1_hit_cooldown > 0:
            setze p1_hit_cooldown auf p1_hit_cooldown - 1

        wenn p2_schlag > 0:
            setze p2_schlag auf p2_schlag - 1
            wenn p2_schlag == 0:
                setze p2_hit_cooldown auf 8
        wenn p2_hit_cooldown > 0:
            setze p2_hit_cooldown auf p2_hit_cooldown - 1

        # === TREFFER-ERKENNUNG ===
        # P1 schlägt P2
        wenn p1_schlag > 0 und p1_schlag < SCHLAG_DAUER - 2:
            setze faust_x auf p1_x + p1_richtung * (KOERPER_W / 2 + FAUST_W / 2)
            setze faust_y auf p1_y - KOERPER_H / 2 - FAUST_H / 2
            wenn aabb_kollidiert(faust_x - FAUST_W / 2, faust_y, FAUST_W, FAUST_H, p2_x - KOERPER_W / 2, p2_y - KOERPER_H, KOERPER_W, KOERPER_H):
                wenn p2_block:
                    setze p2_hp auf p2_hp - BLOCK_SCHADEN
                    setze p2_x auf p2_x + p1_richtung * KNOCKBACK / 3
                sonst:
                    setze p2_hp auf p2_hp - SCHLAG_SCHADEN
                    setze p2_x auf p2_x + p1_richtung * KNOCKBACK
                setze p1_schlag auf 0

        # P2 schlägt P1
        wenn p2_schlag > 0 und p2_schlag < SCHLAG_DAUER - 2:
            setze faust_x auf p2_x + p2_richtung * (KOERPER_W / 2 + FAUST_W / 2)
            setze faust_y auf p2_y - KOERPER_H / 2 - FAUST_H / 2
            wenn aabb_kollidiert(faust_x - FAUST_W / 2, faust_y, FAUST_W, FAUST_H, p1_x - KOERPER_W / 2, p1_y - KOERPER_H, KOERPER_W, KOERPER_H):
                wenn p1_block:
                    setze p1_hp auf p1_hp - BLOCK_SCHADEN
                    setze p1_x auf p1_x + p2_richtung * KNOCKBACK / 3
                sonst:
                    setze p1_hp auf p1_hp - SCHLAG_SCHADEN
                    setze p1_x auf p1_x + p2_richtung * KNOCKBACK
                setze p2_schlag auf 0

        # === RUNDE VORBEI? ===
        wenn p1_hp <= 0:
            setze p1_hp auf 0
            setze runde_vorbei auf wahr
        wenn p2_hp <= 0:
            setze p2_hp auf 0
            setze runde_vorbei auf wahr

        # Blickrichtung zum Gegner
        wenn p1_x < p2_x:
            setze p1_richtung auf 1
            setze p2_richtung auf -1
        sonst:
            setze p1_richtung auf -1
            setze p2_richtung auf 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#1A1A2E")

    # Boden
    zeichne_rechteck(win, 0, BODEN_Y + 20, BREITE, HOEHE - BODEN_Y - 20, "#3E2723")
    # Boden-Linie
    zeichne_linie(win, 0, BODEN_Y + 20, BREITE, BODEN_Y + 20, "#5D4037")

    # HP-Balken Hintergrund
    zeichne_rechteck(win, 30, 20, HP_BALKEN_W, HP_BALKEN_H, "#424242")
    zeichne_rechteck(win, BREITE - 30 - HP_BALKEN_W, 20, HP_BALKEN_W, HP_BALKEN_H, "#424242")

    # HP-Balken (grün→gelb→rot basierend auf HP)
    setze p1_bar_w auf p1_hp * HP_BALKEN_W / MAX_HP
    setze p2_bar_w auf p2_hp * HP_BALKEN_W / MAX_HP
    setze p1_farbe auf "#4CAF50"
    wenn p1_hp < 30:
        setze p1_farbe auf "#F44336"
    sonst:
        wenn p1_hp < 60:
            setze p1_farbe auf "#FF9800"
    setze p2_farbe auf "#4CAF50"
    wenn p2_hp < 30:
        setze p2_farbe auf "#F44336"
    sonst:
        wenn p2_hp < 60:
            setze p2_farbe auf "#FF9800"
    zeichne_rechteck(win, 30, 20, p1_bar_w, HP_BALKEN_H, p1_farbe)
    zeichne_rechteck(win, BREITE - 30 - p2_bar_w, 20, p2_bar_w, HP_BALKEN_H, p2_farbe)

    # Spieler-Label (Kreise als P1/P2 Marker)
    zeichne_kreis(win, 15, 30, 8, "#42A5F5")
    zeichne_kreis(win, BREITE - 15, 30, 8, "#EF5350")

    # Kämpfer zeichnen
    zeichne_kaempfer(win, p1_x, p1_y, p1_richtung, "#1565C0", "#42A5F5", p1_schlag, p1_block, p1_spr_idle, p1_spr_walk, p1_spr_jump, p1_spr_duck, p1_y >= BODEN_Y)
    zeichne_kaempfer(win, p2_x, p2_y, p2_richtung, "#B71C1C", "#EF5350", p2_schlag, p2_block, p2_spr_idle, p2_spr_walk, p2_spr_jump, p2_spr_duck, p2_y >= BODEN_Y)

    # Runde vorbei — Gewinner anzeigen
    wenn runde_vorbei:
        # Großer Kreis in Gewinner-Farbe
        wenn p1_hp > p2_hp:
            zeichne_kreis(win, BREITE / 2, HOEHE / 2 - 50, 60, "#42A5F5")
        sonst:
            zeichne_kreis(win, BREITE / 2, HOEHE / 2 - 50, 60, "#EF5350")
        # "Restart" Hinweis: 3 kleine Kreise
        zeichne_kreis(win, BREITE / 2 - 20, HOEHE / 2 + 40, 5, "#FFFFFF")
        zeichne_kreis(win, BREITE / 2, HOEHE / 2 + 40, 5, "#FFFFFF")
        zeichne_kreis(win, BREITE / 2 + 20, HOEHE / 2 + 40, 5, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn p1_hp > p2_hp:
    zeige "SPIELER 1 (BLAU) GEWINNT!"
sonst:
    wenn p2_hp > p1_hp:
        zeige "SPIELER 2 (ROT) GEWINNT!"
    sonst:
        zeige "UNENTSCHIEDEN!"

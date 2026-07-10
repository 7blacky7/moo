# ============================================================
# fighter.moo — 2-Spieler Kampfspiel (geteilte Tastatur)
#
# ZIEL: Bring die HP deines Gegners auf 0 — Best-of-Rounds
#       entscheidet ihr selbst, R startet die nächste Runde!
#
# Start: moo-compiler run beispiele/fighter.moo
#
# Spieler 1 (BLAU):  A/D=Bewegen, W=Springen, F=Schlagen, G=Blocken
# Spieler 2 (ROT):   Links/Rechts=Bewegen, Hoch=Springen, K=Schlagen, L=Blocken
# R = Neue Runde, Escape = Beenden
#
# Aufbau: 1. Pixelfont  2. Zustands-Dict S (Globals-Shadowing!
#   siehe zelda.moo)  3. spiel_schritt(i1, i2) — headless testbar
#   (fighter_selftest.moo)  4. Zeichnen  5. Loop (Env-Guard
#   FIGHTER_SELFTEST)
# ============================================================

konstante BREITE auf 800
konstante HOEHE auf 600
konstante BODEN_Y auf 480
konstante WAND_LINKS auf 30
konstante WAND_RECHTS auf 770
konstante KOERPER_W auf 40
konstante KOERPER_H auf 60
konstante FAUST_W auf 30
konstante FAUST_H auf 12
konstante GESCHWINDIGKEIT auf 4
konstante SPRUNG_KRAFT auf 10.0
konstante GRAVITATION auf 0.5
konstante SCHLAG_DAUER auf 12
konstante SCHLAG_SCHADEN auf 10
konstante BLOCK_SCHADEN auf 4
konstante KNOCKBACK auf 15
konstante MAX_HP auf 100
konstante HP_BALKEN_W auf 300
konstante HP_BALKEN_H auf 20

# ============================================================
# 1. Pixelfont (3x5) — nur die Zeichen, die wir anzeigen
# ============================================================
setze FONT auf {}
FONT[" "] = [0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0]
FONT["1"] = [0,1,0, 1,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["2"] = [1,1,1, 0,0,1, 1,1,1, 1,0,0, 1,1,1]
FONT["C"] = [1,1,1, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["D"] = [1,1,0, 1,0,1, 1,0,1, 1,0,1, 1,1,0]
FONT["E"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1]
FONT["G"] = [1,1,1, 1,0,0, 1,0,1, 1,0,1, 1,1,1]
FONT["I"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["K"] = [1,0,1, 1,1,0, 1,0,0, 1,1,0, 1,0,1]
FONT["N"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT["P"] = [1,1,1, 1,0,1, 1,1,1, 1,0,0, 1,0,0]
FONT["R"] = [1,1,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1]
FONT["T"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,0]
FONT["U"] = [1,0,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["W"] = [1,0,1, 1,0,1, 1,1,1, 1,1,1, 1,0,1]

funktion zeichne_bitmap(win, bits, x, y, px, farbe):
    setze zy auf 0
    solange zy < 5:
        setze zx auf 0
        solange zx < 3:
            wenn bits[zy * 3 + zx] == 1:
                zeichne_rechteck(win, x + zx * px, y + zy * px, px, px, farbe)
            setze zx auf zx + 1
        setze zy auf zy + 1

funktion zeichne_text_px(win, s, x, y, px, farbe):
    setze i auf 0
    setze cx auf x
    solange i < länge(s):
        setze ch auf s[i]
        wenn FONT.enthält(ch):
            zeichne_bitmap(win, FONT[ch], cx, y, px, farbe)
        setze cx auf cx + 4 * px
        setze i auf i + 1

# ============================================================
# 2. Zustand + Kollision
#    modus: 0=kampf, 1=runde vorbei (gewinner 1/2)
# ============================================================
setze S auf {}

funktion neustart():
    S["p1_x"] = 200.0
    S["p1_y"] = BODEN_Y * 1.0
    S["p1_vy"] = 0.0
    S["p1_hp"] = MAX_HP
    S["p1_schlag"] = 0
    S["p1_block"] = falsch
    S["p1_richtung"] = 1
    S["p1_cd"] = 0
    S["p2_x"] = 560.0
    S["p2_y"] = BODEN_Y * 1.0
    S["p2_vy"] = 0.0
    S["p2_hp"] = MAX_HP
    S["p2_schlag"] = 0
    S["p2_block"] = falsch
    S["p2_richtung"] = -1
    S["p2_cd"] = 0
    S["modus"] = 0
    S["gewinner"] = 0

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

# ============================================================
# 3. Spiellogik — ein Frame pro Aufruf, komplett headless testbar.
#    i1/i2 = {"links","rechts","hoch","schlag","block","neustart"}
# ============================================================
funktion spiel_schritt(i1, i2):
    wenn S["modus"] != 0:
        wenn i1["neustart"] oder i2["neustart"]:
            neustart()
        gib_zurück 0

    # --- Input Spieler 1 ---
    S["p1_block"] = i1["block"]
    wenn i1["links"]:
        S["p1_x"] = S["p1_x"] - GESCHWINDIGKEIT
        S["p1_richtung"] = -1
    wenn i1["rechts"]:
        S["p1_x"] = S["p1_x"] + GESCHWINDIGKEIT
        S["p1_richtung"] = 1
    wenn i1["hoch"] und S["p1_y"] >= BODEN_Y:
        S["p1_vy"] = 0 - SPRUNG_KRAFT
    wenn i1["schlag"] und S["p1_schlag"] == 0 und S["p1_cd"] == 0:
        S["p1_schlag"] = SCHLAG_DAUER

    # --- Input Spieler 2 ---
    S["p2_block"] = i2["block"]
    wenn i2["links"]:
        S["p2_x"] = S["p2_x"] - GESCHWINDIGKEIT
        S["p2_richtung"] = -1
    wenn i2["rechts"]:
        S["p2_x"] = S["p2_x"] + GESCHWINDIGKEIT
        S["p2_richtung"] = 1
    wenn i2["hoch"] und S["p2_y"] >= BODEN_Y:
        S["p2_vy"] = 0 - SPRUNG_KRAFT
    wenn i2["schlag"] und S["p2_schlag"] == 0 und S["p2_cd"] == 0:
        S["p2_schlag"] = SCHLAG_DAUER

    # --- Physik + Arena-Grenzen ---
    S["p1_vy"] = S["p1_vy"] + GRAVITATION
    S["p1_y"] = S["p1_y"] + S["p1_vy"]
    wenn S["p1_y"] > BODEN_Y:
        S["p1_y"] = BODEN_Y * 1.0
        S["p1_vy"] = 0.0
    wenn S["p1_x"] < WAND_LINKS:
        S["p1_x"] = WAND_LINKS * 1.0
    wenn S["p1_x"] > WAND_RECHTS:
        S["p1_x"] = WAND_RECHTS * 1.0
    S["p2_vy"] = S["p2_vy"] + GRAVITATION
    S["p2_y"] = S["p2_y"] + S["p2_vy"]
    wenn S["p2_y"] > BODEN_Y:
        S["p2_y"] = BODEN_Y * 1.0
        S["p2_vy"] = 0.0
    wenn S["p2_x"] < WAND_LINKS:
        S["p2_x"] = WAND_LINKS * 1.0
    wenn S["p2_x"] > WAND_RECHTS:
        S["p2_x"] = WAND_RECHTS * 1.0

    # --- Schlag-Timer + Cooldowns ---
    wenn S["p1_schlag"] > 0:
        S["p1_schlag"] = S["p1_schlag"] - 1
        wenn S["p1_schlag"] == 0:
            S["p1_cd"] = 8
    wenn S["p1_cd"] > 0:
        S["p1_cd"] = S["p1_cd"] - 1
    wenn S["p2_schlag"] > 0:
        S["p2_schlag"] = S["p2_schlag"] - 1
        wenn S["p2_schlag"] == 0:
            S["p2_cd"] = 8
    wenn S["p2_cd"] > 0:
        S["p2_cd"] = S["p2_cd"] - 1

    # --- Treffer: P1 schlägt P2 ---
    wenn S["p1_schlag"] > 0 und S["p1_schlag"] < SCHLAG_DAUER - 2:
        setze faust_x auf S["p1_x"] + S["p1_richtung"] * (KOERPER_W / 2 + FAUST_W / 2)
        setze faust_y auf S["p1_y"] - KOERPER_H / 2 - FAUST_H / 2
        wenn aabb_kollidiert(faust_x - FAUST_W / 2, faust_y, FAUST_W, FAUST_H, S["p2_x"] - KOERPER_W / 2, S["p2_y"] - KOERPER_H, KOERPER_W, KOERPER_H):
            wenn S["p2_block"]:
                S["p2_hp"] = S["p2_hp"] - BLOCK_SCHADEN
                S["p2_x"] = S["p2_x"] + S["p1_richtung"] * KNOCKBACK / 3
            sonst:
                S["p2_hp"] = S["p2_hp"] - SCHLAG_SCHADEN
                S["p2_x"] = S["p2_x"] + S["p1_richtung"] * KNOCKBACK
            S["p1_schlag"] = 0

    # --- Treffer: P2 schlägt P1 ---
    wenn S["p2_schlag"] > 0 und S["p2_schlag"] < SCHLAG_DAUER - 2:
        setze faust_x auf S["p2_x"] + S["p2_richtung"] * (KOERPER_W / 2 + FAUST_W / 2)
        setze faust_y auf S["p2_y"] - KOERPER_H / 2 - FAUST_H / 2
        wenn aabb_kollidiert(faust_x - FAUST_W / 2, faust_y, FAUST_W, FAUST_H, S["p1_x"] - KOERPER_W / 2, S["p1_y"] - KOERPER_H, KOERPER_W, KOERPER_H):
            wenn S["p1_block"]:
                S["p1_hp"] = S["p1_hp"] - BLOCK_SCHADEN
                S["p1_x"] = S["p1_x"] + S["p2_richtung"] * KNOCKBACK / 3
            sonst:
                S["p1_hp"] = S["p1_hp"] - SCHLAG_SCHADEN
                S["p1_x"] = S["p1_x"] + S["p2_richtung"] * KNOCKBACK
            S["p2_schlag"] = 0

    # --- Runde vorbei? ---
    wenn S["p1_hp"] <= 0:
        S["p1_hp"] = 0
        S["modus"] = 1
        S["gewinner"] = 2
    wenn S["p2_hp"] <= 0:
        S["p2_hp"] = 0
        S["modus"] = 1
        S["gewinner"] = 1

    # --- Blickrichtung zum Gegner ---
    wenn S["p1_x"] < S["p2_x"]:
        S["p1_richtung"] = 1
        S["p2_richtung"] = -1
    sonst:
        S["p1_richtung"] = -1
        S["p2_richtung"] = 1
    gib_zurück 0

# ============================================================
# 4. Zeichnen (Sprites kommen als Dict, damit der Selftest sie
#    selbst laden kann)
# ============================================================
funktion zeichne_kaempfer(win, sprs, praefix, x, y, richtung, schlag_timer, blockt, am_boden):
    setze spr auf sprs[praefix + "_idle"]
    wenn am_boden == falsch:
        setze spr auf sprs[praefix + "_jump"]
    wenn blockt:
        setze spr auf sprs[praefix + "_duck"]
    wenn schlag_timer > 0:
        setze spr auf sprs[praefix + "_walk"]
    sprite_zeichnen_skaliert(win, spr, x - 24, y - KOERPER_H - 10, 48, 70)
    wenn schlag_timer > 0:
        setze faust_x auf x + richtung * (KOERPER_W / 2 + FAUST_W / 2)
        setze faust_y auf y - KOERPER_H / 2 - FAUST_H / 2
        zeichne_rechteck(win, faust_x - FAUST_W / 2, faust_y, FAUST_W, FAUST_H, "#FFEB3B")
    wenn blockt:
        setze schild_x auf x + richtung * (KOERPER_W / 2 + 5)
        zeichne_rechteck(win, schild_x - 3, y - KOERPER_H + 5, 6, KOERPER_H - 10, "#90CAF9")

funktion welt_zeichnen(win, sprs):
    fenster_löschen(win, "#1A1A2E")
    zeichne_rechteck(win, 0, BODEN_Y + 20, BREITE, HOEHE - BODEN_Y - 20, "#3E2723")
    zeichne_linie(win, 0, BODEN_Y + 20, BREITE, BODEN_Y + 20, "#5D4037")

    # HP-Balken (grün→gelb→rot)
    zeichne_rechteck(win, 30, 20, HP_BALKEN_W, HP_BALKEN_H, "#424242")
    zeichne_rechteck(win, BREITE - 30 - HP_BALKEN_W, 20, HP_BALKEN_W, HP_BALKEN_H, "#424242")
    setze p1_bar_w auf S["p1_hp"] * HP_BALKEN_W / MAX_HP
    setze p2_bar_w auf S["p2_hp"] * HP_BALKEN_W / MAX_HP
    setze p1_farbe auf "#4CAF50"
    wenn S["p1_hp"] < 30:
        setze p1_farbe auf "#F44336"
    sonst wenn S["p1_hp"] < 60:
        setze p1_farbe auf "#FF9800"
    setze p2_farbe auf "#4CAF50"
    wenn S["p2_hp"] < 30:
        setze p2_farbe auf "#F44336"
    sonst wenn S["p2_hp"] < 60:
        setze p2_farbe auf "#FF9800"
    zeichne_rechteck(win, 30, 20, p1_bar_w, HP_BALKEN_H, p1_farbe)
    zeichne_rechteck(win, BREITE - 30 - p2_bar_w, 20, p2_bar_w, HP_BALKEN_H, p2_farbe)
    zeichne_kreis(win, 15, 30, 8, "#42A5F5")
    zeichne_kreis(win, BREITE - 15, 30, 8, "#EF5350")

    zeichne_kaempfer(win, sprs, "p1", S["p1_x"], S["p1_y"], S["p1_richtung"], S["p1_schlag"], S["p1_block"], S["p1_y"] >= BODEN_Y)
    zeichne_kaempfer(win, sprs, "p2", S["p2_x"], S["p2_y"], S["p2_richtung"], S["p2_schlag"], S["p2_block"], S["p2_y"] >= BODEN_Y)

    # Runden-Ende: Sieger-Overlay mit Text + Neustart-Hinweis
    wenn S["modus"] == 1:
        wenn S["gewinner"] == 1:
            zeichne_rechteck(win, 150, 200, 500, 140, "#0D2B4E")
            zeichne_text_px(win, "P1 GEWINNT", 220, 230, 6, "#42A5F5")
        sonst:
            zeichne_rechteck(win, 150, 200, 500, 140, "#4E0D0D")
            zeichne_text_px(win, "P2 GEWINNT", 220, 230, 6, "#EF5350")
        zeichne_text_px(win, "DRUECKE R", 290, 300, 3, "#FFFFFF")
    gib_zurück 0

neustart()

# ============================================================
# 5. Interaktiver Loop (im Selftest per Env-Variable übersprungen)
# ============================================================
wenn umgebung("FIGHTER_SELFTEST") == nichts:
    zeige "=== moo Fighter ==="
    zeige "ZIEL: Bring die HP deines Gegners auf 0!"
    zeige "P1: A/D/W, F=Schlag, G=Block | P2: Pfeile, K=Schlag, L=Block | R=Neue Runde"

    setze win auf fenster_erstelle("moo Fighter", BREITE, HOEHE)
    setze spr_pfad auf "beispiele/assets/sprites/kenney_platformer/PNG/Characters/"
    setze sprs auf {}
    sprs["p1_idle"] = sprite_laden(win, spr_pfad + "platformChar_idle.png")
    sprs["p1_walk"] = sprite_laden(win, spr_pfad + "platformChar_walk1.png")
    sprs["p1_jump"] = sprite_laden(win, spr_pfad + "platformChar_jump.png")
    sprs["p1_duck"] = sprite_laden(win, spr_pfad + "platformChar_duck.png")
    sprs["p2_idle"] = sprite_laden(win, spr_pfad + "platformChar_happy.png")
    sprs["p2_walk"] = sprite_laden(win, spr_pfad + "platformChar_walk2.png")
    sprs["p2_jump"] = sprite_laden(win, spr_pfad + "platformChar_climb1.png")
    sprs["p2_duck"] = sprite_laden(win, spr_pfad + "platformChar_climb2.png")

    solange fenster_offen(win):
        wenn taste_gedrückt("escape"):
            stopp
        setze i1 auf {}
        i1["links"] = taste_gedrückt("a")
        i1["rechts"] = taste_gedrückt("d")
        i1["hoch"] = taste_gedrückt("w")
        i1["schlag"] = taste_gedrückt("f")
        i1["block"] = taste_gedrückt("g")
        i1["neustart"] = taste_gedrückt("r")
        setze i2 auf {}
        i2["links"] = taste_gedrückt("links")
        i2["rechts"] = taste_gedrückt("rechts")
        i2["hoch"] = taste_gedrückt("hoch")
        i2["schlag"] = taste_gedrückt("k")
        i2["block"] = taste_gedrückt("l")
        i2["neustart"] = taste_gedrückt("r")
        spiel_schritt(i1, i2)
        welt_zeichnen(win, sprs)
        fenster_aktualisieren(win)
        warte(16)

    fenster_schliessen(win)
    zeige "Fighter beendet."

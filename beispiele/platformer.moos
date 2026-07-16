# ============================================================
# moo Platformer — Mario-Style Side-Scroller
#
# Kompilieren: moo-compiler compile beispiele/platformer.moo -o beispiele/platformer
# Starten:     ./beispiele/platformer
#
# Bedienung:
#   Links/Rechts oder A/D - Laufen
#   Leertaste - Springen
#   Escape - Beenden
#
# Features: Schwerkraft, Plattformen, Feinde, Muenzen, Scrolling, Score
# ============================================================

# === KONSTANTEN ===
setze BREITE auf 800
setze HOEHE auf 600
setze SCHWERKRAFT auf 0.5
setze SPRUNGKRAFT auf -10.0
setze GESCHWINDIGKEIT auf 5
setze BODEN_Y auf 550

# === SPIELER ===
setze spieler_x auf 100.0
setze spieler_y auf 400.0
setze spieler_vy auf 0.0
setze spieler_w auf 24
setze spieler_h auf 36
setze auf_boden auf falsch
setze punkte auf 0

# === KAMERA ===
setze kamera_x auf 0.0

# === PLATTFORMEN (x, y, breite) ===
setze plat_x auf []
setze plat_y auf []
setze plat_w auf []

# Boden-Segmente
funktion boden_hinzufuegen(start_x, laenge):
    plat_x.hinzufügen(start_x)
    plat_y.hinzufügen(BODEN_Y)
    plat_w.hinzufügen(laenge)

# Plattform hinzufuegen
funktion plattform(x, y, w):
    plat_x.hinzufügen(x)
    plat_y.hinzufügen(y)
    plat_w.hinzufügen(w)

# Level bauen
boden_hinzufuegen(0, 400)
boden_hinzufuegen(500, 300)
boden_hinzufuegen(900, 600)
boden_hinzufuegen(1600, 400)
boden_hinzufuegen(2100, 500)

plattform(250, 450, 120)
plattform(450, 380, 100)
plattform(650, 320, 80)
plattform(850, 400, 150)
plattform(1100, 350, 100)
plattform(1300, 280, 120)
plattform(1500, 420, 100)
plattform(1750, 350, 80)
plattform(1950, 300, 100)
plattform(2200, 380, 120)
plattform(2450, 320, 80)

setze plat_anzahl auf länge(plat_x)

# === MUENZEN (x, y, eingesammelt) ===
setze muenz_x auf []
setze muenz_y auf []
setze muenz_aktiv auf []

funktion muenze(x, y):
    muenz_x.hinzufügen(x)
    muenz_y.hinzufügen(y)
    muenz_aktiv.hinzufügen(wahr)

muenze(300, 420)
muenze(470, 350)
muenze(680, 290)
muenze(900, 370)
muenze(950, 370)
muenze(1150, 320)
muenze(1350, 250)
muenze(1550, 390)
muenze(1800, 320)
muenze(2000, 270)
muenze(2250, 350)
muenze(2500, 290)

setze muenz_anzahl auf länge(muenz_x)

# === FEINDE (x, y, richtung, start_x, reichweite) ===
setze feind_x auf []
setze feind_y auf []
setze feind_richtung auf []
setze feind_start auf []
setze feind_reichweite auf []

funktion feind(x, y, reichweite):
    feind_x.hinzufügen(x * 1.0)
    feind_y.hinzufügen(y)
    feind_richtung.hinzufügen(1.0)
    feind_start.hinzufügen(x * 1.0)
    feind_reichweite.hinzufügen(reichweite * 1.0)

feind(600, 520, 150)
feind(1000, 520, 200)
feind(1700, 520, 100)
feind(2200, 520, 180)

setze feind_anzahl auf länge(feind_x)
setze leben auf 3
setze unverwundbar auf 0

# === KOLLISIONSERKENNUNG ===
funktion rechteck_kollidiert(ax, ay, aw, ah, bx, by, bw, bh):
    wenn ax + aw < bx:
        gib_zurück falsch
    wenn ax > bx + bw:
        gib_zurück falsch
    wenn ay + ah < by:
        gib_zurück falsch
    wenn ay > by + bh:
        gib_zurück falsch
    gib_zurück wahr

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Platformer", BREITE, HOEHE)

# === SPRITES LADEN ===
setze spr_pfad auf "beispiele/assets/sprites/kenney_platformer/PNG/"
setze spr_idle auf sprite_laden(win, spr_pfad + "Characters/platformChar_idle.png")
setze spr_walk1 auf sprite_laden(win, spr_pfad + "Characters/platformChar_walk1.png")
setze spr_walk2 auf sprite_laden(win, spr_pfad + "Characters/platformChar_walk2.png")
setze spr_jump auf sprite_laden(win, spr_pfad + "Characters/platformChar_jump.png")
setze spr_tile auf sprite_laden(win, spr_pfad + "Tiles/platformPack_tile001.png")
setze spr_tile_top auf sprite_laden(win, spr_pfad + "Tiles/platformPack_tile003.png")
setze spr_muenze auf sprite_laden(win, spr_pfad + "Items/platformPack_item001.png")
setze spr_feind auf sprite_laden(win, spr_pfad + "Characters/platformChar_happy.png")
setze walk_frame auf 0
setze walk_timer auf 0

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # === INPUT ===
    wenn taste_gedrückt("links") oder taste_gedrückt("a"):
        setze spieler_x auf spieler_x - GESCHWINDIGKEIT
    wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
        setze spieler_x auf spieler_x + GESCHWINDIGKEIT

    # Walk-Animation
    wenn taste_gedrückt("links") oder taste_gedrückt("a") oder taste_gedrückt("rechts") oder taste_gedrückt("d"):
        setze walk_timer auf walk_timer + 1
        wenn walk_timer > 8:
            setze walk_timer auf 0
            setze walk_frame auf 1 - walk_frame
    sonst:
        setze walk_frame auf 0
        setze walk_timer auf 0

    wenn taste_gedrückt("leertaste") und auf_boden:
        setze spieler_vy auf SPRUNGKRAFT
        setze auf_boden auf falsch

    # === PHYSIK ===
    setze spieler_vy auf spieler_vy + SCHWERKRAFT
    setze spieler_y auf spieler_y + spieler_vy

    # Plattform-Kollision
    setze auf_boden auf falsch
    setze i auf 0
    solange i < plat_anzahl:
        # Spieler faellt auf Plattform
        wenn spieler_vy >= 0:
            wenn spieler_x + spieler_w > plat_x[i]:
                wenn spieler_x < plat_x[i] + plat_w[i]:
                    wenn spieler_y + spieler_h >= plat_y[i]:
                        wenn spieler_y + spieler_h <= plat_y[i] + 20:
                            setze spieler_y auf (plat_y[i] - spieler_h) * 1.0
                            setze spieler_vy auf 0.0
                            setze auf_boden auf wahr
        setze i auf i + 1

    # Tod bei Fall
    wenn spieler_y > HOEHE + 50:
        setze leben auf leben - 1
        setze spieler_x auf 100.0
        setze spieler_y auf 400.0
        setze spieler_vy auf 0.0
        wenn leben <= 0:
            stopp

    # === FEINDE BEWEGEN ===
    setze i auf 0
    solange i < feind_anzahl:
        setze feind_x[i] auf feind_x[i] + feind_richtung[i] * 2.0
        wenn feind_x[i] > feind_start[i] + feind_reichweite[i]:
            setze feind_richtung[i] auf -1.0
        wenn feind_x[i] < feind_start[i]:
            setze feind_richtung[i] auf 1.0

        # Feind-Kollision
        wenn unverwundbar <= 0:
            wenn rechteck_kollidiert(spieler_x + 4, spieler_y + 4, spieler_w - 8, spieler_h - 8, feind_x[i] + 4, feind_y[i] + 4, 20, 20):
                setze leben auf leben - 1
                setze unverwundbar auf 60
                wenn leben <= 0:
                    stopp
        setze i auf i + 1

    wenn unverwundbar > 0:
        setze unverwundbar auf unverwundbar - 1

    # === MUENZEN EINSAMMELN ===
    setze i auf 0
    solange i < muenz_anzahl:
        wenn muenz_aktiv[i]:
            wenn rechteck_kollidiert(spieler_x, spieler_y, spieler_w, spieler_h, muenz_x[i] - 6, muenz_y[i] - 6, 12, 12):
                setze muenz_aktiv[i] auf falsch
                setze punkte auf punkte + 100
        setze i auf i + 1

    # === KAMERA ===
    setze kamera_x auf spieler_x - 300.0
    wenn kamera_x < 0:
        setze kamera_x auf 0.0

    # === ZEICHNEN ===
    fenster_löschen(win, "#87CEEB")

    # Plattformen (Tile-Sprites)
    setze i auf 0
    solange i < plat_anzahl:
        setze draw_x auf plat_x[i] - kamera_x
        wenn draw_x > -200:
            wenn draw_x < BREITE + 50:
                # Obere Kante mit Gras-Tile
                setze tx auf 0
                solange tx < plat_w[i]:
                    sprite_zeichnen_skaliert(win, spr_tile_top, draw_x + tx, plat_y[i], 32, 20)
                    setze tx auf tx + 32
        setze i auf i + 1

    # Muenzen (Sprites)
    setze i auf 0
    solange i < muenz_anzahl:
        wenn muenz_aktiv[i]:
            setze draw_x auf muenz_x[i] - kamera_x
            wenn draw_x > -20:
                wenn draw_x < BREITE + 20:
                    sprite_zeichnen_skaliert(win, spr_muenze, draw_x - 12, muenz_y[i] - 12, 24, 24)
        setze i auf i + 1

    # Feinde (Sprites)
    setze i auf 0
    solange i < feind_anzahl:
        setze draw_x auf feind_x[i] - kamera_x
        wenn draw_x > -30:
            wenn draw_x < BREITE + 30:
                sprite_zeichnen_skaliert(win, spr_feind, draw_x, feind_y[i], 28, 28)
        setze i auf i + 1

    # Spieler
    # Spieler (animierte Sprites)
    setze draw_spieler_x auf spieler_x - kamera_x
    setze spr_aktuell auf spr_idle
    wenn nicht auf_boden:
        setze spr_aktuell auf spr_jump
    sonst:
        wenn taste_gedrückt("links") oder taste_gedrückt("a") oder taste_gedrückt("rechts") oder taste_gedrückt("d"):
            wenn walk_frame == 0:
                setze spr_aktuell auf spr_walk1
            sonst:
                setze spr_aktuell auf spr_walk2
    wenn unverwundbar > 0:
        wenn (unverwundbar / 4) % 2 == 0:
            sprite_zeichnen_skaliert(win, spr_aktuell, draw_spieler_x - 4, spieler_y - 10, 32, 48)
    sonst:
        sprite_zeichnen_skaliert(win, spr_aktuell, draw_spieler_x - 4, spieler_y - 10, 32, 48)

    # HUD: Leben
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, 20 + li * 25, 20, 8, "#F44336")
        setze li auf li + 1

    # HUD: Muenzen/Score als gelbe Kreise
    setze score_icons auf punkte / 100
    setze si auf 0
    solange si < score_icons:
        zeichne_kreis(win, 20 + si * 15, 45, 5, "#FFD700")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn leben <= 0:
    zeige "GAME OVER! Punkte: " + text(punkte)
sonst:
    zeige "GEWONNEN! Punkte: " + text(punkte)

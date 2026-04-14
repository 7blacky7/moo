# ============================================================
# moo Artillery — Worms/Scorched Earth Style
#
# Kompilieren: moo-compiler compile beispiele/artillery.moo -o beispiele/artillery
# Starten:     ./beispiele/artillery
#
# Links/Rechts = Winkel aendern
# Hoch/Runter = Kraft aendern
# Leertaste = Schiessen
# Escape = Beenden
# ============================================================

setze BREITE auf 800
setze HOEHE auf 500
setze MAX_TERRAIN auf 800

# Terrain (Hoehenlinie)
setze terrain auf []
setze ti auf 0
solange ti < MAX_TERRAIN:
    # Huegelige Landschaft
    setze h1 auf sinus(ti * 0.02) * 80
    setze h2 auf sinus(ti * 0.05 + 2) * 40
    setze h3 auf sinus(ti * 0.01 + 5) * 60
    setze hoehe auf 300 + h1 + h2 + h3
    terrain.hinzufügen(hoehe)
    setze ti auf ti + 1

# Spieler 1 (links)
setze p1_x auf 100
setze p1_winkel auf 45.0
setze p1_kraft auf 8.0
setze p1_hp auf 100

# Spieler 2 (rechts)
setze p2_x auf 700
setze p2_winkel auf 135.0
setze p2_kraft auf 8.0
setze p2_hp auf 100

# Wer ist dran? 1 oder 2
setze aktiver auf 1

# Projektil
setze proj_x auf 0.0
setze proj_y auf 0.0
setze proj_vx auf 0.0
setze proj_vy auf 0.0
setze proj_aktiv auf falsch

setze eingabe_cd auf 0
setze wind auf 0.02

funktion terrain_hoehe(px):
    wenn px < 0:
        gib_zurück 400
    wenn px >= MAX_TERRAIN:
        gib_zurück 400
    gib_zurück terrain[px]

funktion terrain_zerstoeren(cx, radius):
    setze ti auf cx - radius
    solange ti <= cx + radius:
        wenn ti >= 0 und ti < MAX_TERRAIN:
            setze dist auf ti - cx
            wenn dist < 0:
                setze dist auf 0 - dist
            setze tiefe auf radius - dist
            wenn tiefe > 0:
                setze terrain[ti] auf terrain[ti] + tiefe * 1.5
        setze ti auf ti + 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Artillery", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn p1_hp <= 0 oder p2_hp <= 0:
        stopp

    wenn nicht proj_aktiv und eingabe_cd <= 0:
        # Winkel + Kraft aendern
        wenn aktiver == 1:
            wenn taste_gedrückt("links"):
                setze p1_winkel auf p1_winkel + 1
                setze eingabe_cd auf 3
            wenn taste_gedrückt("rechts"):
                setze p1_winkel auf p1_winkel - 1
                setze eingabe_cd auf 3
            wenn taste_gedrückt("oben"):
                setze p1_kraft auf p1_kraft + 0.3
                setze eingabe_cd auf 3
            wenn taste_gedrückt("unten"):
                setze p1_kraft auf p1_kraft - 0.3
                setze eingabe_cd auf 3
            wenn p1_kraft < 2:
                setze p1_kraft auf 2.0
            wenn p1_kraft > 15:
                setze p1_kraft auf 15.0
            wenn taste_gedrückt("leertaste"):
                setze rad auf p1_winkel * 3.14159 / 180.0
                setze proj_x auf p1_x * 1.0
                setze proj_y auf terrain_hoehe(p1_x) - 15.0
                setze proj_vx auf cosinus(rad) * p1_kraft
                setze proj_vy auf 0 - sinus(rad) * p1_kraft
                setze proj_aktiv auf wahr
                setze eingabe_cd auf 20
        sonst:
            wenn taste_gedrückt("links"):
                setze p2_winkel auf p2_winkel + 1
                setze eingabe_cd auf 3
            wenn taste_gedrückt("rechts"):
                setze p2_winkel auf p2_winkel - 1
                setze eingabe_cd auf 3
            wenn taste_gedrückt("oben"):
                setze p2_kraft auf p2_kraft + 0.3
                setze eingabe_cd auf 3
            wenn taste_gedrückt("unten"):
                setze p2_kraft auf p2_kraft - 0.3
                setze eingabe_cd auf 3
            wenn p2_kraft < 2:
                setze p2_kraft auf 2.0
            wenn p2_kraft > 15:
                setze p2_kraft auf 15.0
            wenn taste_gedrückt("leertaste"):
                setze rad auf p2_winkel * 3.14159 / 180.0
                setze proj_x auf p2_x * 1.0
                setze proj_y auf terrain_hoehe(p2_x) - 15.0
                setze proj_vx auf cosinus(rad) * p2_kraft
                setze proj_vy auf 0 - sinus(rad) * p2_kraft
                setze proj_aktiv auf wahr
                setze eingabe_cd auf 20

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    # Projektil-Physik
    wenn proj_aktiv:
        setze proj_vx auf proj_vx + wind
        setze proj_vy auf proj_vy + 0.15
        setze proj_x auf proj_x + proj_vx
        setze proj_y auf proj_y + proj_vy

        # Terrain-Kollision
        setze px_int auf proj_x
        wenn px_int >= 0 und px_int < MAX_TERRAIN:
            wenn proj_y >= terrain_hoehe(px_int):
                # Explosion!
                terrain_zerstoeren(px_int, 20)
                # Schaden an Spielern
                setze d1 auf p1_x - px_int
                wenn d1 < 0:
                    setze d1 auf 0 - d1
                wenn d1 < 25:
                    setze schaden auf 30 - d1
                    setze p1_hp auf p1_hp - schaden

                setze d2 auf p2_x - px_int
                wenn d2 < 0:
                    setze d2 auf 0 - d2
                wenn d2 < 25:
                    setze schaden auf 30 - d2
                    setze p2_hp auf p2_hp - schaden

                setze proj_aktiv auf falsch
                # Spieler wechseln
                wenn aktiver == 1:
                    setze aktiver auf 2
                sonst:
                    setze aktiver auf 1
                # Neuer Wind
                setze wind auf (sinus(p1_hp * 0.1 + p2_hp * 0.07) * 0.04)

        # Aus dem Bild
        wenn proj_x < -50 oder proj_x > BREITE + 50 oder proj_y > HOEHE + 50:
            setze proj_aktiv auf falsch
            wenn aktiver == 1:
                setze aktiver auf 2
            sonst:
                setze aktiver auf 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#87CEEB")

    # Wolken
    zeichne_kreis(win, 150, 50, 30, "#FFFFFF")
    zeichne_kreis(win, 180, 40, 35, "#FFFFFF")
    zeichne_kreis(win, 210, 50, 25, "#FFFFFF")
    zeichne_kreis(win, 500, 60, 25, "#FFFFFF")
    zeichne_kreis(win, 530, 55, 30, "#FFFFFF")

    # Terrain
    setze ti auf 0
    solange ti < MAX_TERRAIN - 1:
        setze th auf terrain[ti]
        zeichne_linie(win, ti, th, ti, HOEHE, "#4CAF50")
        setze ti auf ti + 1

    # Gras-Oberflaeche
    setze ti auf 0
    solange ti < MAX_TERRAIN - 1:
        zeichne_linie(win, ti, terrain[ti], ti + 1, terrain[ti + 1], "#2E7D32")
        setze ti auf ti + 1

    # Spieler 1 (Panzer)
    setze p1_ty auf terrain_hoehe(p1_x)
    zeichne_rechteck(win, p1_x - 12, p1_ty - 10, 24, 10, "#1565C0")
    zeichne_kreis(win, p1_x, p1_ty - 12, 8, "#42A5F5")
    # Kanone
    setze rad auf p1_winkel * 3.14159 / 180.0
    setze kx auf p1_x + cosinus(rad) * 18
    setze ky auf p1_ty - 12 - sinus(rad) * 18
    zeichne_linie(win, p1_x, p1_ty - 12, kx, ky, "#90CAF9")

    # Spieler 2 (Panzer)
    setze p2_ty auf terrain_hoehe(p2_x)
    zeichne_rechteck(win, p2_x - 12, p2_ty - 10, 24, 10, "#C62828")
    zeichne_kreis(win, p2_x, p2_ty - 12, 8, "#EF5350")
    setze rad auf p2_winkel * 3.14159 / 180.0
    setze kx auf p2_x + cosinus(rad) * 18
    setze ky auf p2_ty - 12 - sinus(rad) * 18
    zeichne_linie(win, p2_x, p2_ty - 12, kx, ky, "#FFCDD2")

    # Projektil
    wenn proj_aktiv:
        zeichne_kreis(win, proj_x, proj_y, 4, "#FF5722")
        zeichne_kreis(win, proj_x, proj_y, 2, "#FFEB3B")

    # HUD
    # P1 HP
    zeichne_rechteck(win, 10, 10, 102, 14, "#333333")
    setze hp1w auf p1_hp
    wenn hp1w < 0:
        setze hp1w auf 0
    zeichne_rechteck(win, 11, 11, hp1w, 12, "#42A5F5")

    # P2 HP
    zeichne_rechteck(win, BREITE - 112, 10, 102, 14, "#333333")
    setze hp2w auf p2_hp
    wenn hp2w < 0:
        setze hp2w auf 0
    zeichne_rechteck(win, BREITE - 111, 11, hp2w, 12, "#EF5350")

    # Aktiver Spieler
    wenn aktiver == 1:
        zeichne_kreis(win, 60, 35, 6, "#42A5F5")
    sonst:
        zeichne_kreis(win, BREITE - 60, 35, 6, "#EF5350")

    # Kraft-Anzeige
    wenn aktiver == 1:
        zeichne_rechteck(win, 10, 40, p1_kraft * 8, 6, "#81C784")
    sonst:
        zeichne_rechteck(win, BREITE - 130, 40, p2_kraft * 8, 6, "#81C784")

    # Wind-Anzeige
    setze wind_px auf BREITE / 2
    wenn wind > 0:
        zeichne_rechteck(win, wind_px, 15, wind * 500, 4, "#FFFFFF")
    sonst:
        zeichne_rechteck(win, wind_px + wind * 500, 15, (0 - wind) * 500, 4, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn p1_hp > p2_hp:
    zeige "Spieler 1 (Blau) gewinnt!"
sonst:
    zeige "Spieler 2 (Rot) gewinnt!"

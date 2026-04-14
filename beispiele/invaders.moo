# ============================================================
# moo Space Invaders
#
# Kompilieren: moo-compiler compile beispiele/invaders.moo -o beispiele/invaders
# Starten:     ./beispiele/invaders
#
# Links/Rechts oder A/D = Bewegen
# Leertaste = Schiessen
# Escape = Beenden
# ============================================================

setze BREITE auf 600
setze HOEHE auf 700
setze SPIELER_W auf 30
setze SPIELER_H auf 20
setze MAX_SCHUESSE auf 5
setze MAX_FEIND_SCHUESSE auf 10
setze FEIND_SPALTEN auf 8
setze FEIND_REIHEN auf 5

# Spieler
setze spieler_x auf 285.0
setze punkte auf 0
setze leben auf 3

# Feinde
setze feind_x auf []
setze feind_y auf []
setze feind_aktiv auf []
setze feind_typ auf []

setze ry auf 0
solange ry < FEIND_REIHEN:
    setze rx auf 0
    solange rx < FEIND_SPALTEN:
        feind_x.hinzufügen(80 + rx * 55)
        feind_y.hinzufügen(60 + ry * 40)
        feind_aktiv.hinzufügen(wahr)
        feind_typ.hinzufügen(ry)
        setze rx auf rx + 1
    setze ry auf ry + 1

setze feind_total auf FEIND_REIHEN * FEIND_SPALTEN
setze feind_dir auf 1.0
setze feind_speed auf 0.5
setze feind_timer auf 0

# Spieler-Schuesse
setze sch_x auf []
setze sch_y auf []
setze sch_aktiv auf []
setze sci auf 0
solange sci < MAX_SCHUESSE:
    sch_x.hinzufügen(0.0)
    sch_y.hinzufügen(0.0)
    sch_aktiv.hinzufügen(falsch)
    setze sci auf sci + 1

# Feind-Schuesse
setze fsch_x auf []
setze fsch_y auf []
setze fsch_aktiv auf []
setze fsci auf 0
solange fsci < MAX_FEIND_SCHUESSE:
    fsch_x.hinzufügen(0.0)
    fsch_y.hinzufügen(0.0)
    fsch_aktiv.hinzufügen(falsch)
    setze fsci auf fsci + 1

setze schuss_cooldown auf 0
setze feind_schuss_timer auf 0

funktion feind_farbe(typ):
    wenn typ == 0:
        gib_zurück "#F44336"
    wenn typ == 1:
        gib_zurück "#FF9800"
    wenn typ == 2:
        gib_zurück "#FFEB3B"
    wenn typ == 3:
        gib_zurück "#4CAF50"
    gib_zurück "#2196F3"

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Space Invaders", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # === INPUT ===
    wenn taste_gedrückt("links") oder taste_gedrückt("a"):
        setze spieler_x auf spieler_x - 5
    wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
        setze spieler_x auf spieler_x + 5

    wenn spieler_x < 0:
        setze spieler_x auf 0.0
    wenn spieler_x > BREITE - SPIELER_W:
        setze spieler_x auf (BREITE - SPIELER_W) * 1.0

    # Schiessen
    wenn taste_gedrückt("leertaste") und schuss_cooldown <= 0:
        setze sci auf 0
        solange sci < MAX_SCHUESSE:
            wenn nicht sch_aktiv[sci]:
                setze sch_x[sci] auf spieler_x + SPIELER_W / 2
                setze sch_y[sci] auf 650.0
                setze sch_aktiv[sci] auf wahr
                setze schuss_cooldown auf 12
                setze sci auf MAX_SCHUESSE
            setze sci auf sci + 1

    wenn schuss_cooldown > 0:
        setze schuss_cooldown auf schuss_cooldown - 1

    # === FEINDE BEWEGEN ===
    setze feind_timer auf feind_timer + 1
    wenn feind_timer > 3:
        setze feind_timer auf 0
        setze muss_runter auf falsch
        setze fi auf 0
        solange fi < feind_total:
            wenn feind_aktiv[fi]:
                setze feind_x[fi] auf feind_x[fi] + feind_dir * feind_speed * 10
                wenn feind_x[fi] > BREITE - 40 oder feind_x[fi] < 10:
                    setze muss_runter auf wahr
            setze fi auf fi + 1

        wenn muss_runter:
            setze feind_dir auf 0 - feind_dir
            setze fi auf 0
            solange fi < feind_total:
                wenn feind_aktiv[fi]:
                    setze feind_y[fi] auf feind_y[fi] + 15
                    # Game Over wenn Feinde unten ankommen
                    wenn feind_y[fi] > 620:
                        setze leben auf 0
                        stopp
                setze fi auf fi + 1

    # Feind-Schuesse
    setze feind_schuss_timer auf feind_schuss_timer + 1
    wenn feind_schuss_timer > 40:
        setze feind_schuss_timer auf 0
        # Zufaelliger aktiver Feind schiesst
        setze fi auf (punkte * 7 + feind_timer * 13) % feind_total
        setze versuche auf 0
        solange versuche < feind_total:
            wenn feind_aktiv[fi]:
                setze fsci auf 0
                solange fsci < MAX_FEIND_SCHUESSE:
                    wenn nicht fsch_aktiv[fsci]:
                        setze fsch_x[fsci] auf feind_x[fi] + 15.0
                        setze fsch_y[fsci] auf feind_y[fi] + 20.0
                        setze fsch_aktiv[fsci] auf wahr
                        setze fsci auf MAX_FEIND_SCHUESSE
                        setze versuche auf feind_total
                    setze fsci auf fsci + 1
            setze fi auf (fi + 1) % feind_total
            setze versuche auf versuche + 1

    # === SCHUESSE BEWEGEN ===
    setze sci auf 0
    solange sci < MAX_SCHUESSE:
        wenn sch_aktiv[sci]:
            setze sch_y[sci] auf sch_y[sci] - 8
            wenn sch_y[sci] < -10:
                setze sch_aktiv[sci] auf falsch

            # Treffer?
            setze fi auf 0
            solange fi < feind_total:
                wenn feind_aktiv[fi]:
                    wenn sch_x[sci] > feind_x[fi] und sch_x[sci] < feind_x[fi] + 30:
                        wenn sch_y[sci] > feind_y[fi] und sch_y[sci] < feind_y[fi] + 25:
                            setze feind_aktiv[fi] auf falsch
                            setze sch_aktiv[sci] auf falsch
                            setze punkte auf punkte + (FEIND_REIHEN - feind_typ[fi]) * 10
                setze fi auf fi + 1
        setze sci auf sci + 1

    # Feind-Schuesse bewegen
    setze fsci auf 0
    solange fsci < MAX_FEIND_SCHUESSE:
        wenn fsch_aktiv[fsci]:
            setze fsch_y[fsci] auf fsch_y[fsci] + 4
            wenn fsch_y[fsci] > HOEHE:
                setze fsch_aktiv[fsci] auf falsch

            # Spieler getroffen?
            wenn fsch_x[fsci] > spieler_x und fsch_x[fsci] < spieler_x + SPIELER_W:
                wenn fsch_y[fsci] > 645 und fsch_y[fsci] < 670:
                    setze fsch_aktiv[fsci] auf falsch
                    setze leben auf leben - 1
                    wenn leben <= 0:
                        stopp
        setze fsci auf fsci + 1

    # Alle Feinde besiegt?
    setze alle_tot auf wahr
    setze fi auf 0
    solange fi < feind_total:
        wenn feind_aktiv[fi]:
            setze alle_tot auf falsch
        setze fi auf fi + 1
    wenn alle_tot:
        stopp

    # === ZEICHNEN ===
    fenster_löschen(win, "#000011")

    # Feinde
    setze fi auf 0
    solange fi < feind_total:
        wenn feind_aktiv[fi]:
            setze ff auf feind_farbe(feind_typ[fi])
            zeichne_rechteck(win, feind_x[fi], feind_y[fi], 30, 22, ff)
            # Augen
            zeichne_rechteck(win, feind_x[fi] + 6, feind_y[fi] + 6, 5, 5, "#FFFFFF")
            zeichne_rechteck(win, feind_x[fi] + 19, feind_y[fi] + 6, 5, 5, "#FFFFFF")
            # Beine
            zeichne_rechteck(win, feind_x[fi] + 3, feind_y[fi] + 22, 4, 5, ff)
            zeichne_rechteck(win, feind_x[fi] + 12, feind_y[fi] + 22, 4, 5, ff)
            zeichne_rechteck(win, feind_x[fi] + 23, feind_y[fi] + 22, 4, 5, ff)
        setze fi auf fi + 1

    # Spieler-Schuesse
    setze sci auf 0
    solange sci < MAX_SCHUESSE:
        wenn sch_aktiv[sci]:
            zeichne_rechteck(win, sch_x[sci] - 1, sch_y[sci], 3, 10, "#00FF00")
        setze sci auf sci + 1

    # Feind-Schuesse
    setze fsci auf 0
    solange fsci < MAX_FEIND_SCHUESSE:
        wenn fsch_aktiv[fsci]:
            zeichne_rechteck(win, fsch_x[fsci] - 1, fsch_y[fsci], 3, 10, "#FF5722")
        setze fsci auf fsci + 1

    # Spieler
    zeichne_rechteck(win, spieler_x, 650, SPIELER_W, SPIELER_H, "#4CAF50")
    zeichne_rechteck(win, spieler_x + SPIELER_W / 2 - 2, 645, 4, 8, "#81C784")

    # Bunker (4 Stueck)
    zeichne_rechteck(win, 70, 600, 50, 30, "#388E3C")
    zeichne_rechteck(win, 210, 600, 50, 30, "#388E3C")
    zeichne_rechteck(win, 340, 600, 50, 30, "#388E3C")
    zeichne_rechteck(win, 480, 600, 50, 30, "#388E3C")

    # HUD
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, 20 + li * 25, 690, 6, "#4CAF50")
        setze li auf li + 1

    setze si auf 0
    solange si < punkte / 10 und si < 50:
        zeichne_pixel(win, BREITE - 20 - si * 4, 688, "#FFD700")
        zeichne_pixel(win, BREITE - 20 - si * 4, 689, "#FFD700")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn alle_tot:
    zeige "GEWONNEN! Punkte: " + text(punkte)
sonst:
    zeige "Game Over! Punkte: " + text(punkte)

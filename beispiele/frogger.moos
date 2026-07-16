# ============================================================
# moo Frogger — Klassisches Strassenüberquerungs-Spiel
#
# Kompilieren: moo-compiler compile beispiele/frogger.moo -o beispiele/frogger
# Starten:     ./beispiele/frogger
#
# Hoch/W = Vorwaerts springen
# Links/A, Rechts/D = Seitlich bewegen
# Escape = Beenden
# ============================================================

setze BREITE auf 600
setze HOEHE auf 700
setze REIHEN auf 12
setze ZELLE auf 50
setze FROSCH_SIZE auf 30

# Frosch
setze frosch_x auf 300.0
setze frosch_y auf 650.0
setze frosch_cooldown auf 0
setze leben auf 3
setze punkte auf 0
setze beste_y auf 650.0

# Autos (3 Reihen Strasse)
setze auto_x auf []
setze auto_y auf []
setze auto_speed auf []
setze auto_breite auf []
setze auto_farbe_idx auf []

funktion auto_spawnen(ax, ay, aspeed, aw, farbe):
    auto_x.hinzufügen(ax * 1.0)
    auto_y.hinzufügen(ay * 1.0)
    auto_speed.hinzufügen(aspeed * 1.0)
    auto_breite.hinzufügen(aw)
    auto_farbe_idx.hinzufügen(farbe)

# Reihe 1 (langsam, nach rechts)
auto_spawnen(0, 550, 2, 60, 0)
auto_spawnen(200, 550, 2, 60, 0)
auto_spawnen(450, 550, 2, 60, 0)

# Reihe 2 (mittel, nach links)
auto_spawnen(100, 500, -3, 80, 1)
auto_spawnen(350, 500, -3, 80, 1)

# Reihe 3 (schnell, nach rechts)
auto_spawnen(50, 450, 4, 50, 2)
auto_spawnen(250, 450, 4, 50, 2)
auto_spawnen(500, 450, 4, 50, 2)

# Reihe 4 (LKW, nach links)
auto_spawnen(0, 400, -2, 100, 3)
auto_spawnen(300, 400, -2, 100, 3)

# Reihe 5 (schnell, nach rechts)
auto_spawnen(100, 350, 5, 40, 4)
auto_spawnen(400, 350, 5, 40, 4)

setze auto_anzahl auf länge(auto_x)

# Baumstaemme (Fluss, 3 Reihen)
setze log_x auf []
setze log_y auf []
setze log_speed auf []
setze log_breite auf []

funktion log_spawnen(lx, ly, lspeed, lw):
    log_x.hinzufügen(lx * 1.0)
    log_y.hinzufügen(ly * 1.0)
    log_speed.hinzufügen(lspeed * 1.0)
    log_breite.hinzufügen(lw)

# Fluss Reihe 1
log_spawnen(0, 250, 1.5, 90)
log_spawnen(250, 250, 1.5, 90)
log_spawnen(500, 250, 1.5, 90)

# Fluss Reihe 2
log_spawnen(50, 200, -2, 70)
log_spawnen(300, 200, -2, 70)

# Fluss Reihe 3
log_spawnen(100, 150, 1, 120)
log_spawnen(400, 150, 1, 120)

setze log_anzahl auf länge(log_x)

funktion auto_farbe(idx):
    wenn idx == 0:
        gib_zurück "#F44336"
    wenn idx == 1:
        gib_zurück "#2196F3"
    wenn idx == 2:
        gib_zurück "#FF9800"
    wenn idx == 3:
        gib_zurück "#795548"
    gib_zurück "#9C27B0"

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Frogger", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # === INPUT ===
    wenn frosch_cooldown <= 0:
        wenn taste_gedrückt("oben") oder taste_gedrückt("w"):
            setze frosch_y auf frosch_y - ZELLE
            setze frosch_cooldown auf 10
        wenn taste_gedrückt("links") oder taste_gedrückt("a"):
            setze frosch_x auf frosch_x - ZELLE
            setze frosch_cooldown auf 8
        wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
            setze frosch_x auf frosch_x + ZELLE
            setze frosch_cooldown auf 8

    wenn frosch_cooldown > 0:
        setze frosch_cooldown auf frosch_cooldown - 1

    # Grenzen
    wenn frosch_x < 15:
        setze frosch_x auf 15.0
    wenn frosch_x > BREITE - 15:
        setze frosch_x auf (BREITE - 15) * 1.0

    # Punkte fuer Fortschritt
    wenn frosch_y < beste_y:
        setze punkte auf punkte + 10
        setze beste_y auf frosch_y

    # Ziel erreicht
    wenn frosch_y < 100:
        setze punkte auf punkte + 100
        setze frosch_x auf 300.0
        setze frosch_y auf 650.0
        setze beste_y auf 650.0

    # === AUTOS BEWEGEN ===
    setze ai auf 0
    solange ai < auto_anzahl:
        setze auto_x[ai] auf auto_x[ai] + auto_speed[ai]
        wenn auto_x[ai] > BREITE + 20:
            setze auto_x[ai] auf -auto_breite[ai] * 1.0
        wenn auto_x[ai] < -auto_breite[ai]:
            setze auto_x[ai] auf (BREITE + 20) * 1.0

        # Kollision mit Frosch
        wenn frosch_y > auto_y[ai] - 20 und frosch_y < auto_y[ai] + 20:
            wenn frosch_x > auto_x[ai] - 15 und frosch_x < auto_x[ai] + auto_breite[ai] + 15:
                setze leben auf leben - 1
                setze frosch_x auf 300.0
                setze frosch_y auf 650.0
                setze beste_y auf 650.0
                wenn leben <= 0:
                    stopp
        setze ai auf ai + 1

    # === LOGS BEWEGEN ===
    setze auf_log auf falsch
    setze li auf 0
    solange li < log_anzahl:
        setze log_x[li] auf log_x[li] + log_speed[li]
        wenn log_x[li] > BREITE + 20:
            setze log_x[li] auf -log_breite[li] * 1.0
        wenn log_x[li] < -log_breite[li]:
            setze log_x[li] auf (BREITE + 20) * 1.0

        # Frosch auf Log?
        wenn frosch_y > log_y[li] - 20 und frosch_y < log_y[li] + 20:
            wenn frosch_x > log_x[li] - 15 und frosch_x < log_x[li] + log_breite[li] + 15:
                setze frosch_x auf frosch_x + log_speed[li]
                setze auf_log auf wahr
        setze li auf li + 1

    # Im Fluss ohne Log = Tod
    wenn frosch_y >= 130 und frosch_y <= 270:
        wenn nicht auf_log:
            setze leben auf leben - 1
            setze frosch_x auf 300.0
            setze frosch_y auf 650.0
            setze beste_y auf 650.0
            wenn leben <= 0:
                stopp

    # === ZEICHNEN ===
    fenster_löschen(win, "#4CAF50")

    # Zielzone (oben)
    zeichne_rechteck(win, 0, 50, BREITE, 50, "#1B5E20")
    # Markierungen
    setze zi auf 0
    solange zi < 5:
        zeichne_rechteck(win, 20 + zi * 120, 60, 40, 30, "#2E7D32")
        setze zi auf zi + 1

    # Fluss
    zeichne_rechteck(win, 0, 130, BREITE, 150, "#1565C0")

    # Sicherheitsstreifen
    zeichne_rechteck(win, 0, 280, BREITE, 50, "#4CAF50")

    # Strasse
    zeichne_rechteck(win, 0, 330, BREITE, 270, "#424242")
    # Fahrbahnmarkierungen
    setze mi auf 0
    solange mi < 15:
        zeichne_rechteck(win, mi * 45, 375, 25, 3, "#FFEB3B")
        zeichne_rechteck(win, mi * 45, 425, 25, 3, "#FFEB3B")
        zeichne_rechteck(win, mi * 45, 475, 25, 3, "#FFEB3B")
        zeichne_rechteck(win, mi * 45, 525, 25, 3, "#FFEB3B")
        setze mi auf mi + 1

    # Gehweg (unten)
    zeichne_rechteck(win, 0, 600, BREITE, 100, "#8D6E63")

    # Logs
    setze li auf 0
    solange li < log_anzahl:
        zeichne_rechteck(win, log_x[li], log_y[li] - 10, log_breite[li], 20, "#5D4037")
        zeichne_rechteck(win, log_x[li] + 5, log_y[li] - 6, log_breite[li] - 10, 12, "#795548")
        setze li auf li + 1

    # Autos
    setze ai auf 0
    solange ai < auto_anzahl:
        setze af auf auto_farbe(auto_farbe_idx[ai])
        zeichne_rechteck(win, auto_x[ai], auto_y[ai] - 12, auto_breite[ai], 24, af)
        # Fenster
        wenn auto_speed[ai] > 0:
            zeichne_rechteck(win, auto_x[ai] + auto_breite[ai] - 15, auto_y[ai] - 8, 10, 16, "#90CAF9")
        sonst:
            zeichne_rechteck(win, auto_x[ai] + 5, auto_y[ai] - 8, 10, 16, "#90CAF9")
        setze ai auf ai + 1

    # Frosch
    zeichne_kreis(win, frosch_x, frosch_y, 14, "#4CAF50")
    zeichne_kreis(win, frosch_x, frosch_y, 10, "#66BB6A")
    # Augen
    zeichne_kreis(win, frosch_x - 5, frosch_y - 6, 3, "#FFFFFF")
    zeichne_kreis(win, frosch_x + 5, frosch_y - 6, 3, "#FFFFFF")
    zeichne_kreis(win, frosch_x - 5, frosch_y - 6, 1, "#000000")
    zeichne_kreis(win, frosch_x + 5, frosch_y - 6, 1, "#000000")

    # HUD
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, 20 + li * 25, 25, 8, "#F44336")
        setze li auf li + 1

    setze si auf 0
    solange si < punkte / 10 und si < 40:
        zeichne_kreis(win, BREITE - 20 - si * 10, 25, 3, "#FFD700")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Game Over! Punkte: " + text(punkte)

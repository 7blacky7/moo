# ============================================================
# moo Asteroids — Klassisches Weltraum-Spiel
#
# Kompilieren: moo-compiler compile beispiele/asteroids.moo -o beispiele/asteroids
# Starten:     ./beispiele/asteroids
#
# Links/Rechts oder A/D = Drehen
# Hoch/W = Schub
# Leertaste = Schiessen
# Escape = Beenden
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600
setze MAX_SCHUSS auf 10
setze MAX_ASTER auf 20
setze SCHUSS_SPEED auf 8.0

# Schiff
setze schiff_x auf 400.0
setze schiff_y auf 300.0
setze schiff_vx auf 0.0
setze schiff_vy auf 0.0
setze schiff_winkel auf 0.0
setze schiff_schub auf falsch
setze leben auf 3
setze unverw auf 0
setze punkte auf 0

# Schuesse
setze sch_x auf []
setze sch_y auf []
setze sch_vx auf []
setze sch_vy auf []
setze sch_aktiv auf []
setze sci auf 0
solange sci < MAX_SCHUSS:
    sch_x.hinzufügen(0.0)
    sch_y.hinzufügen(0.0)
    sch_vx.hinzufügen(0.0)
    sch_vy.hinzufügen(0.0)
    sch_aktiv.hinzufügen(falsch)
    setze sci auf sci + 1

# Asteroiden
setze ast_x auf []
setze ast_y auf []
setze ast_vx auf []
setze ast_vy auf []
setze ast_r auf []
setze ast_aktiv auf []
setze ai auf 0
solange ai < MAX_ASTER:
    ast_x.hinzufügen(0.0)
    ast_y.hinzufügen(0.0)
    ast_vx.hinzufügen(0.0)
    ast_vy.hinzufügen(0.0)
    ast_r.hinzufügen(0)
    ast_aktiv.hinzufügen(falsch)
    setze ai auf ai + 1

# Start-Asteroiden
funktion spawn_asteroid(ax, ay, avx, avy, ar):
    setze ai auf 0
    solange ai < MAX_ASTER:
        wenn nicht ast_aktiv[ai]:
            setze ast_x[ai] auf ax
            setze ast_y[ai] auf ay
            setze ast_vx[ai] auf avx
            setze ast_vy[ai] auf avy
            setze ast_r[ai] auf ar
            setze ast_aktiv[ai] auf wahr
            gib_zurück ai
        setze ai auf ai + 1
    gib_zurück -1

spawn_asteroid(100.0, 100.0, 1.5, 0.8, 40)
spawn_asteroid(600.0, 100.0, -1.0, 1.2, 35)
spawn_asteroid(200.0, 500.0, 0.8, -1.0, 45)
spawn_asteroid(700.0, 400.0, -1.5, -0.5, 30)

setze schuss_cooldown auf 0

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Asteroids", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # === INPUT ===
    wenn taste_gedrückt("links") oder taste_gedrückt("a"):
        setze schiff_winkel auf schiff_winkel - 0.06
    wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
        setze schiff_winkel auf schiff_winkel + 0.06

    setze schiff_schub auf falsch
    wenn taste_gedrückt("oben") oder taste_gedrückt("w"):
        setze schiff_vx auf schiff_vx + sinus(schiff_winkel) * 0.15
        setze schiff_vy auf schiff_vy - cosinus(schiff_winkel) * 0.15
        setze schiff_schub auf wahr

    # Schiessen
    wenn taste_gedrückt("leertaste") und schuss_cooldown <= 0:
        setze sci auf 0
        solange sci < MAX_SCHUSS:
            wenn nicht sch_aktiv[sci]:
                setze sch_x[sci] auf schiff_x
                setze sch_y[sci] auf schiff_y
                setze sch_vx[sci] auf sinus(schiff_winkel) * SCHUSS_SPEED
                setze sch_vy[sci] auf 0.0 - cosinus(schiff_winkel) * SCHUSS_SPEED
                setze sch_aktiv[sci] auf wahr
                setze schuss_cooldown auf 10
                setze sci auf MAX_SCHUSS
            setze sci auf sci + 1

    wenn schuss_cooldown > 0:
        setze schuss_cooldown auf schuss_cooldown - 1

    # === PHYSIK ===
    # Schiff bewegen
    setze schiff_x auf schiff_x + schiff_vx
    setze schiff_y auf schiff_y + schiff_vy
    # Reibung
    setze schiff_vx auf schiff_vx * 0.995
    setze schiff_vy auf schiff_vy * 0.995

    # Wrap-around
    wenn schiff_x < 0:
        setze schiff_x auf BREITE * 1.0
    wenn schiff_x > BREITE:
        setze schiff_x auf 0.0
    wenn schiff_y < 0:
        setze schiff_y auf HOEHE * 1.0
    wenn schiff_y > HOEHE:
        setze schiff_y auf 0.0

    # Schuesse bewegen
    setze sci auf 0
    solange sci < MAX_SCHUSS:
        wenn sch_aktiv[sci]:
            setze sch_x[sci] auf sch_x[sci] + sch_vx[sci]
            setze sch_y[sci] auf sch_y[sci] + sch_vy[sci]
            wenn sch_x[sci] < -10 oder sch_x[sci] > BREITE + 10:
                setze sch_aktiv[sci] auf falsch
            wenn sch_y[sci] < -10 oder sch_y[sci] > HOEHE + 10:
                setze sch_aktiv[sci] auf falsch
        setze sci auf sci + 1

    # Asteroiden bewegen
    setze ai auf 0
    solange ai < MAX_ASTER:
        wenn ast_aktiv[ai]:
            setze ast_x[ai] auf ast_x[ai] + ast_vx[ai]
            setze ast_y[ai] auf ast_y[ai] + ast_vy[ai]
            # Wrap
            wenn ast_x[ai] < -50:
                setze ast_x[ai] auf (BREITE + 50) * 1.0
            wenn ast_x[ai] > BREITE + 50:
                setze ast_x[ai] auf -50.0
            wenn ast_y[ai] < -50:
                setze ast_y[ai] auf (HOEHE + 50) * 1.0
            wenn ast_y[ai] > HOEHE + 50:
                setze ast_y[ai] auf -50.0
        setze ai auf ai + 1

    # Schuss-Asteroid Kollision
    setze sci auf 0
    solange sci < MAX_SCHUSS:
        wenn sch_aktiv[sci]:
            setze ai auf 0
            solange ai < MAX_ASTER:
                wenn ast_aktiv[ai]:
                    setze ddx auf sch_x[sci] - ast_x[ai]
                    setze ddy auf sch_y[sci] - ast_y[ai]
                    setze dist auf ddx * ddx + ddy * ddy
                    setze ar auf ast_r[ai]
                    wenn dist < ar * ar:
                        setze sch_aktiv[sci] auf falsch
                        setze ast_aktiv[ai] auf falsch
                        setze punkte auf punkte + (50 - ar)
                        # Split: grosse Asteroiden → 2 kleine
                        wenn ar > 20:
                            setze nr auf ar / 2
                            spawn_asteroid(ast_x[ai], ast_y[ai], ast_vx[ai] + 1.0, ast_vy[ai] - 1.0, nr)
                            spawn_asteroid(ast_x[ai], ast_y[ai], ast_vx[ai] - 1.0, ast_vy[ai] + 1.0, nr)
                setze ai auf ai + 1
        setze sci auf sci + 1

    # Schiff-Asteroid Kollision
    wenn unverw <= 0:
        setze ai auf 0
        solange ai < MAX_ASTER:
            wenn ast_aktiv[ai]:
                setze ddx auf schiff_x - ast_x[ai]
                setze ddy auf schiff_y - ast_y[ai]
                setze dist auf ddx * ddx + ddy * ddy
                setze ar auf ast_r[ai] + 12
                wenn dist < ar * ar:
                    setze leben auf leben - 1
                    setze unverw auf 90
                    setze schiff_x auf 400.0
                    setze schiff_y auf 300.0
                    setze schiff_vx auf 0.0
                    setze schiff_vy auf 0.0
                    wenn leben <= 0:
                        stopp
            setze ai auf ai + 1

    wenn unverw > 0:
        setze unverw auf unverw - 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#000011")

    # Sterne (statisch)
    zeichne_pixel(win, 50, 80, "#FFFFFF")
    zeichne_pixel(win, 150, 200, "#FFFFFF")
    zeichne_pixel(win, 300, 50, "#FFFFFF")
    zeichne_pixel(win, 450, 180, "#FFFFFF")
    zeichne_pixel(win, 600, 90, "#FFFFFF")
    zeichne_pixel(win, 700, 250, "#FFFFFF")
    zeichne_pixel(win, 100, 400, "#FFFFFF")
    zeichne_pixel(win, 350, 350, "#FFFFFF")
    zeichne_pixel(win, 550, 450, "#FFFFFF")
    zeichne_pixel(win, 650, 500, "#FFFFFF")
    zeichne_pixel(win, 200, 550, "#FFFFFF")
    zeichne_pixel(win, 750, 150, "#FFFFFF")

    # Asteroiden
    setze ai auf 0
    solange ai < MAX_ASTER:
        wenn ast_aktiv[ai]:
            setze ar auf ast_r[ai]
            zeichne_kreis(win, ast_x[ai], ast_y[ai], ar, "#8D6E63")
            zeichne_kreis(win, ast_x[ai] - ar / 4, ast_y[ai] - ar / 4, ar / 3, "#795548")
        setze ai auf ai + 1

    # Schuesse
    setze sci auf 0
    solange sci < MAX_SCHUSS:
        wenn sch_aktiv[sci]:
            zeichne_kreis(win, sch_x[sci], sch_y[sci], 2, "#FF5722")
        setze sci auf sci + 1

    # Schiff
    wenn unverw <= 0 oder (unverw / 4) % 2 == 0:
        setze sx auf schiff_x
        setze sy auf schiff_y
        setze sw auf schiff_winkel
        # Dreieck aus 3 Linien
        setze nx auf sinus(sw) * 15
        setze ny auf 0.0 - cosinus(sw) * 15
        setze lx auf sinus(sw - 2.5) * 12
        setze ly auf 0.0 - cosinus(sw - 2.5) * 12
        setze rrx auf sinus(sw + 2.5) * 12
        setze rry auf 0.0 - cosinus(sw + 2.5) * 12
        zeichne_linie(win, sx + nx, sy + ny, sx + lx, sy + ly, "#00BCD4")
        zeichne_linie(win, sx + nx, sy + ny, sx + rrx, sy + rry, "#00BCD4")
        zeichne_linie(win, sx + lx, sy + ly, sx + rrx, sy + rry, "#00838F")
        # Schub-Flamme
        wenn schiff_schub:
            setze fx auf 0.0 - sinus(sw) * 18
            setze fy auf cosinus(sw) * 18
            zeichne_linie(win, sx + lx, sy + ly, sx + fx, sy + fy, "#FF9800")
            zeichne_linie(win, sx + rrx, sy + rry, sx + fx, sy + fy, "#FF9800")

    # HUD
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, 20 + li * 25, 20, 6, "#00BCD4")
        setze li auf li + 1

    setze si auf 0
    solange si < punkte / 10 und si < 50:
        zeichne_pixel(win, 20 + si * 4, 40, "#FFD700")
        zeichne_pixel(win, 20 + si * 4, 41, "#FFD700")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Game Over! Punkte: " + text(punkte)

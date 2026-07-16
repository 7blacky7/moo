# ============================================================
# doodle_jump.moo — Vertikaler Endlos-Platformer
#
# Kompilieren: moo-compiler compile beispiele/doodle_jump.moo -o beispiele/doodle_jump
# Starten:     ./beispiele/doodle_jump
#
# Steuerung: Links/Rechts oder A/D = Bewegen, Escape = Beenden
# Ziel: So hoch wie moeglich springen!
# ============================================================

konstante WIN_W auf 320
konstante WIN_H auf 480
konstante SCHWERKRAFT auf 0.35
konstante SPRUNG auf -10.0
konstante MAX_PLATTFORMEN auf 20
konstante PLATT_W auf 60
konstante PLATT_H auf 12

# === PRNG ===
setze dj_seed auf 54321

funktion dj_rand(max_val):
    setze dj_seed auf (dj_seed * 1103515245 + 12345) % 2147483648
    gib_zurück dj_seed % max_val

# === Spieler ===
setze px auf 160.0
setze py auf 400.0
setze vy auf SPRUNG
setze richtung auf 1

# === Plattformen ===
setze pl_x auf []
setze pl_y auf []
setze pl_typ auf []
setze pl_count auf 0

# Typ: 0=normal(grün), 1=bewegend(blau), 2=zerbrechlich(braun), 3=feder(rot)
funktion init_plattformen():
    setze pl_x auf []
    setze pl_y auf []
    setze pl_typ auf []
    setze pl_count auf 0
    setze pi auf 0
    solange pi < MAX_PLATTFORMEN:
        pl_x.hinzufügen(dj_rand(WIN_W - PLATT_W))
        pl_y.hinzufügen(WIN_H - pi * 30)
        setze typ auf 0
        setze rnd auf dj_rand(100)
        wenn rnd > 85:
            setze typ auf 1
        sonst wenn rnd > 70:
            setze typ auf 2
        sonst wenn rnd > 60:
            setze typ auf 3
        pl_typ.hinzufügen(typ)
        setze pl_count auf pl_count + 1
        setze pi auf pi + 1

# === Kamera ===
setze kamera_y auf 0.0
setze score auf 0
setze highscore auf 0
setze game_over auf falsch

# === Plattform-Farben ===
funktion platt_farbe(typ):
    wenn typ == 0: gib_zurück "#4CAF50"
    wenn typ == 1: gib_zurück "#2196F3"
    wenn typ == 2: gib_zurück "#8D6E63"
    wenn typ == 3: gib_zurück "#F44336"
    gib_zurück "#4CAF50"

funktion platt_farbe2(typ):
    wenn typ == 0: gib_zurück "#66BB6A"
    wenn typ == 1: gib_zurück "#42A5F5"
    wenn typ == 2: gib_zurück "#A1887F"
    wenn typ == 3: gib_zurück "#EF5350"
    gib_zurück "#66BB6A"

# === Neues Spiel ===
funktion neues_spiel():
    setze px auf 160.0
    setze py auf 400.0
    setze vy auf SPRUNG
    setze kamera_y auf 0.0
    setze score auf 0
    setze game_over auf falsch
    setze dj_seed auf zeit_ms() % 99991
    init_plattformen()

# === Hauptprogramm ===
zeige "=== moo Doodle Jump ==="
zeige "Links/Rechts = Bewegen, Spring so hoch wie moeglich!"

setze win auf fenster_erstelle("moo Doodle Jump", WIN_W, WIN_H)
setze dj_seed auf zeit_ms() % 99991
init_plattformen()

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn game_over:
        wenn taste_gedrückt("leertaste"):
            neues_spiel()
    sonst:
        # Bewegung
        wenn taste_gedrückt("links") oder taste_gedrückt("a"):
            setze px auf px - 5
            setze richtung auf -1
        wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
            setze px auf px + 5
            setze richtung auf 1

        # Wrap-Around
        wenn px < -10:
            setze px auf WIN_W + 10
        wenn px > WIN_W + 10:
            setze px auf -10

        # Schwerkraft
        setze vy auf vy + SCHWERKRAFT
        setze py auf py + vy

        # Plattform-Kollision (nur beim Fallen)
        wenn vy > 0:
            setze pi auf 0
            solange pi < pl_count:
                setze plat_x auf pl_x[pi]
                setze plat_y auf pl_y[pi]
                setze screen_y auf plat_y - kamera_y
                # Spieler-Fuesse auf Plattform?
                wenn py + 15 >= plat_y und py + 15 <= plat_y + PLATT_H + vy:
                    wenn px + 10 > plat_x und px - 10 < plat_x + PLATT_W:
                        wenn pl_typ[pi] == 2:
                            # Zerbrechlich — einmal springen dann weg
                            setze vy auf SPRUNG
                            pl_typ[pi] = -1
                        sonst wenn pl_typ[pi] == 3:
                            # Feder — extra hoch
                            setze vy auf SPRUNG * 1.5
                        sonst wenn pl_typ[pi] >= 0:
                            setze vy auf SPRUNG
                setze pi auf pi + 1

        # Kamera folgt Spieler (nur nach oben)
        wenn py < kamera_y + WIN_H / 3:
            setze kamera_y auf py - WIN_H / 3

        # Score = Höhe
        setze hoehe auf boden(-kamera_y / 10)
        wenn hoehe > score:
            setze score auf hoehe

        # Plattformen recyclen (unter Bildschirm → oben neu)
        setze pi auf 0
        solange pi < pl_count:
            wenn pl_y[pi] - kamera_y > WIN_H + 50:
                # Oben neu platzieren
                pl_y[pi] = kamera_y - 30 - dj_rand(60)
                pl_x[pi] = dj_rand(WIN_W - PLATT_W)
                setze rnd auf dj_rand(100)
                wenn rnd > 85:
                    pl_typ[pi] = 1
                sonst wenn rnd > 70:
                    pl_typ[pi] = 2
                sonst wenn rnd > 60:
                    pl_typ[pi] = 3
                sonst:
                    pl_typ[pi] = 0
            setze pi auf pi + 1

        # Bewegende Plattformen
        setze pi auf 0
        solange pi < pl_count:
            wenn pl_typ[pi] == 1:
                pl_x[pi] = pl_x[pi] + sinus(pl_y[pi] * 0.02) * 2
                wenn pl_x[pi] < 0: pl_x[pi] = 0
                wenn pl_x[pi] > WIN_W - PLATT_W: pl_x[pi] = WIN_W - PLATT_W
            setze pi auf pi + 1

        # Game Over (zu weit gefallen)
        wenn py - kamera_y > WIN_H + 50:
            setze game_over auf wahr
            wenn score > highscore:
                setze highscore auf score

    # === Zeichnen ===
    # Hintergrund-Gradient (hell oben, dunkel unten)
    zeichne_rechteck(win, 0, 0, WIN_W, WIN_H / 2, "#E8F5E9")
    zeichne_rechteck(win, 0, WIN_H / 2, WIN_W, WIN_H / 2, "#C8E6C9")

    # Plattformen
    setze pi auf 0
    solange pi < pl_count:
        wenn pl_typ[pi] >= 0:
            setze draw_x auf pl_x[pi]
            setze draw_y auf pl_y[pi] - kamera_y
            wenn draw_y > -20 und draw_y < WIN_H + 20:
                setze farbe auf platt_farbe(pl_typ[pi])
                setze farbe2 auf platt_farbe2(pl_typ[pi])
                zeichne_rechteck(win, draw_x, draw_y, PLATT_W, PLATT_H, farbe)
                zeichne_rechteck(win, draw_x + 2, draw_y + 2, PLATT_W - 4, PLATT_H - 4, farbe2)
                # Feder-Symbol
                wenn pl_typ[pi] == 3:
                    zeichne_rechteck(win, draw_x + PLATT_W / 2 - 3, draw_y - 8, 6, 8, "#FF5722")
                    zeichne_kreis(win, draw_x + PLATT_W / 2, draw_y - 10, 5, "#FF8A65")
                # Risse bei zerbrechlich
                wenn pl_typ[pi] == 2:
                    zeichne_linie(win, draw_x + 10, draw_y + 3, draw_x + 25, draw_y + 9, "#5D4037")
                    zeichne_linie(win, draw_x + 35, draw_y + 2, draw_x + 45, draw_y + 8, "#5D4037")
        setze pi auf pi + 1

    # Spieler
    wenn game_over == falsch:
        setze draw_px auf px
        setze draw_py auf py - kamera_y
        # Koerper
        zeichne_kreis(win, draw_px, draw_py, 14, "#FFD700")
        zeichne_kreis(win, draw_px, draw_py - 2, 12, "#FFEB3B")
        # Augen
        zeichne_kreis(win, draw_px - 5 * richtung, draw_py - 6, 5, "#FFFFFF")
        zeichne_kreis(win, draw_px + 3 * richtung, draw_py - 6, 5, "#FFFFFF")
        zeichne_kreis(win, draw_px - 4 * richtung, draw_py - 6, 2, "#000000")
        zeichne_kreis(win, draw_px + 4 * richtung, draw_py - 6, 2, "#000000")
        # Mund
        zeichne_rechteck(win, draw_px - 4, draw_py + 2, 8, 3, "#E65100")
        # Fuesse
        zeichne_rechteck(win, draw_px - 8, draw_py + 12, 6, 4, "#8D6E63")
        zeichne_rechteck(win, draw_px + 2, draw_py + 12, 6, 4, "#8D6E63")
        # Nase/Schnauze
        zeichne_kreis(win, draw_px + 8 * richtung, draw_py - 2, 4, "#4CAF50")

    # HUD
    zeichne_rechteck(win, 0, 0, WIN_W, 28, "#1A1A2E")
    # Score
    setze si auf 0
    solange si < score und si < 40:
        zeichne_kreis(win, 12 + si * 6, 14, 2, "#FFD700")
        setze si auf si + 1
    # Highscore
    setze hi auf 0
    solange hi < highscore und hi < 20:
        zeichne_kreis(win, WIN_W - 12 - hi * 6, 14, 2, "#90A4AE")
        setze hi auf hi + 1

    # Game Over
    wenn game_over:
        zeichne_rechteck(win, WIN_W / 2 - 80, WIN_H / 2 - 30, 160, 60, "#D32F2F")
        zeichne_rechteck(win, WIN_W / 2 - 78, WIN_H / 2 - 28, 156, 56, "#F44336")
        # Trauriges Gesicht
        zeichne_kreis(win, WIN_W / 2, WIN_H / 2, 16, "#FFD700")
        zeichne_kreis(win, WIN_W / 2 - 5, WIN_H / 2 - 5, 2, "#000000")
        zeichne_kreis(win, WIN_W / 2 + 5, WIN_H / 2 - 5, 2, "#000000")
        zeichne_linie(win, WIN_W / 2 - 6, WIN_H / 2 + 6, WIN_W / 2 + 6, WIN_H / 2 + 6, "#000000")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Doodle Jump beendet. Score: " + text(score) + " Highscore: " + text(highscore)

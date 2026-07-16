# ============================================================
# moo Ski — SkiFree-Style Abfahrt
#
# Kompilieren: moo-compiler compile beispiele/ski.moo -o beispiele/ski
# Starten:     ./beispiele/ski
#
# Links/Rechts oder A/D = Lenken
# Escape = Beenden
# ============================================================

setze BREITE auf 500
setze HOEHE auf 600
setze MAX_OBJ auf 40
setze SPEED auf 3.0

# Skifahrer
setze ski_x auf 250.0
setze punkte auf 0
setze leben auf 3
setze frame auf 0

# Objekte (Baeume, Steine, Tore, Rampen)
# Typ: 0=Baum, 1=Stein, 2=Tor-Links, 3=Tor-Rechts, 4=Rampe
setze obj_x auf []
setze obj_y auf []
setze obj_typ auf []

setze seed auf 55

setze oi auf 0
solange oi < MAX_OBJ:
    setze seed auf (seed * 1103515245 + 12345) % 2147483648
    obj_x.hinzufügen((50 + seed % 400) * 1.0)
    obj_y.hinzufügen((HOEHE + oi * 80) * 1.0)
    setze seed auf (seed * 1103515245 + 12345) % 2147483648
    setze typ auf seed % 10
    wenn typ < 4:
        obj_typ.hinzufügen(0)
    sonst:
        wenn typ < 6:
            obj_typ.hinzufügen(1)
        sonst:
            wenn typ < 8:
                obj_typ.hinzufügen(2)
            sonst:
                obj_typ.hinzufügen(4)
    setze oi auf oi + 1

setze game_over auf falsch
setze geschwindigkeit auf SPEED
setze unverw auf 0

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Ski", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn game_over:
        stopp

    # Lenken
    wenn taste_gedrückt("links") oder taste_gedrückt("a"):
        setze ski_x auf ski_x - 4
    wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
        setze ski_x auf ski_x + 4

    wenn ski_x < 20:
        setze ski_x auf 20.0
    wenn ski_x > BREITE - 20:
        setze ski_x auf (BREITE - 20) * 1.0

    setze frame auf frame + 1
    setze punkte auf punkte + 1

    # Objekte bewegen (nach oben scrollen)
    setze oi auf 0
    solange oi < MAX_OBJ:
        setze obj_y[oi] auf obj_y[oi] - geschwindigkeit

        # Recyclen
        wenn obj_y[oi] < -50:
            setze obj_y[oi] auf (HOEHE + 50) * 1.0
            setze seed auf (seed * 1103515245 + 12345) % 2147483648
            setze obj_x[oi] auf (50 + seed % 400) * 1.0
            setze seed auf (seed * 1103515245 + 12345) % 2147483648
            setze typ auf seed % 10
            wenn typ < 4:
                setze obj_typ[oi] auf 0
            sonst:
                wenn typ < 6:
                    setze obj_typ[oi] auf 1
                sonst:
                    wenn typ < 8:
                        setze obj_typ[oi] auf 2
                    sonst:
                        setze obj_typ[oi] auf 4

        # Kollision (Skifahrer bei y=100)
        wenn unverw <= 0:
            wenn obj_y[oi] > 90 und obj_y[oi] < 120:
                setze ddx auf ski_x - obj_x[oi]
                wenn ddx < 0:
                    setze ddx auf 0 - ddx

                wenn obj_typ[oi] == 0 und ddx < 15:
                    # Baum-Crash
                    setze leben auf leben - 1
                    setze unverw auf 40
                    wenn leben <= 0:
                        setze game_over auf wahr

                wenn obj_typ[oi] == 1 und ddx < 12:
                    # Stein-Crash
                    setze leben auf leben - 1
                    setze unverw auf 40
                    wenn leben <= 0:
                        setze game_over auf wahr

                wenn obj_typ[oi] == 2 und ddx < 25:
                    # Tor durchfahren = Bonus
                    setze punkte auf punkte + 50

                wenn obj_typ[oi] == 4 und ddx < 15:
                    # Rampe = Sprung + Bonus
                    setze punkte auf punkte + 30

        setze oi auf oi + 1

    wenn unverw > 0:
        setze unverw auf unverw - 1

    # Geschwindigkeit erhoehen
    wenn punkte % 500 == 0:
        setze geschwindigkeit auf geschwindigkeit + 0.1

    # === ZEICHNEN ===
    fenster_löschen(win, "#FFFFFF")

    # Schneespuren (dekorativ)
    setze si auf 0
    solange si < 20:
        setze sy auf (frame * 3 + si * 37) % HOEHE
        zeichne_pixel(win, 100 + si * 20, sy, "#E0E0E0")
        zeichne_pixel(win, 101 + si * 20, sy + 1, "#E0E0E0")
        setze si auf si + 1

    # Objekte
    setze oi auf 0
    solange oi < MAX_OBJ:
        setze ox auf obj_x[oi]
        setze oy auf obj_y[oi]

        wenn oy > -30 und oy < HOEHE + 30:
            wenn obj_typ[oi] == 0:
                # Baum
                zeichne_rechteck(win, ox - 3, oy, 6, 15, "#5D4037")
                zeichne_kreis(win, ox, oy - 5, 12, "#2E7D32")
                zeichne_kreis(win, ox, oy - 15, 8, "#388E3C")

            wenn obj_typ[oi] == 1:
                # Stein
                zeichne_kreis(win, ox, oy, 8, "#78909C")
                zeichne_kreis(win, ox - 2, oy - 2, 3, "#90A4AE")

            wenn obj_typ[oi] == 2:
                # Slalom-Tor
                zeichne_rechteck(win, ox - 2, oy - 15, 4, 30, "#F44336")
                zeichne_rechteck(win, ox - 10, oy - 15, 20, 4, "#F44336")

            wenn obj_typ[oi] == 4:
                # Rampe
                zeichne_rechteck(win, ox - 12, oy, 24, 6, "#FF9800")
                zeichne_rechteck(win, ox - 10, oy - 3, 20, 3, "#FFB74D")

        setze oi auf oi + 1

    # Skifahrer
    wenn unverw <= 0 oder (unverw / 3) % 2 == 0:
        # Koerper
        zeichne_kreis(win, ski_x, 95, 6, "#2196F3")
        zeichne_rechteck(win, ski_x - 4, 101, 8, 12, "#1565C0")
        # Kopf
        zeichne_kreis(win, ski_x, 87, 5, "#FFE0B2")
        # Skier
        zeichne_rechteck(win, ski_x - 8, 113, 7, 3, "#37474F")
        zeichne_rechteck(win, ski_x + 1, 113, 7, 3, "#37474F")
        # Stoecke
        zeichne_linie(win, ski_x - 6, 100, ski_x - 14, 112, "#795548")
        zeichne_linie(win, ski_x + 6, 100, ski_x + 14, 112, "#795548")

    # HUD
    setze li auf 0
    solange li < leben:
        zeichne_kreis(win, 20 + li * 20, 20, 6, "#F44336")
        setze li auf li + 1

    setze si auf 0
    solange si < punkte / 100 und si < 30:
        zeichne_kreis(win, BREITE - 20 - si * 10, 20, 3, "#FFD700")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Game Over! Punkte: " + text(punkte)

# ============================================================
# moo Helicopter — Endloser Side-Scroller
#
# Kompilieren: moo-compiler compile beispiele/helicopter.moo -o beispiele/helicopter
# Starten:     ./beispiele/helicopter
#
# Leertaste/Maus = Steigen, Loslassen = Sinken
# Escape = Beenden
# ============================================================

setze BREITE auf 600
setze HOEHE auf 400
setze MAX_HINDER auf 20
setze SCHWERKRAFT auf 0.3
setze AUFTRIEB auf -0.6
setze TUNNEL_MIN auf 120
setze TUNNEL_MAX auf 250

# Helikopter
setze heli_y auf 200.0
setze heli_vy auf 0.0
setze heli_x auf 80

# Tunnel (obere + untere Wand)
setze wand_oben auf []
setze wand_unten auf []
setze scroll auf 0.0
setze speed auf 2.0

# Tunnel generieren
setze seed auf 42
setze tunnel_y auf 150.0
setze ti auf 0
solange ti < 300:
    setze seed auf (seed * 1103515245 + 12345) % 2147483648
    setze tunnel_y auf tunnel_y + (seed % 20 - 10) * 0.3
    wenn tunnel_y < 30:
        setze tunnel_y auf 30.0
    wenn tunnel_y > HOEHE - TUNNEL_MIN - 30:
        setze tunnel_y auf (HOEHE - TUNNEL_MIN - 30) * 1.0
    wand_oben.hinzufügen(tunnel_y)
    wand_unten.hinzufügen(tunnel_y + TUNNEL_MIN + (seed % 50))
    setze ti auf ti + 1

# Hindernisse
setze hind_x auf []
setze hind_y auf []
setze hind_aktiv auf []
setze hi auf 0
solange hi < MAX_HINDER:
    hind_x.hinzufügen(BREITE + hi * 150.0)
    setze seed auf (seed * 1103515245 + 12345) % 2147483648
    hind_y.hinzufügen(80 + seed % 240.0)
    hind_aktiv.hinzufügen(wahr)
    setze hi auf hi + 1

setze punkte auf 0
setze game_over auf falsch
setze rotor_frame auf 0

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Helicopter", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn game_over:
        stopp

    # Steigen/Sinken
    wenn taste_gedrückt("leertaste") oder maus_gedrückt(win):
        setze heli_vy auf heli_vy + AUFTRIEB
    sonst:
        setze heli_vy auf heli_vy + SCHWERKRAFT

    # Geschwindigkeit begrenzen
    wenn heli_vy > 5:
        setze heli_vy auf 5.0
    wenn heli_vy < -5:
        setze heli_vy auf -5.0

    setze heli_y auf heli_y + heli_vy

    # Scrollen
    setze scroll auf scroll + speed
    setze punkte auf punkte + 1

    # Tunnel-Kollision
    setze tunnel_idx auf (scroll + heli_x) / 2
    wenn tunnel_idx >= 0 und tunnel_idx < 300:
        wenn heli_y < wand_oben[tunnel_idx] + 5:
            setze game_over auf wahr
        wenn heli_y + 20 > wand_unten[tunnel_idx] - 5:
            setze game_over auf wahr

    # Bildschirmrand
    wenn heli_y < 0 oder heli_y > HOEHE - 20:
        setze game_over auf wahr

    # Hindernisse
    setze hi auf 0
    solange hi < MAX_HINDER:
        setze hind_x[hi] auf hind_x[hi] - speed
        wenn hind_x[hi] < -20:
            setze hind_x[hi] auf BREITE + 100.0
            setze seed auf (seed * 1103515245 + 12345) % 2147483648
            setze hind_y[hi] auf (80 + seed % 240) * 1.0
        # Kollision
        wenn hind_x[hi] > heli_x - 15 und hind_x[hi] < heli_x + 30:
            wenn hind_y[hi] > heli_y - 10 und hind_y[hi] < heli_y + 25:
                setze game_over auf wahr
        setze hi auf hi + 1

    setze rotor_frame auf rotor_frame + 1

    # Geschwindigkeit erhoehen
    wenn punkte % 500 == 0 und speed < 5:
        setze speed auf speed + 0.2

    # === ZEICHNEN ===
    fenster_löschen(win, "#87CEEB")

    # Tunnel
    setze ti auf 0
    solange ti < BREITE / 2:
        setze tidx auf (scroll / 2 + ti)
        wenn tidx >= 0 und tidx < 300:
            setze wo auf wand_oben[tidx]
            setze wu auf wand_unten[tidx]
            # Decke
            zeichne_rechteck(win, ti * 2, 0, 2, wo, "#795548")
            # Boden
            zeichne_rechteck(win, ti * 2, wu, 2, HOEHE - wu, "#795548")
            # Textur
            zeichne_rechteck(win, ti * 2, wo - 3, 2, 3, "#5D4037")
            zeichne_rechteck(win, ti * 2, wu, 2, 3, "#5D4037")
        setze ti auf ti + 1

    # Hindernisse
    setze hi auf 0
    solange hi < MAX_HINDER:
        wenn hind_x[hi] > -20 und hind_x[hi] < BREITE + 20:
            zeichne_rechteck(win, hind_x[hi] - 5, hind_y[hi] - 5, 10, 10, "#F44336")
        setze hi auf hi + 1

    # Helikopter
    wenn nicht game_over:
        # Koerper
        zeichne_rechteck(win, heli_x - 5, heli_y, 30, 15, "#4CAF50")
        zeichne_rechteck(win, heli_x + 20, heli_y + 3, 10, 8, "#81C784")
        # Cockpit
        zeichne_rechteck(win, heli_x - 8, heli_y + 3, 8, 10, "#90CAF9")
        # Rotor
        wenn rotor_frame % 4 < 2:
            zeichne_rechteck(win, heli_x - 10, heli_y - 3, 35, 2, "#607D8B")
        sonst:
            zeichne_rechteck(win, heli_x + 2, heli_y - 3, 15, 2, "#607D8B")
        # Heckrotor
        zeichne_rechteck(win, heli_x + 28, heli_y - 2, 2, 8, "#607D8B")
        # Kufen
        zeichne_rechteck(win, heli_x - 5, heli_y + 15, 25, 2, "#37474F")

    # Score
    setze si auf 0
    solange si < punkte / 100 und si < 40:
        zeichne_pixel(win, 10 + si * 4, 10, "#FFD700")
        zeichne_pixel(win, 10 + si * 4, 11, "#FFD700")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Game Over! Punkte: " + text(punkte)

# ============================================================
# moo Pipes — Rohre verbinden (Puzzle)
#
# Kompilieren: moo-compiler compile beispiele/pipes.moo -o beispiele/pipes
# Starten:     ./beispiele/pipes
#
# Maus = Rohr drehen (Klick)
# R = Neues Puzzle, Escape = Beenden
# ============================================================

setze BREITE auf 480
setze HOEHE auf 480
setze GRID auf 8
setze CELL auf 60

# Rohr-Typen: 0=leer, 1=gerade(H), 2=gerade(V), 3=kurve(RD), 4=kurve(RU), 5=kurve(LU), 6=kurve(LD)
# Verbindungen: R=rechts, D=unten, L=links, U=oben
setze brett auf []
setze seed auf 77

funktion zufall_rohr():
    setze seed auf (seed * 1103515245 + 12345) % 2147483648
    gib_zurück 1 + seed % 6

setze gi auf 0
solange gi < GRID * GRID:
    brett.hinzufügen(zufall_rohr())
    setze gi auf gi + 1

# Start + Ziel
setze brett[0 * GRID + 0] auf 3
setze brett[(GRID - 1) * GRID + GRID - 1] auf 5

setze eingabe_cd auf 0
setze gelöst auf falsch

funktion rohr_drehen(idx):
    setze typ auf brett[idx]
    # 1→2→1 (gerade), 3→4→5→6→3 (kurven)
    wenn typ == 1:
        setze brett[idx] auf 2
    wenn typ == 2:
        setze brett[idx] auf 1
    wenn typ == 3:
        setze brett[idx] auf 4
    wenn typ == 4:
        setze brett[idx] auf 5
    wenn typ == 5:
        setze brett[idx] auf 6
    wenn typ == 6:
        setze brett[idx] auf 3

funktion rohr_farbe(typ):
    wenn typ == 1 oder typ == 2:
        gib_zurück "#42A5F5"
    wenn typ == 3:
        gib_zurück "#66BB6A"
    wenn typ == 4:
        gib_zurück "#FFA726"
    wenn typ == 5:
        gib_zurück "#EF5350"
    wenn typ == 6:
        gib_zurück "#AB47BC"
    gib_zurück "#333333"

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Pipes", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Maus-Klick: Rohr drehen
    wenn maus_gedrückt(win) und eingabe_cd <= 0:
        setze mx auf maus_x(win) / CELL
        setze my auf maus_y(win) / CELL
        wenn mx >= 0 und mx < GRID und my >= 0 und my < GRID:
            rohr_drehen(my * GRID + mx)
            setze eingabe_cd auf 12

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    # Neustart
    wenn taste_gedrückt("r") und eingabe_cd <= 0:
        setze gi auf 0
        solange gi < GRID * GRID:
            setze brett[gi] auf zufall_rohr()
            setze gi auf gi + 1
        setze brett[0] auf 3
        setze brett[(GRID - 1) * GRID + GRID - 1] auf 5
        setze eingabe_cd auf 20

    # === ZEICHNEN ===
    fenster_löschen(win, "#263238")

    setze gy auf 0
    solange gy < GRID:
        setze gx auf 0
        solange gx < GRID:
            setze px auf gx * CELL
            setze py auf gy * CELL
            setze typ auf brett[gy * GRID + gx]
            setze rf auf rohr_farbe(typ)

            # Hintergrund
            zeichne_rechteck(win, px + 1, py + 1, CELL - 2, CELL - 2, "#37474F")

            # Rohr zeichnen
            setze cx auf px + CELL / 2
            setze cy auf py + CELL / 2
            setze rw auf 10

            wenn typ == 1:
                # Horizontal
                zeichne_rechteck(win, px + 5, cy - rw / 2, CELL - 10, rw, rf)
            wenn typ == 2:
                # Vertikal
                zeichne_rechteck(win, cx - rw / 2, py + 5, rw, CELL - 10, rf)
            wenn typ == 3:
                # Kurve Rechts-Unten
                zeichne_rechteck(win, cx, cy - rw / 2, CELL / 2, rw, rf)
                zeichne_rechteck(win, cx - rw / 2, cy, rw, CELL / 2, rf)
            wenn typ == 4:
                # Kurve Rechts-Oben
                zeichne_rechteck(win, cx, cy - rw / 2, CELL / 2, rw, rf)
                zeichne_rechteck(win, cx - rw / 2, py + 5, rw, CELL / 2 - 5, rf)
            wenn typ == 5:
                # Kurve Links-Oben
                zeichne_rechteck(win, px + 5, cy - rw / 2, CELL / 2, rw, rf)
                zeichne_rechteck(win, cx - rw / 2, py + 5, rw, CELL / 2 - 5, rf)
            wenn typ == 6:
                # Kurve Links-Unten
                zeichne_rechteck(win, px + 5, cy - rw / 2, CELL / 2, rw, rf)
                zeichne_rechteck(win, cx - rw / 2, cy, rw, CELL / 2, rf)

            # Verbindungspunkte
            zeichne_kreis(win, cx, cy, 6, rf)

            setze gx auf gx + 1
        setze gy auf gy + 1

    # Start/Ziel Markierung
    zeichne_kreis(win, CELL / 2, CELL / 2, 12, "#4CAF50")
    zeichne_kreis(win, (GRID - 1) * CELL + CELL / 2, (GRID - 1) * CELL + CELL / 2, 12, "#F44336")

    # Hover
    setze mx auf maus_x(win) / CELL
    setze my auf maus_y(win) / CELL
    wenn mx >= 0 und mx < GRID und my >= 0 und my < GRID:
        zeichne_rechteck(win, mx * CELL, my * CELL, CELL, 2, "#FFFFFF")
        zeichne_rechteck(win, mx * CELL, my * CELL + CELL - 2, CELL, 2, "#FFFFFF")
        zeichne_rechteck(win, mx * CELL, my * CELL, 2, CELL, "#FFFFFF")
        zeichne_rechteck(win, mx * CELL + CELL - 2, my * CELL, 2, CELL, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Pipes beendet!"

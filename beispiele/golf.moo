# ============================================================
# moo Golf — 2D Mini-Golf
#
# Kompilieren: moo-compiler compile beispiele/golf.moo -o beispiele/golf
# Starten:     ./beispiele/golf
#
# Maus = Zielen (Linie zeigt Richtung+Kraft)
# Klick = Schlagen
# R = Neustart, Escape = Beenden
# ============================================================

setze BREITE auf 600
setze HOEHE auf 500
setze BALL_R auf 6
setze LOCH_R auf 12
setze MAX_LEVEL auf 5
setze REIBUNG auf 0.98

# Ball
setze ball_x auf 0.0
setze ball_y auf 0.0
setze ball_vx auf 0.0
setze ball_vy auf 0.0
setze ball_ruht auf wahr

# Level-Daten
setze start_x auf []
setze start_y auf []
setze loch_x auf []
setze loch_y auf []

# Level 1: Gerade
start_x.hinzufügen(100)
start_y.hinzufügen(250)
loch_x.hinzufügen(500)
loch_y.hinzufügen(250)

# Level 2: Diagonal
start_x.hinzufügen(80)
start_y.hinzufügen(400)
loch_x.hinzufügen(520)
loch_y.hinzufügen(100)

# Level 3: Kurve
start_x.hinzufügen(100)
start_y.hinzufügen(100)
loch_x.hinzufügen(500)
loch_y.hinzufügen(400)

# Level 4: Eng
start_x.hinzufügen(50)
start_y.hinzufügen(250)
loch_x.hinzufügen(550)
loch_y.hinzufügen(250)

# Level 5: Weit
start_x.hinzufügen(80)
start_y.hinzufügen(450)
loch_x.hinzufügen(520)
loch_y.hinzufügen(50)

# Hindernisse pro Level (Rechtecke: x,y,w,h)
setze hind_x auf []
setze hind_y auf []
setze hind_w auf []
setze hind_h auf []
setze hind_level auf []

funktion hindernis(hx, hy, hw, hh, lev):
    hind_x.hinzufügen(hx)
    hind_y.hinzufügen(hy)
    hind_w.hinzufügen(hw)
    hind_h.hinzufügen(hh)
    hind_level.hinzufügen(lev)

# Level 1
hindernis(300, 200, 20, 100, 0)
# Level 2
hindernis(250, 150, 100, 20, 1)
hindernis(350, 300, 100, 20, 1)
# Level 3
hindernis(200, 200, 20, 150, 2)
hindernis(400, 150, 20, 150, 2)
# Level 4
hindernis(200, 100, 20, 130, 3)
hindernis(200, 270, 20, 130, 3)
hindernis(400, 100, 20, 130, 3)
hindernis(400, 270, 20, 130, 3)
# Level 5
hindernis(150, 150, 150, 20, 4)
hindernis(300, 330, 150, 20, 4)

setze hind_anzahl auf länge(hind_x)

setze level auf 0
setze schlaege auf 0
setze total_schlaege auf 0
setze eingabe_cd auf 0

funktion level_start():
    setze ball_x auf start_x[level] * 1.0
    setze ball_y auf start_y[level] * 1.0
    setze ball_vx auf 0.0
    setze ball_vy auf 0.0
    setze ball_ruht auf wahr
    setze schlaege auf 0

level_start()

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Golf", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Neustart Level
    wenn taste_gedrückt("r") und eingabe_cd <= 0:
        level_start()
        setze eingabe_cd auf 15

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    # Schlag (Maus)
    wenn ball_ruht und maus_gedrückt(win) und eingabe_cd <= 0:
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        setze ddx auf mx - ball_x
        setze ddy auf my - ball_y
        # Kraft begrenzen
        setze kraft auf sqrt(ddx * ddx + ddy * ddy)
        wenn kraft > 1:
            setze ball_vx auf ddx / kraft * (kraft * 0.05)
            setze ball_vy auf ddy / kraft * (kraft * 0.05)
            wenn ball_vx > 8:
                setze ball_vx auf 8.0
            wenn ball_vx < -8:
                setze ball_vx auf -8.0
            wenn ball_vy > 8:
                setze ball_vy auf 8.0
            wenn ball_vy < -8:
                setze ball_vy auf -8.0
            setze ball_ruht auf falsch
            setze schlaege auf schlaege + 1
            setze eingabe_cd auf 20

    # Ball-Physik
    wenn nicht ball_ruht:
        setze ball_x auf ball_x + ball_vx
        setze ball_y auf ball_y + ball_vy
        setze ball_vx auf ball_vx * REIBUNG
        setze ball_vy auf ball_vy * REIBUNG

        # Waende
        wenn ball_x < BALL_R:
            setze ball_x auf BALL_R * 1.0
            setze ball_vx auf abs(ball_vx) * 0.8
        wenn ball_x > BREITE - BALL_R:
            setze ball_x auf (BREITE - BALL_R) * 1.0
            setze ball_vx auf 0 - abs(ball_vx) * 0.8
        wenn ball_y < BALL_R:
            setze ball_y auf BALL_R * 1.0
            setze ball_vy auf abs(ball_vy) * 0.8
        wenn ball_y > HOEHE - BALL_R:
            setze ball_y auf (HOEHE - BALL_R) * 1.0
            setze ball_vy auf 0 - abs(ball_vy) * 0.8

        # Hindernis-Kollision
        setze hi auf 0
        solange hi < hind_anzahl:
            wenn hind_level[hi] == level:
                setze hx auf hind_x[hi]
                setze hy auf hind_y[hi]
                setze hw auf hind_w[hi]
                setze hh auf hind_h[hi]
                wenn ball_x + BALL_R > hx und ball_x - BALL_R < hx + hw:
                    wenn ball_y + BALL_R > hy und ball_y - BALL_R < hy + hh:
                        # Einfache Reflexion
                        wenn ball_x < hx + hw / 2:
                            setze ball_vx auf 0 - abs(ball_vx)
                            setze ball_x auf (hx - BALL_R) * 1.0
                        sonst:
                            setze ball_vx auf abs(ball_vx)
                            setze ball_x auf (hx + hw + BALL_R) * 1.0
                        wenn ball_y < hy + hh / 2:
                            setze ball_vy auf 0 - abs(ball_vy)
                        sonst:
                            setze ball_vy auf abs(ball_vy)
            setze hi auf hi + 1

        # Ruht?
        wenn abs(ball_vx) < 0.1 und abs(ball_vy) < 0.1:
            setze ball_vx auf 0.0
            setze ball_vy auf 0.0
            setze ball_ruht auf wahr

    # Loch erreicht?
    setze ddx auf ball_x - loch_x[level]
    setze ddy auf ball_y - loch_y[level]
    wenn ddx * ddx + ddy * ddy < LOCH_R * LOCH_R:
        setze total_schlaege auf total_schlaege + schlaege
        setze level auf level + 1
        wenn level >= MAX_LEVEL:
            stopp
        level_start()

    # === ZEICHNEN ===
    fenster_löschen(win, "#4CAF50")

    # Rasen-Textur
    setze ri auf 0
    solange ri < 30:
        zeichne_linie(win, 0, ri * 18, BREITE, ri * 18, "#43A047")
        setze ri auf ri + 1

    # Hindernisse
    setze hi auf 0
    solange hi < hind_anzahl:
        wenn hind_level[hi] == level:
            zeichne_rechteck(win, hind_x[hi], hind_y[hi], hind_w[hi], hind_h[hi], "#5D4037")
            zeichne_rechteck(win, hind_x[hi] + 2, hind_y[hi] + 2, hind_w[hi] - 4, hind_h[hi] - 4, "#795548")
        setze hi auf hi + 1

    # Loch
    zeichne_kreis(win, loch_x[level], loch_y[level], LOCH_R, "#212121")
    zeichne_kreis(win, loch_x[level], loch_y[level], LOCH_R - 3, "#1A1A1A")
    # Fahne
    zeichne_rechteck(win, loch_x[level], loch_y[level] - 35, 2, 35, "#FFFFFF")
    zeichne_rechteck(win, loch_x[level] + 2, loch_y[level] - 35, 15, 10, "#F44336")

    # Ziellinie (wenn Ball ruht)
    wenn ball_ruht:
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        zeichne_linie(win, ball_x, ball_y, mx, my, "#FFFFFF")

    # Ball
    zeichne_kreis(win, ball_x, ball_y, BALL_R, "#FFFFFF")
    zeichne_kreis(win, ball_x - 1, ball_y - 1, 2, "#E0E0E0")

    # HUD
    # Level
    setze li auf 0
    solange li < MAX_LEVEL:
        wenn li < level:
            zeichne_kreis(win, 20 + li * 25, 20, 8, "#FFD700")
        sonst:
            wenn li == level:
                zeichne_kreis(win, 20 + li * 25, 20, 8, "#FFFFFF")
            sonst:
                zeichne_kreis(win, 20 + li * 25, 20, 8, "#616161")
        setze li auf li + 1

    # Schlaege
    setze si auf 0
    solange si < schlaege und si < 10:
        zeichne_rechteck(win, BREITE - 20 - si * 12, 15, 8, 10, "#FF9800")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Golf beendet! Level: " + text(level) + "/" + text(MAX_LEVEL) + " Schlaege: " + text(total_schlaege)

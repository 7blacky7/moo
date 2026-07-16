# ============================================================
# Tetris — Klassiker in moo
# Steuerung: Links/Rechts/Runter = bewegen, Oben = rotieren,
#            Leertaste = Hard-Drop, ESC = beenden
#
# Hinweis zu moo-Einschraenkungen (Stand Stresstest):
#  1. Listen-Literale muessen auf EINER Zeile stehen
#     (mehrzeilige [ ... ] werden vom Parser abgelehnt).
#  2. Globale Variablen sind innerhalb von `funktion` NICHT
#     sichtbar. Deshalb ist die Spiellogik komplett in die
#     Hauptschleife inline-geschrieben — keine Hilfsfunktionen.
# ============================================================

konstante BREITE auf 800
konstante HOEHE auf 600
konstante ZELLE auf 28
konstante COLS auf 10
konstante ROWS auf 20
konstante BRETT_X auf 40
konstante BRETT_Y auf 20

# --- Fenster ---
setze fenster auf fenster_erstelle("moo Tetris", BREITE, HOEHE)

# --- Brett: 20 Zeilen x 10 Spalten, 0 = leer, sonst Farb-Index ---
setze brett auf []
setze r auf 0
solange r < ROWS:
    setze zeile auf []
    setze c auf 0
    solange c < COLS:
        zeile.hinzufügen(0)
        c += 1
    brett.hinzufügen(zeile)
    r += 1

# --- Farben (Index 1-7) ---
setze farben auf ["schwarz", "cyan", "gelb", "magenta", "gruen", "rot", "blau", "orange"]

# --- Tetromino-Definitionen: pro Stueck 4 Rotationen, je 4 [dx,dy] ---
setze I_rot auf [[[0, 1], [1, 1], [2, 1], [3, 1]], [[2, 0], [2, 1], [2, 2], [2, 3]], [[0, 2], [1, 2], [2, 2], [3, 2]], [[1, 0], [1, 1], [1, 2], [1, 3]]]
setze O_rot auf [[[1, 0], [2, 0], [1, 1], [2, 1]], [[1, 0], [2, 0], [1, 1], [2, 1]], [[1, 0], [2, 0], [1, 1], [2, 1]], [[1, 0], [2, 0], [1, 1], [2, 1]]]
setze T_rot auf [[[1, 0], [0, 1], [1, 1], [2, 1]], [[1, 0], [1, 1], [2, 1], [1, 2]], [[0, 1], [1, 1], [2, 1], [1, 2]], [[1, 0], [0, 1], [1, 1], [1, 2]]]
setze S_rot auf [[[1, 0], [2, 0], [0, 1], [1, 1]], [[1, 0], [1, 1], [2, 1], [2, 2]], [[1, 1], [2, 1], [0, 2], [1, 2]], [[0, 0], [0, 1], [1, 1], [1, 2]]]
setze Z_rot auf [[[0, 0], [1, 0], [1, 1], [2, 1]], [[2, 0], [1, 1], [2, 1], [1, 2]], [[0, 1], [1, 1], [1, 2], [2, 2]], [[1, 0], [0, 1], [1, 1], [0, 2]]]
setze J_rot auf [[[0, 0], [0, 1], [1, 1], [2, 1]], [[1, 0], [2, 0], [1, 1], [1, 2]], [[0, 1], [1, 1], [2, 1], [2, 2]], [[1, 0], [1, 1], [0, 2], [1, 2]]]
setze L_rot auf [[[2, 0], [0, 1], [1, 1], [2, 1]], [[1, 0], [1, 1], [1, 2], [2, 2]], [[0, 1], [1, 1], [2, 1], [0, 2]], [[0, 0], [1, 0], [1, 1], [1, 2]]]

setze alle_stuecke auf [I_rot, O_rot, T_rot, S_rot, Z_rot, J_rot, L_rot]

# --- Spielzustand ---
setze aktuell auf boden(zufall() * 7)
setze rotation auf 0
setze stueck_x auf 3
setze stueck_y auf 0
setze punkte auf 0
setze level auf 1
setze linien_gesamt auf 0
setze spielende auf falsch
setze tick_frames auf 10
setze frame_seit_fall auf 0

# --- Game Loop ---
solange fenster_offen(fenster) und nicht spielende:

    # --- Input ---
    wenn taste_gedrückt("links"):
        # Kollisionstest links
        setze bloecke auf alle_stuecke[aktuell][rotation]
        setze kol auf falsch
        für b in bloecke:
            setze bx auf stueck_x - 1 + b[0]
            setze by auf stueck_y + b[1]
            wenn bx < 0 oder bx >= COLS oder by >= ROWS:
                setze kol auf wahr
            sonst wenn by >= 0:
                setze z auf brett[by]
                wenn z[bx] != 0:
                    setze kol auf wahr
        wenn nicht kol:
            setze stueck_x auf stueck_x - 1

    wenn taste_gedrückt("rechts"):
        setze bloecke auf alle_stuecke[aktuell][rotation]
        setze kol auf falsch
        für b in bloecke:
            setze bx auf stueck_x + 1 + b[0]
            setze by auf stueck_y + b[1]
            wenn bx < 0 oder bx >= COLS oder by >= ROWS:
                setze kol auf wahr
            sonst wenn by >= 0:
                setze z auf brett[by]
                wenn z[bx] != 0:
                    setze kol auf wahr
        wenn nicht kol:
            setze stueck_x auf stueck_x + 1

    wenn taste_gedrückt("unten"):
        setze bloecke auf alle_stuecke[aktuell][rotation]
        setze kol auf falsch
        für b in bloecke:
            setze bx auf stueck_x + b[0]
            setze by auf stueck_y + 1 + b[1]
            wenn bx < 0 oder bx >= COLS oder by >= ROWS:
                setze kol auf wahr
            sonst wenn by >= 0:
                setze z auf brett[by]
                wenn z[bx] != 0:
                    setze kol auf wahr
        wenn nicht kol:
            setze stueck_y auf stueck_y + 1
            punkte += 1

    wenn taste_gedrückt("oben"):
        setze neu_rot auf (rotation + 1) % 4
        setze bloecke auf alle_stuecke[aktuell][neu_rot]
        setze kol auf falsch
        für b in bloecke:
            setze bx auf stueck_x + b[0]
            setze by auf stueck_y + b[1]
            wenn bx < 0 oder bx >= COLS oder by >= ROWS:
                setze kol auf wahr
            sonst wenn by >= 0:
                setze z auf brett[by]
                wenn z[bx] != 0:
                    setze kol auf wahr
        wenn nicht kol:
            setze rotation auf neu_rot

    wenn taste_gedrückt("leertaste"):
        # Hard-Drop: solange nach unten kollisionsfrei, absenken
        setze drop auf wahr
        solange drop:
            setze bloecke auf alle_stuecke[aktuell][rotation]
            setze kol auf falsch
            für b in bloecke:
                setze bx auf stueck_x + b[0]
                setze by auf stueck_y + 1 + b[1]
                wenn bx < 0 oder bx >= COLS oder by >= ROWS:
                    setze kol auf wahr
                sonst wenn by >= 0:
                    setze z auf brett[by]
                    wenn z[bx] != 0:
                        setze kol auf wahr
            wenn kol:
                setze drop auf falsch
            sonst:
                setze stueck_y auf stueck_y + 1
                punkte += 2

    wenn taste_gedrückt("escape"):
        setze spielende auf wahr

    # --- Automatischer Fall ---
    setze frame_seit_fall auf frame_seit_fall + 1
    wenn frame_seit_fall >= tick_frames:
        # Kann das Stueck eine Zeile weiter runter?
        setze bloecke auf alle_stuecke[aktuell][rotation]
        setze kol auf falsch
        für b in bloecke:
            setze bx auf stueck_x + b[0]
            setze by auf stueck_y + 1 + b[1]
            wenn bx < 0 oder bx >= COLS oder by >= ROWS:
                setze kol auf wahr
            sonst wenn by >= 0:
                setze z auf brett[by]
                wenn z[bx] != 0:
                    setze kol auf wahr

        wenn kol:
            # Fixieren
            setze bloecke auf alle_stuecke[aktuell][rotation]
            für b in bloecke:
                setze bx auf stueck_x + b[0]
                setze by auf stueck_y + b[1]
                wenn by >= 0 und by < ROWS und bx >= 0 und bx < COLS:
                    setze z auf brett[by]
                    z[bx] = aktuell + 1
                    brett[by] = z

            # Linien pruefen
            setze neue_zeilen auf []
            setze geloeschte auf 0
            für zz in brett:
                setze voll auf wahr
                für v in zz:
                    wenn v == 0:
                        setze voll auf falsch
                wenn voll:
                    geloeschte += 1
                sonst:
                    neue_zeilen.hinzufügen(zz)
            setze aufgefuellt auf []
            setze gi auf 0
            solange gi < geloeschte:
                setze leer auf []
                setze cc auf 0
                solange cc < COLS:
                    leer.hinzufügen(0)
                    cc += 1
                aufgefuellt.hinzufügen(leer)
                gi += 1
            für nz in neue_zeilen:
                aufgefuellt.hinzufügen(nz)
            setze brett auf aufgefuellt

            wenn geloeschte == 1:
                punkte += 100 * level
            wenn geloeschte == 2:
                punkte += 300 * level
            wenn geloeschte == 3:
                punkte += 500 * level
            wenn geloeschte == 4:
                punkte += 800 * level
            setze linien_gesamt auf linien_gesamt + geloeschte
            setze neu_level auf boden(linien_gesamt / 10) + 1
            wenn neu_level > level:
                setze level auf neu_level
                wenn tick_frames > 2:
                    setze tick_frames auf tick_frames - 1

            # Neues Stueck
            setze aktuell auf boden(zufall() * 7)
            setze rotation auf 0
            setze stueck_x auf 3
            setze stueck_y auf 0
            # Kollision beim Spawn = Game Over
            setze bloecke auf alle_stuecke[aktuell][0]
            setze kol auf falsch
            für b in bloecke:
                setze bx auf 3 + b[0]
                setze by auf 0 + b[1]
                wenn by >= 0 und by < ROWS und bx >= 0 und bx < COLS:
                    setze z auf brett[by]
                    wenn z[bx] != 0:
                        setze kol auf wahr
            wenn kol:
                setze spielende auf wahr
        sonst:
            setze stueck_y auf stueck_y + 1
        setze frame_seit_fall auf 0

    # --- Zeichnen ---
    fenster_löschen(fenster, "schwarz")

    # Brett-Rahmen + Hintergrund
    zeichne_rechteck(fenster, BRETT_X - 2, BRETT_Y - 2, COLS * ZELLE + 4, ROWS * ZELLE + 4, "grau")
    zeichne_rechteck(fenster, BRETT_X, BRETT_Y, COLS * ZELLE, ROWS * ZELLE, "schwarz")

    # Fixierte Steine
    setze ry auf 0
    solange ry < ROWS:
        setze zeile auf brett[ry]
        setze rx auf 0
        solange rx < COLS:
            setze v auf zeile[rx]
            wenn v != 0:
                setze f auf farben[v]
                zeichne_rechteck(fenster, BRETT_X + rx * ZELLE + 1, BRETT_Y + ry * ZELLE + 1, ZELLE - 2, ZELLE - 2, f)
            rx += 1
        ry += 1

    # Aktuelles Stueck
    setze bloecke auf alle_stuecke[aktuell][rotation]
    setze akt_farbe auf farben[aktuell + 1]
    für b in bloecke:
        setze bx auf stueck_x + b[0]
        setze by auf stueck_y + b[1]
        wenn by >= 0:
            zeichne_rechteck(fenster, BRETT_X + bx * ZELLE + 1, BRETT_Y + by * ZELLE + 1, ZELLE - 2, ZELLE - 2, akt_farbe)

    # Seiten-Panel
    setze panel_x auf BRETT_X + COLS * ZELLE + 30
    zeichne_rechteck(fenster, panel_x, 20, 180, 560, "#101020")

    # Score-Balken
    setze sb auf punkte / 50
    wenn sb > 170:
        setze sb auf 170
    zeichne_rechteck(fenster, panel_x + 5, 40, 170, 20, "#202040")
    zeichne_rechteck(fenster, panel_x + 5, 40, sb, 20, "gelb")

    # Level-Blocks
    setze lx auf 0
    solange lx < level und lx < 10:
        zeichne_rechteck(fenster, panel_x + 5 + lx * 16, 80, 14, 20, "gruen")
        lx += 1

    # Linien-Blocks (2 Reihen)
    setze lnx auf 0
    solange lnx < linien_gesamt und lnx < 20:
        setze col auf lnx % 10
        setze row auf boden(lnx / 10)
        zeichne_rechteck(fenster, panel_x + 5 + col * 16, 120 + row * 24, 14, 20, "cyan")
        lnx += 1

    fenster_aktualisieren(fenster)
    warte(50)

# --- Game Over Screen ---
fenster_löschen(fenster, "schwarz")
zeichne_rechteck(fenster, 200, 200, 400, 200, "rot")
zeichne_rechteck(fenster, 210, 210, 380, 180, "schwarz")

setze eb auf punkte / 20
wenn eb > 360:
    setze eb auf 360
zeichne_rechteck(fenster, 220, 240, eb, 30, "gelb")

setze gx auf 0
solange gx < level und gx < 20:
    zeichne_rechteck(fenster, 220 + gx * 18, 290, 16, 20, "gruen")
    gx += 1

setze glx auf 0
solange glx < linien_gesamt und glx < 20:
    zeichne_rechteck(fenster, 220 + glx * 18, 330, 16, 20, "cyan")
    glx += 1

fenster_aktualisieren(fenster)
warte(3000)

fenster_schliessen(fenster)
zeige f"Game Over! Punkte: {punkte} Level: {level} Linien: {linien_gesamt}"

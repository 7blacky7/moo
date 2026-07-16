# ============================================================
# moo Schach — Vollstaendiges Schachspiel
#
# Kompilieren: moo-compiler compile beispiele/schach.moo -o beispiele/schach
# Starten:     ./beispiele/schach
#
# Maus = Figur waehlen + Ziel klicken
# R = Neustart, Escape = Beenden
# ============================================================

setze BREITE auf 640
setze HOEHE auf 640
setze CELL auf 80

# Brett: 0=leer, positiv=Weiss, negativ=Schwarz
# 1=Bauer, 2=Turm, 3=Springer, 4=Laeufer, 5=Dame, 6=Koenig
setze brett auf []

# Startposition
funktion brett_init():
    setze brett auf []
    # Reihe 0 (Schwarz oben)
    brett.hinzufügen(-2)
    brett.hinzufügen(-3)
    brett.hinzufügen(-4)
    brett.hinzufügen(-5)
    brett.hinzufügen(-6)
    brett.hinzufügen(-4)
    brett.hinzufügen(-3)
    brett.hinzufügen(-2)
    # Reihe 1 (Schwarze Bauern)
    setze bi auf 0
    solange bi < 8:
        brett.hinzufügen(-1)
        setze bi auf bi + 1
    # Reihen 2-5 (leer)
    setze bi auf 0
    solange bi < 32:
        brett.hinzufügen(0)
        setze bi auf bi + 1
    # Reihe 6 (Weisse Bauern)
    setze bi auf 0
    solange bi < 8:
        brett.hinzufügen(1)
        setze bi auf bi + 1
    # Reihe 7 (Weiss unten)
    brett.hinzufügen(2)
    brett.hinzufügen(3)
    brett.hinzufügen(4)
    brett.hinzufügen(5)
    brett.hinzufügen(6)
    brett.hinzufügen(4)
    brett.hinzufügen(3)
    brett.hinzufügen(2)

brett_init()

setze auswahl_x auf -1
setze auswahl_y auf -1
setze spieler auf 1
setze game_over auf falsch
setze eingabe_cd auf 0

funktion figur_bei(fx, fy):
    wenn fx < 0 oder fx > 7 oder fy < 0 oder fy > 7:
        gib_zurück 0
    gib_zurück brett[fy * 8 + fx]

funktion ist_eigen(fig, team):
    wenn team == 1 und fig > 0:
        gib_zurück wahr
    wenn team == -1 und fig < 0:
        gib_zurück wahr
    gib_zurück falsch

funktion ist_feind(fig, team):
    wenn team == 1 und fig < 0:
        gib_zurück wahr
    wenn team == -1 und fig > 0:
        gib_zurück wahr
    gib_zurück falsch

funktion abs_val(val):
    wenn val < 0:
        gib_zurück 0 - val
    gib_zurück val

funktion zug_gueltig(von_x, von_y, nach_x, nach_y, team):
    setze fig auf figur_bei(von_x, von_y)
    setze ziel auf figur_bei(nach_x, nach_y)
    setze typ auf abs_val(fig)
    setze ddx auf nach_x - von_x
    setze ddy auf nach_y - von_y
    setze adx auf abs_val(ddx)
    setze ady auf abs_val(ddy)

    # Kann nicht auf eigene Figur ziehen
    wenn ist_eigen(ziel, team):
        gib_zurück falsch

    # Bauer
    wenn typ == 1:
        setze richtung auf 0 - team
        wenn ddx == 0 und ddy == richtung und ziel == 0:
            gib_zurück wahr
        # Doppelschritt vom Start
        wenn ddx == 0 und ddy == richtung * 2:
            wenn team == 1 und von_y == 6 und ziel == 0:
                wenn figur_bei(von_x, von_y + richtung) == 0:
                    gib_zurück wahr
            wenn team == -1 und von_y == 1 und ziel == 0:
                wenn figur_bei(von_x, von_y + richtung) == 0:
                    gib_zurück wahr
        # Diagonal schlagen
        wenn adx == 1 und ddy == richtung und ist_feind(ziel, team):
            gib_zurück wahr
        gib_zurück falsch

    # Turm
    wenn typ == 2:
        wenn ddx != 0 und ddy != 0:
            gib_zurück falsch
        # Weg frei?
        setze sx auf 0
        setze sy auf 0
        wenn ddx > 0:
            setze sx auf 1
        wenn ddx < 0:
            setze sx auf -1
        wenn ddy > 0:
            setze sy auf 1
        wenn ddy < 0:
            setze sy auf -1
        setze cx auf von_x + sx
        setze cy auf von_y + sy
        solange cx != nach_x oder cy != nach_y:
            wenn figur_bei(cx, cy) != 0:
                gib_zurück falsch
            setze cx auf cx + sx
            setze cy auf cy + sy
        gib_zurück wahr

    # Springer
    wenn typ == 3:
        wenn adx == 2 und ady == 1:
            gib_zurück wahr
        wenn adx == 1 und ady == 2:
            gib_zurück wahr
        gib_zurück falsch

    # Laeufer
    wenn typ == 4:
        wenn adx != ady oder adx == 0:
            gib_zurück falsch
        setze sx auf 0
        setze sy auf 0
        wenn ddx > 0:
            setze sx auf 1
        wenn ddx < 0:
            setze sx auf -1
        wenn ddy > 0:
            setze sy auf 1
        wenn ddy < 0:
            setze sy auf -1
        setze cx auf von_x + sx
        setze cy auf von_y + sy
        solange cx != nach_x oder cy != nach_y:
            wenn figur_bei(cx, cy) != 0:
                gib_zurück falsch
            setze cx auf cx + sx
            setze cy auf cy + sy
        gib_zurück wahr

    # Dame = Turm + Laeufer
    wenn typ == 5:
        wenn ddx == 0 oder ddy == 0:
            # Turm-Bewegung
            setze sx auf 0
            setze sy auf 0
            wenn ddx > 0:
                setze sx auf 1
            wenn ddx < 0:
                setze sx auf -1
            wenn ddy > 0:
                setze sy auf 1
            wenn ddy < 0:
                setze sy auf -1
            setze cx auf von_x + sx
            setze cy auf von_y + sy
            solange cx != nach_x oder cy != nach_y:
                wenn figur_bei(cx, cy) != 0:
                    gib_zurück falsch
                setze cx auf cx + sx
                setze cy auf cy + sy
            gib_zurück wahr
        wenn adx == ady:
            # Laeufer-Bewegung
            setze sx auf 0
            setze sy auf 0
            wenn ddx > 0:
                setze sx auf 1
            wenn ddx < 0:
                setze sx auf -1
            wenn ddy > 0:
                setze sy auf 1
            wenn ddy < 0:
                setze sy auf -1
            setze cx auf von_x + sx
            setze cy auf von_y + sy
            solange cx != nach_x oder cy != nach_y:
                wenn figur_bei(cx, cy) != 0:
                    gib_zurück falsch
                setze cx auf cx + sx
                setze cy auf cy + sy
            gib_zurück wahr
        gib_zurück falsch

    # Koenig
    wenn typ == 6:
        wenn adx <= 1 und ady <= 1 und (adx + ady) > 0:
            gib_zurück wahr
        gib_zurück falsch

    gib_zurück falsch

funktion figur_farbe(fig):
    setze typ auf abs_val(fig)
    wenn fig > 0:
        # Weiss
        wenn typ == 1:
            gib_zurück "#ECEFF1"
        wenn typ == 2:
            gib_zurück "#B0BEC5"
        wenn typ == 3:
            gib_zurück "#CFD8DC"
        wenn typ == 4:
            gib_zurück "#E0E0E0"
        wenn typ == 5:
            gib_zurück "#FAFAFA"
        wenn typ == 6:
            gib_zurück "#FFFFFF"
    sonst:
        # Schwarz
        wenn typ == 1:
            gib_zurück "#37474F"
        wenn typ == 2:
            gib_zurück "#263238"
        wenn typ == 3:
            gib_zurück "#455A64"
        wenn typ == 4:
            gib_zurück "#546E7A"
        wenn typ == 5:
            gib_zurück "#1A237E"
        wenn typ == 6:
            gib_zurück "#212121"
    gib_zurück "#000000"

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Schach", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Maus-Input
    setze mx auf maus_x(win)
    setze my auf maus_y(win)
    setze feld_x auf mx / CELL
    setze feld_y auf my / CELL

    wenn maus_gedrückt(win) und eingabe_cd <= 0 und nicht game_over:
        wenn feld_x >= 0 und feld_x < 8 und feld_y >= 0 und feld_y < 8:
            wenn auswahl_x < 0:
                # Figur auswaehlen
                setze fig auf figur_bei(feld_x, feld_y)
                wenn ist_eigen(fig, spieler):
                    setze auswahl_x auf feld_x
                    setze auswahl_y auf feld_y
            sonst:
                # Zug versuchen
                wenn zug_gueltig(auswahl_x, auswahl_y, feld_x, feld_y, spieler):
                    setze ziel_fig auf brett[feld_y * 8 + feld_x]
                    # Koenig geschlagen?
                    wenn abs_val(ziel_fig) == 6:
                        setze game_over auf wahr
                    # Zug ausfuehren
                    setze brett[feld_y * 8 + feld_x] auf brett[auswahl_y * 8 + auswahl_x]
                    setze brett[auswahl_y * 8 + auswahl_x] auf 0
                    # Bauernumwandlung
                    setze gezogen auf brett[feld_y * 8 + feld_x]
                    wenn gezogen == 1 und feld_y == 0:
                        setze brett[feld_y * 8 + feld_x] auf 5
                    wenn gezogen == -1 und feld_y == 7:
                        setze brett[feld_y * 8 + feld_x] auf -5
                    # Spieler wechseln
                    setze spieler auf 0 - spieler
                setze auswahl_x auf -1
                setze auswahl_y auf -1
            setze eingabe_cd auf 12

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    # Neustart
    wenn taste_gedrückt("r"):
        brett_init()
        setze spieler auf 1
        setze game_over auf falsch
        setze auswahl_x auf -1
        setze auswahl_y auf -1

    # === ZEICHNEN ===
    fenster_löschen(win, "#5D4037")

    # Brett
    setze ry auf 0
    solange ry < 8:
        setze rx auf 0
        solange rx < 8:
            setze hell auf (rx + ry) % 2 == 0
            wenn hell:
                zeichne_rechteck(win, rx * CELL, ry * CELL, CELL, CELL, "#FFCC80")
            sonst:
                zeichne_rechteck(win, rx * CELL, ry * CELL, CELL, CELL, "#8D6E63")

            # Auswahl-Highlight
            wenn rx == auswahl_x und ry == auswahl_y:
                zeichne_rechteck(win, rx * CELL + 2, ry * CELL + 2, CELL - 4, CELL - 4, "#FFEB3B")

            # Figur zeichnen
            setze fig auf figur_bei(rx, ry)
            wenn fig != 0:
                setze typ auf abs_val(fig)
                setze ff auf figur_farbe(fig)
                setze cx auf rx * CELL + CELL / 2
                setze cy auf ry * CELL + CELL / 2

                # Basis-Kreis
                zeichne_kreis(win, cx, cy, 28, ff)

                # Typ-Symbol
                wenn typ == 1:
                    # Bauer: kleiner Kreis
                    zeichne_kreis(win, cx, cy - 5, 10, ff)
                    zeichne_rechteck(win, cx - 8, cy + 5, 16, 8, ff)
                wenn typ == 2:
                    # Turm: Zinnen
                    zeichne_rechteck(win, cx - 12, cy - 15, 24, 25, ff)
                    zeichne_rechteck(win, cx - 14, cy - 18, 6, 8, ff)
                    zeichne_rechteck(win, cx - 3, cy - 18, 6, 8, ff)
                    zeichne_rechteck(win, cx + 8, cy - 18, 6, 8, ff)
                wenn typ == 3:
                    # Springer: L-Form
                    zeichne_rechteck(win, cx - 5, cy - 15, 10, 25, ff)
                    zeichne_rechteck(win, cx - 5, cy - 15, 18, 8, ff)
                wenn typ == 4:
                    # Laeufer: Spitze
                    zeichne_kreis(win, cx, cy + 5, 14, ff)
                    zeichne_kreis(win, cx, cy - 10, 8, ff)
                wenn typ == 5:
                    # Dame: Krone
                    zeichne_kreis(win, cx, cy, 20, ff)
                    zeichne_kreis(win, cx, cy - 12, 6, ff)
                    zeichne_kreis(win, cx - 10, cy - 8, 4, ff)
                    zeichne_kreis(win, cx + 10, cy - 8, 4, ff)
                wenn typ == 6:
                    # Koenig: Kreuz
                    zeichne_kreis(win, cx, cy, 22, ff)
                    zeichne_rechteck(win, cx - 2, cy - 20, 4, 14, ff)
                    zeichne_rechteck(win, cx - 8, cy - 16, 16, 4, ff)

                # Umrandung fuer Kontrast
                wenn fig > 0:
                    zeichne_kreis(win, cx, cy, 29, "#5D4037")
                    zeichne_kreis(win, cx, cy, 27, ff)

            setze rx auf rx + 1
        setze ry auf ry + 1

    # Hover
    wenn feld_x >= 0 und feld_x < 8 und feld_y >= 0 und feld_y < 8:
        zeichne_rechteck(win, feld_x * CELL, feld_y * CELL, CELL, 2, "#FFFFFF")
        zeichne_rechteck(win, feld_x * CELL, feld_y * CELL + CELL - 2, CELL, 2, "#FFFFFF")
        zeichne_rechteck(win, feld_x * CELL, feld_y * CELL, 2, CELL, "#FFFFFF")
        zeichne_rechteck(win, feld_x * CELL + CELL - 2, feld_y * CELL, 2, CELL, "#FFFFFF")

    # Spieler-Anzeige
    wenn spieler == 1:
        zeichne_kreis(win, BREITE - 20, HOEHE - 20, 10, "#FFFFFF")
    sonst:
        zeichne_kreis(win, BREITE - 20, HOEHE - 20, 10, "#212121")

    # Game Over
    wenn game_over:
        zeichne_rechteck(win, BREITE / 2 - 100, HOEHE / 2 - 25, 200, 50, "#333333")
        wenn spieler == -1:
            zeichne_rechteck(win, BREITE / 2 - 80, HOEHE / 2 - 15, 160, 30, "#FFFFFF")
        sonst:
            zeichne_rechteck(win, BREITE / 2 - 80, HOEHE / 2 - 15, 160, 30, "#212121")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn spieler == -1:
    zeige "Weiss gewinnt!"
sonst:
    zeige "Schwarz gewinnt!"

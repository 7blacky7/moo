# ============================================================
# moo Vier Gewinnt (Connect 4) — 2 Spieler
#
# Kompilieren: moo-compiler compile beispiele/connect4.moo -o beispiele/connect4
# Starten:     ./beispiele/connect4
#
# 1-7 oder Links/Rechts + Leertaste = Spalte waehlen
# R = Neustart, Escape = Beenden
# ============================================================

setze BREITE auf 560
setze HOEHE auf 560
setze SPALTEN auf 7
setze REIHEN auf 6
setze CELL auf 70

# Brett (0=leer, 1=Rot, 2=Gelb)
setze brett auf []
setze bi auf 0
solange bi < SPALTEN * REIHEN:
    brett.hinzufügen(0)
    setze bi auf bi + 1

setze spieler auf 1
setze cursor auf 3
setze gewinner auf 0
setze game_over auf falsch

funktion stein_setzen(spalte, sp):
    # Von unten nach oben suchen
    setze reihe auf REIHEN - 1
    solange reihe >= 0:
        setze idx auf reihe * SPALTEN + spalte
        wenn brett[idx] == 0:
            setze brett[idx] auf sp
            gib_zurück reihe
        setze reihe auf reihe - 1
    gib_zurück -1

funktion check_vier(sx, sy, dx, dy, sp):
    setze count auf 0
    setze ci auf 0
    solange ci < 4:
        setze cx auf sx + ci * dx
        setze cy auf sy + ci * dy
        wenn cx >= 0 und cx < SPALTEN und cy >= 0 und cy < REIHEN:
            wenn brett[cy * SPALTEN + cx] == sp:
                setze count auf count + 1
        setze ci auf ci + 1
    gib_zurück count == 4

funktion check_gewonnen(sp):
    setze ry auf 0
    solange ry < REIHEN:
        setze rx auf 0
        solange rx < SPALTEN:
            # Horizontal
            wenn check_vier(rx, ry, 1, 0, sp):
                gib_zurück wahr
            # Vertikal
            wenn check_vier(rx, ry, 0, 1, sp):
                gib_zurück wahr
            # Diagonal rechts-unten
            wenn check_vier(rx, ry, 1, 1, sp):
                gib_zurück wahr
            # Diagonal links-unten
            wenn check_vier(rx, ry, -1, 1, sp):
                gib_zurück wahr
            setze rx auf rx + 1
        setze ry auf ry + 1
    gib_zurück falsch

funktion brett_voll():
    setze bi auf 0
    solange bi < SPALTEN * REIHEN:
        wenn brett[bi] == 0:
            gib_zurück falsch
        setze bi auf bi + 1
    gib_zurück wahr

setze eingabe_cooldown auf 0

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Vier Gewinnt", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn nicht game_over:
        wenn eingabe_cooldown <= 0:
            wenn taste_gedrückt("links") oder taste_gedrückt("a"):
                wenn cursor > 0:
                    setze cursor auf cursor - 1
                    setze eingabe_cooldown auf 10

            wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
                wenn cursor < SPALTEN - 1:
                    setze cursor auf cursor + 1
                    setze eingabe_cooldown auf 10

            wenn taste_gedrückt("leertaste"):
                setze reihe auf stein_setzen(cursor, spieler)
                wenn reihe >= 0:
                    wenn check_gewonnen(spieler):
                        setze gewinner auf spieler
                        setze game_over auf wahr
                    sonst:
                        wenn brett_voll():
                            setze game_over auf wahr
                        sonst:
                            wenn spieler == 1:
                                setze spieler auf 2
                            sonst:
                                setze spieler auf 1
                    setze eingabe_cooldown auf 15

        wenn eingabe_cooldown > 0:
            setze eingabe_cooldown auf eingabe_cooldown - 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#1565C0")

    # Brett
    setze ry auf 0
    solange ry < REIHEN:
        setze rx auf 0
        solange rx < SPALTEN:
            setze cx auf 35 + rx * CELL
            setze cy auf 100 + ry * CELL
            setze val auf brett[ry * SPALTEN + rx]
            wenn val == 0:
                zeichne_kreis(win, cx, cy, 28, "#0D47A1")
            wenn val == 1:
                zeichne_kreis(win, cx, cy, 28, "#F44336")
            wenn val == 2:
                zeichne_kreis(win, cx, cy, 28, "#FFEB3B")
            setze rx auf rx + 1
        setze ry auf ry + 1

    # Cursor
    setze cursor_x auf 35 + cursor * CELL
    wenn spieler == 1:
        zeichne_kreis(win, cursor_x, 40, 22, "#F44336")
    sonst:
        zeichne_kreis(win, cursor_x, 40, 22, "#FFEB3B")
    # Pfeil
    zeichne_linie(win, cursor_x, 60, cursor_x, 80, "#FFFFFF")
    zeichne_linie(win, cursor_x - 5, 72, cursor_x, 80, "#FFFFFF")
    zeichne_linie(win, cursor_x + 5, 72, cursor_x, 80, "#FFFFFF")

    # Gewinner-Anzeige
    wenn game_over:
        zeichne_rechteck(win, BREITE / 2 - 100, HOEHE / 2 - 25, 200, 50, "#333333")
        wenn gewinner == 1:
            zeichne_rechteck(win, BREITE / 2 - 80, HOEHE / 2 - 15, 160, 30, "#F44336")
        wenn gewinner == 2:
            zeichne_rechteck(win, BREITE / 2 - 80, HOEHE / 2 - 15, 160, 30, "#FFEB3B")
        wenn gewinner == 0:
            zeichne_rechteck(win, BREITE / 2 - 80, HOEHE / 2 - 15, 80, 30, "#F44336")
            zeichne_rechteck(win, BREITE / 2, HOEHE / 2 - 15, 80, 30, "#FFEB3B")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn gewinner == 1:
    zeige "Rot gewinnt!"
wenn gewinner == 2:
    zeige "Gelb gewinnt!"
wenn gewinner == 0 und game_over:
    zeige "Unentschieden!"

# ============================================================
# moo Wordle — Wort-Ratespiel (visuell)
#
# Kompilieren: moo-compiler compile beispiele/wordle.moo -o beispiele/wordle
# Starten:     ./beispiele/wordle
#
# A-Z = Buchstabe eingeben
# Eingabe/Leertaste = Wort pruefen
# Escape = Beenden
# ============================================================

setze BREITE auf 400
setze HOEHE auf 500
setze CELL auf 55
setze MAX_VERSUCHE auf 6
setze WORT_LEN auf 5

# Woerter-Liste (einfache deutsche 5-Buchstaben Woerter)
setze woerter auf []
woerter.hinzufügen("hallo")
woerter.hinzufügen("wurst")
woerter.hinzufügen("katze")
woerter.hinzufügen("hunde")
woerter.hinzufügen("birne")
woerter.hinzufügen("wolke")
woerter.hinzufügen("tiger")
woerter.hinzufügen("blume")
woerter.hinzufügen("stern")
woerter.hinzufügen("licht")
woerter.hinzufügen("regen")
woerter.hinzufügen("insel")
woerter.hinzufügen("perle")
woerter.hinzufügen("wiese")
woerter.hinzufügen("pferd")
woerter.hinzufügen("vogel")
woerter.hinzufügen("kerze")
woerter.hinzufügen("tafel")
woerter.hinzufügen("stuhl")
woerter.hinzufügen("tisch")

setze wort_anzahl auf länge(woerter)

# Ziel-Wort auswaehlen
setze seed auf 42
setze seed auf (seed * 1103515245 + 12345) % 2147483648
setze ziel_idx auf seed % wort_anzahl
setze ziel auf woerter[ziel_idx]

# Spielfeld: Versuche als Strings
setze versuche auf []
setze versuch_farben auf []
setze vi auf 0
solange vi < MAX_VERSUCHE:
    versuche.hinzufügen("")
    versuch_farben.hinzufügen("")
    setze vi auf vi + 1

setze aktuelle_reihe auf 0
setze aktuelles_wort auf ""
setze gewonnen auf falsch
setze game_over auf falsch
setze eingabe_cd auf 0

# Buchstabe zu Index (a=0, b=1, ...)
funktion buchstabe_zu_idx(ch):
    wenn ch == "a":
        gib_zurück 0
    wenn ch == "b":
        gib_zurück 1
    wenn ch == "c":
        gib_zurück 2
    wenn ch == "d":
        gib_zurück 3
    wenn ch == "e":
        gib_zurück 4
    wenn ch == "f":
        gib_zurück 5
    wenn ch == "g":
        gib_zurück 6
    wenn ch == "h":
        gib_zurück 7
    wenn ch == "i":
        gib_zurück 8
    wenn ch == "j":
        gib_zurück 9
    wenn ch == "k":
        gib_zurück 10
    wenn ch == "l":
        gib_zurück 11
    wenn ch == "m":
        gib_zurück 12
    wenn ch == "n":
        gib_zurück 13
    wenn ch == "o":
        gib_zurück 14
    wenn ch == "p":
        gib_zurück 15
    wenn ch == "q":
        gib_zurück 16
    wenn ch == "r":
        gib_zurück 17
    wenn ch == "s":
        gib_zurück 18
    wenn ch == "t":
        gib_zurück 19
    wenn ch == "u":
        gib_zurück 20
    wenn ch == "v":
        gib_zurück 21
    wenn ch == "w":
        gib_zurück 22
    wenn ch == "x":
        gib_zurück 23
    wenn ch == "y":
        gib_zurück 24
    wenn ch == "z":
        gib_zurück 25
    gib_zurück -1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Wordle", BREITE, HOEHE)

setze buchstaben auf "abcdefghijklmnopqrstuvwxyz"

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn nicht game_over und eingabe_cd <= 0:
        # Buchstaben-Input
        setze bi auf 0
        solange bi < 26:
            setze ch auf buchstaben[bi]
            wenn taste_gedrückt(ch):
                wenn länge(aktuelles_wort) < WORT_LEN:
                    setze aktuelles_wort auf aktuelles_wort + ch
                    setze eingabe_cd auf 10
            setze bi auf bi + 1

        # Loeschen (Backspace simulieren mit "q" — moo hat kein Backspace-Builtin)

        # Pruefen (Leertaste)
        wenn taste_gedrückt("leertaste"):
            wenn länge(aktuelles_wort) == WORT_LEN:
                setze versuche[aktuelle_reihe] auf aktuelles_wort
                # Farben berechnen: g=gruen, y=gelb, x=grau
                setze farben auf ""
                setze ci auf 0
                solange ci < WORT_LEN:
                    setze ch auf aktuelles_wort[ci]
                    setze zch auf ziel[ci]
                    wenn ch == zch:
                        setze farben auf farben + "g"
                    sonst:
                        # Im Wort enthalten?
                        setze gefunden auf falsch
                        setze ji auf 0
                        solange ji < WORT_LEN:
                            wenn ziel[ji] == ch:
                                setze gefunden auf wahr
                            setze ji auf ji + 1
                        wenn gefunden:
                            setze farben auf farben + "y"
                        sonst:
                            setze farben auf farben + "x"
                    setze ci auf ci + 1

                setze versuch_farben[aktuelle_reihe] auf farben

                # Gewonnen?
                wenn aktuelles_wort == ziel:
                    setze gewonnen auf wahr
                    setze game_over auf wahr

                setze aktuelle_reihe auf aktuelle_reihe + 1
                setze aktuelles_wort auf ""

                wenn aktuelle_reihe >= MAX_VERSUCHE:
                    setze game_over auf wahr

                setze eingabe_cd auf 15

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#121213")

    # Spielfeld
    setze ry auf 0
    solange ry < MAX_VERSUCHE:
        setze rx auf 0
        solange rx < WORT_LEN:
            setze px auf 35 + rx * (CELL + 5)
            setze py auf 20 + ry * (CELL + 5)

            setze bg auf "#3A3A3C"

            wenn ry < aktuelle_reihe:
                setze farben auf versuch_farben[ry]
                wenn länge(farben) > rx:
                    setze fc auf farben[rx]
                    wenn fc == "g":
                        setze bg auf "#538D4E"
                    wenn fc == "y":
                        setze bg auf "#B59F3B"
                    wenn fc == "x":
                        setze bg auf "#3A3A3C"

            zeichne_rechteck(win, px, py, CELL, CELL, bg)

            # Buchstabe als farbiger Punkt (kein Text-Rendering)
            wenn ry < aktuelle_reihe und länge(versuche[ry]) > rx:
                setze ch auf versuche[ry][rx]
                setze ch_idx auf buchstabe_zu_idx(ch)
                wenn ch_idx >= 0:
                    # Buchstabe als Muster aus Punkten (vereinfacht: Kreis + Position)
                    zeichne_kreis(win, px + CELL / 2, py + CELL / 2, 12, "#FFFFFF")
                    # Index als kleine Punkte
                    setze di auf 0
                    solange di <= ch_idx und di < 13:
                        zeichne_pixel(win, px + 10 + di * 3, py + CELL - 8, "#FFFFFF")
                        setze di auf di + 1

            # Aktuelle Eingabe
            wenn ry == aktuelle_reihe und rx < länge(aktuelles_wort):
                zeichne_rechteck(win, px + 2, py + 2, CELL - 4, CELL - 4, "#565656")
                setze ch auf aktuelles_wort[rx]
                setze ch_idx auf buchstabe_zu_idx(ch)
                wenn ch_idx >= 0:
                    zeichne_kreis(win, px + CELL / 2, py + CELL / 2, 12, "#FFFFFF")
                    setze di auf 0
                    solange di <= ch_idx und di < 13:
                        zeichne_pixel(win, px + 10 + di * 3, py + CELL - 8, "#FFFFFF")
                        setze di auf di + 1

            setze rx auf rx + 1
        setze ry auf ry + 1

    # Status
    wenn gewonnen:
        zeichne_rechteck(win, BREITE / 2 - 80, HOEHE - 45, 160, 30, "#538D4E")
    wenn game_over und nicht gewonnen:
        zeichne_rechteck(win, BREITE / 2 - 80, HOEHE - 45, 160, 30, "#F44336")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn gewonnen:
    zeige "Gewonnen in " + text(aktuelle_reihe) + " Versuchen!"
sonst:
    zeige "Verloren! Das Wort war: " + ziel

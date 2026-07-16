# ============================================================
# Snake — Klassisches Schlangenspiel in moo
# Steuerung: Pfeiltasten oder WASD
# Leertaste: Neustart nach Game Over
# ESC: Beenden
# ============================================================

konstante BREITE auf 640
konstante HOEHE auf 480
konstante ZELLE auf 20
konstante COLS auf 32
konstante ROWS auf 24

setze fenster auf fenster_erstelle("moo Snake", BREITE, HOEHE)

# --- Spielzustand ---
setze schlange auf [[16, 12], [15, 12], [14, 12]]
setze richtung_x auf 1
setze richtung_y auf 0
setze essen auf [20, 12]
setze punkte auf 0
setze spielende auf falsch
setze geschwindigkeit auf 150

funktion neues_essen():
    # Zufaellige Position die nicht auf der Schlange liegt
    setze tries auf 0
    solange tries < 200:
        setze ex auf boden(zufall() * 32)
        setze ey auf boden(zufall() * 24)
        setze ok auf wahr
        für seg in schlange:
            wenn seg[0] == ex:
                wenn seg[1] == ey:
                    setze ok auf falsch
        wenn ok:
            gib_zurück [ex, ey]
        setze tries auf tries + 1
    gib_zurück [0, 0]

funktion neustart():
    setze schlange auf [[16, 12], [15, 12], [14, 12]]
    setze richtung_x auf 1
    setze richtung_y auf 0
    setze punkte auf 0
    setze spielende auf falsch
    setze geschwindigkeit auf 150
    setze essen auf neues_essen()
    gib_zurück nichts

setze essen auf neues_essen()

# --- Haupt-Loop ---
solange fenster_offen(fenster):
    # Input — Pfeiltasten
    wenn taste_gedrückt("oben"):
        wenn richtung_y != 1:
            setze richtung_x auf 0
            setze richtung_y auf -1
    wenn taste_gedrückt("unten"):
        wenn richtung_y != -1:
            setze richtung_x auf 0
            setze richtung_y auf 1
    wenn taste_gedrückt("links"):
        wenn richtung_x != 1:
            setze richtung_x auf -1
            setze richtung_y auf 0
    wenn taste_gedrückt("rechts"):
        wenn richtung_x != -1:
            setze richtung_x auf 1
            setze richtung_y auf 0

    # Input — WASD (falls Runtime die unterstuetzt)
    wenn taste_gedrückt("w"):
        wenn richtung_y != 1:
            setze richtung_x auf 0
            setze richtung_y auf -1
    wenn taste_gedrückt("s"):
        wenn richtung_y != -1:
            setze richtung_x auf 0
            setze richtung_y auf 1
    wenn taste_gedrückt("a"):
        wenn richtung_x != 1:
            setze richtung_x auf -1
            setze richtung_y auf 0
    wenn taste_gedrückt("d"):
        wenn richtung_x != -1:
            setze richtung_x auf 1
            setze richtung_y auf 0

    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_gedrückt("leertaste"):
        wenn spielende:
            neustart()

    wenn nicht spielende:
        # --- Bewegung ---
        setze kopf auf schlange[0]
        setze neu_x auf kopf[0] + richtung_x
        setze neu_y auf kopf[1] + richtung_y

        # Wand-Kollision
        wenn neu_x < 0:
            setze spielende auf wahr
        wenn neu_x >= COLS:
            setze spielende auf wahr
        wenn neu_y < 0:
            setze spielende auf wahr
        wenn neu_y >= ROWS:
            setze spielende auf wahr

        wenn nicht spielende:
            # Selbst-Kollision
            für segment in schlange:
                wenn segment[0] == neu_x:
                    wenn segment[1] == neu_y:
                        setze spielende auf wahr

        wenn nicht spielende:
            setze neuer_kopf auf [neu_x, neu_y]

            # Futter?
            setze hat_gegessen auf falsch
            wenn neu_x == essen[0]:
                wenn neu_y == essen[1]:
                    setze hat_gegessen auf wahr
                    punkte += 1
                    setze essen auf neues_essen()
                    wenn geschwindigkeit > 50:
                        setze geschwindigkeit auf geschwindigkeit - 5

            # Neue Schlange
            setze neue_schlange auf [neuer_kopf]
            wenn hat_gegessen:
                für segment in schlange:
                    neue_schlange.hinzufügen(segment)
            sonst:
                setze i auf 0
                solange i < länge(schlange) - 1:
                    neue_schlange.hinzufügen(schlange[i])
                    i += 1
            setze schlange auf neue_schlange

    # --- Zeichnen ---
    fenster_löschen(fenster, "schwarz")

    # Schlangen-Segmente
    für segment in schlange:
        zeichne_rechteck(fenster, segment[0] * ZELLE, segment[1] * ZELLE, ZELLE - 1, ZELLE - 1, "gruen")

    # Kopf heller (nur wenn nicht game over)
    wenn nicht spielende:
        setze sk auf schlange[0]
        zeichne_rechteck(fenster, sk[0] * ZELLE + 2, sk[1] * ZELLE + 2, ZELLE - 5, ZELLE - 5, "#00FF00")

    # Futter als Kreis (rot)
    setze ex auf essen[0] * ZELLE + ZELLE / 2
    setze ey auf essen[1] * ZELLE + ZELLE / 2
    zeichne_kreis(fenster, ex, ey, ZELLE / 2 - 2, "rot")

    # Score-Balken (gelb, oben)
    setze balken auf punkte * 10
    wenn balken > BREITE - 20:
        setze balken auf BREITE - 20
    zeichne_rechteck(fenster, 10, 5, balken, 8, "gelb")

    # Rahmen
    zeichne_linie(fenster, 0, 0, BREITE - 1, 0, "grau")
    zeichne_linie(fenster, 0, HOEHE - 1, BREITE - 1, HOEHE - 1, "grau")
    zeichne_linie(fenster, 0, 0, 0, HOEHE - 1, "grau")
    zeichne_linie(fenster, BREITE - 1, 0, BREITE - 1, HOEHE - 1, "grau")

    # Game-Over-Overlay
    wenn spielende:
        # Rotes Rechteck mit schwarzem Rand (= "GAME OVER" Box)
        zeichne_rechteck(fenster, 170, 180, 300, 120, "rot")
        zeichne_rechteck(fenster, 180, 190, 280, 100, "schwarz")
        # Score als gelber Balken in der Box
        setze res auf punkte * 15
        wenn res > 260:
            setze res auf 260
        zeichne_rechteck(fenster, 190, 220, res, 20, "gelb")
        # "Leertaste zum Neustart" — als kleines weisses Rechteck unten
        zeichne_rechteck(fenster, 260, 260, 120, 15, "weiss")

    fenster_aktualisieren(fenster)
    warte(geschwindigkeit)

fenster_schliessen(fenster)
zeige f"Game Over! Punkte: {punkte}"

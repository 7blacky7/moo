# ============================================================
# Snake — Klassisches Schlangenspiel in moo
# Steuerung: Pfeiltasten, ESC zum Beenden
# ============================================================

# --- Konstanten ---
konstante BREITE auf 800
konstante HOEHE auf 600
konstante ZELLE auf 20
konstante COLS auf 40
konstante ROWS auf 30

# --- Fenster erstellen ---
setze fenster auf fenster_erstelle("moo Snake", BREITE, HOEHE)

# --- Schlange initialisieren (Liste von [x, y] Positionen) ---
setze schlange auf [[20, 15], [19, 15], [18, 15]]
setze richtung_x auf 1
setze richtung_y auf 0

# --- Essen zufaellig platzieren ---
funktion neues_essen():
    setze ex auf boden(zufall() * 40)
    setze ey auf boden(zufall() * 30)
    gib_zurück [ex, ey]

setze essen auf neues_essen()
setze punkte auf 0
setze spielende auf falsch
setze geschwindigkeit auf 120

# --- Game Loop ---
solange fenster_offen(fenster) und nicht spielende:

    # --- Input ---
    wenn taste_gedrückt("oben") und richtung_y != 1:
        setze richtung_x auf 0
        setze richtung_y auf -1
    wenn taste_gedrückt("unten") und richtung_y != -1:
        setze richtung_x auf 0
        setze richtung_y auf 1
    wenn taste_gedrückt("links") und richtung_x != 1:
        setze richtung_x auf -1
        setze richtung_y auf 0
    wenn taste_gedrückt("rechts") und richtung_x != -1:
        setze richtung_x auf 1
        setze richtung_y auf 0
    wenn taste_gedrückt("escape"):
        setze spielende auf wahr

    # --- Kopf bewegen ---
    setze kopf auf schlange[0]
    setze neu_x auf kopf[0] + richtung_x
    setze neu_y auf kopf[1] + richtung_y

    # --- Wandkollision ---
    wenn neu_x < 0 oder neu_x >= COLS oder neu_y < 0 oder neu_y >= ROWS:
        setze spielende auf wahr

    wenn nicht spielende:
        setze neuer_kopf auf [neu_x, neu_y]

        # --- Selbstkollision pruefen ---
        für segment in schlange:
            wenn segment[0] == neu_x und segment[1] == neu_y:
                setze spielende auf wahr

    wenn nicht spielende:
        # --- Essen pruefen ---
        setze hat_gegessen auf falsch
        wenn neu_x == essen[0] und neu_y == essen[1]:
            setze hat_gegessen auf wahr
            punkte += 1
            setze essen auf neues_essen()
            # Schneller werden (min 40ms)
            wenn geschwindigkeit > 40:
                setze geschwindigkeit auf geschwindigkeit - 5

        # --- Neue Schlange aufbauen (Kopf vorne, Ende weg wenn nicht gegessen) ---
        setze neue_schlange auf [neuer_kopf]
        wenn hat_gegessen:
            für segment in schlange:
                neue_schlange.hinzufügen(segment)
        sonst:
            # Alle ausser letztes Segment
            setze i auf 0
            solange i < länge(schlange) - 1:
                neue_schlange.hinzufügen(schlange[i])
                i += 1
        setze schlange auf neue_schlange

        # --- Zeichnen ---
        fenster_löschen(fenster, "schwarz")

        # Schlange zeichnen (gruen)
        für segment in schlange:
            zeichne_rechteck(fenster, segment[0] * ZELLE, segment[1] * ZELLE, ZELLE - 1, ZELLE - 1, "gruen")

        # Kopf heller
        setze sk auf schlange[0]
        zeichne_rechteck(fenster, sk[0] * ZELLE + 2, sk[1] * ZELLE + 2, ZELLE - 5, ZELLE - 5, "#00FF00")

        # Essen zeichnen (rot)
        setze ex auf essen[0] * ZELLE + ZELLE / 2
        setze ey auf essen[1] * ZELLE + ZELLE / 2
        zeichne_kreis(fenster, ex, ey, ZELLE / 2 - 2, "rot")

        # Punkte-Balken (Laenge = Punkte * 10, max 400)
        setze balken_breite auf punkte * 10
        wenn balken_breite > 400:
            setze balken_breite auf 400
        zeichne_rechteck(fenster, 10, 5, balken_breite, 10, "gelb")

        # Rahmen
        zeichne_linie(fenster, 0, 0, BREITE - 1, 0, "grau")
        zeichne_linie(fenster, 0, HOEHE - 1, BREITE - 1, HOEHE - 1, "grau")
        zeichne_linie(fenster, 0, 0, 0, HOEHE - 1, "grau")
        zeichne_linie(fenster, BREITE - 1, 0, BREITE - 1, HOEHE - 1, "grau")

        fenster_aktualisieren(fenster)
        warte(geschwindigkeit)

# --- Game Over Bildschirm ---
fenster_löschen(fenster, "schwarz")
zeichne_rechteck(fenster, 250, 250, 300, 60, "rot")
zeichne_rechteck(fenster, 255, 255, 290, 50, "schwarz")

# Punkte-Balken als Ergebnis
setze ergebnis_breite auf punkte * 15
wenn ergebnis_breite > 280:
    setze ergebnis_breite auf 280
zeichne_rechteck(fenster, 260, 265, ergebnis_breite, 30, "gelb")

fenster_aktualisieren(fenster)

# 3 Sekunden warten, dann schliessen
warte(3000)
fenster_schliessen(fenster)
zeige f"Game Over! Punkte: {punkte}"

# Partikel-Simulation — Feuerwerk in moo
# Klick erzeugt Explosion an Mausposition

setze fenster auf fenster_erstelle("Moo Partikel", 800, 600)

# Partikel-Liste
setze partikel auf []

# Farben fuer Explosionen
setze farben auf ["rot", "gelb", "orange", "weiss", "rot", "gelb"]

# Explosion erzeugen: n Partikel an Position (ex, ey)
funktion explosion(ex, ey, n):
    setze i auf 0
    solange i < n:
        # Zufaellige Richtung (Kreis)
        setze winkel auf zufall() * 6.28
        setze speed auf zufall() * 6.0 + 2.0
        setze vx auf speed * (zufall() - 0.5) * 2.0
        setze vy auf speed * (zufall() - 0.5) * 2.0 - 3.0
        setze farb_idx auf runde(zufall() * 5.0)
        setze f auf farben[farb_idx]
        setze p auf {"x": ex, "y": ey, "vx": vx, "vy": vy, "farbe": f, "leben": 80.0 + zufall() * 40.0}
        partikel.hinzufügen(p)
        setze i auf i + 1
    gib_zurück nichts

# Start-Explosion in der Mitte
explosion(400.0, 300.0, 100)

# Maus-Tracking
setze maus_war_gedrückt auf falsch

# Spielschleife
solange fenster_offen(fenster):
    # --- Input: Mausklick ---
    setze maus_jetzt auf maus_gedrückt(fenster)
    wenn maus_jetzt:
        wenn nicht maus_war_gedrückt:
            setze mx auf maus_x(fenster)
            setze my auf maus_y(fenster)
            explosion(mx, my, 80)
    setze maus_war_gedrückt auf maus_jetzt

    # Escape zum Beenden
    wenn taste_gedrückt("escape"):
        stopp

    # --- Physik: Partikel aktualisieren ---
    setze neue_partikel auf []
    für p in partikel:
        # Gravitation
        setze ny auf p["vy"] + 0.12
        setze nx auf p["x"] + p["vx"]
        setze npy auf p["y"] + ny
        setze nl auf p["leben"] - 1.0

        wenn nl > 0.0:
            setze np auf {"x": nx, "y": npy, "vx": p["vx"] * 0.99, "vy": ny, "farbe": p["farbe"], "leben": nl}
            neue_partikel.hinzufügen(np)

    setze partikel auf neue_partikel

    # --- Zeichnen ---
    fenster_löschen(fenster, "schwarz")

    für p in partikel:
        setze px auf runde(p["x"])
        setze py auf runde(p["y"])
        # Nur zeichnen wenn im Fenster
        wenn px >= 0:
            wenn px < 800:
                wenn py >= 0:
                    wenn py < 600:
                        zeichne_rechteck(fenster, px, py, 2, 2, p["farbe"])

    fenster_aktualisieren(fenster)
    warte(16)

fenster_schliessen(fenster)

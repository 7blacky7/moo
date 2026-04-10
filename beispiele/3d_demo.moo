# 3D-Demo — Rotierende Wuerfel in moo
# Steuerung: Pfeiltasten oben/unten fuer Kamera-Zoom, Escape zum Beenden

# Fenster + 3D-Kontext erstellen
setze win auf raum_erstelle("moo 3D Demo", 800, 600)
raum_perspektive(win, 45.0, 0.1, 100.0)
raum_kamera(win, 0, 2, 12, 0, 0, 0)

setze winkel auf 0.0
setze kamera_z auf 12.0

solange raum_offen(win):
    # Kamera-Steuerung
    wenn taste_gedrückt("oben"):
        setze kamera_z auf kamera_z - 0.1
    wenn taste_gedrückt("unten"):
        setze kamera_z auf kamera_z + 0.1
    wenn taste_gedrückt("escape"):
        stopp

    # Kamera-Grenzen
    wenn kamera_z < 4.0:
        setze kamera_z auf 4.0
    wenn kamera_z > 25.0:
        setze kamera_z auf 25.0

    raum_kamera(win, 0, 2, kamera_z, 0, 0, 0)

    # Szene zeichnen
    raum_löschen(win, 0.05, 0.05, 0.15)

    # Haupt-Wuerfel (rot, rotiert um Diagonale)
    raum_push(win)
    raum_rotiere(win, winkel, 1.0, 1.0, 0.0)
    raum_würfel(win, 0, 0, 0, 0.3, "rot")
    raum_pop(win)

    # Kleiner Wuerfel links (gruen, gegenlaeufig)
    raum_push(win)
    raum_verschiebe(win, -2.5, 0, 0)
    raum_rotiere(win, 0 - winkel * 1.5, 0.0, 1.0, 1.0)
    raum_würfel(win, 0, 0, 0, 0.2, "gruen")
    raum_pop(win)

    # Kleiner Wuerfel rechts (blau, schnell)
    raum_push(win)
    raum_verschiebe(win, 2.5, 0, 0)
    raum_rotiere(win, winkel * 2.0, 1.0, 0.0, 1.0)
    raum_würfel(win, 0, 0, 0, 0.2, "blau")
    raum_pop(win)

    # Boden (grau, flach, statisch)
    raum_push(win)
    raum_verschiebe(win, 0, -2.0, 0)
    raum_würfel(win, 0, 0, 0, 1.0, "grau")
    raum_pop(win)

    raum_aktualisieren(win)
    setze winkel auf winkel + 1.0
    warte(16)

raum_schliessen(win)

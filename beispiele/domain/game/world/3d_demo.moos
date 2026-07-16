# 3D-Demo — Rotierende Wuerfel in moo
# Steuerung: Pfeiltasten oben/unten fuer Kamera-Zoom, Escape zum Beenden

setze win auf raum_erstelle("moo 3D Demo", 800, 600)
raum_perspektive(win, 45.0, 0.1, 100.0)

setze winkel auf 0.0
setze kamera_z auf 8.0

solange raum_offen(win):
    # Kamera-Steuerung
    wenn raum_taste(win,"oben"):
        setze kamera_z auf kamera_z - 0.2
    wenn raum_taste(win,"unten"):
        setze kamera_z auf kamera_z + 0.2
    wenn raum_taste(win,"escape"):
        stopp

    wenn kamera_z < 3.0:
        setze kamera_z auf 3.0
    wenn kamera_z > 20.0:
        setze kamera_z auf 20.0

    # ERST loeschen, DANN Kamera setzen!
    raum_löschen(win, 0.05, 0.05, 0.15)
    raum_kamera(win, 0, 3, kamera_z, 0, 0, 0)

    # Haupt-Wuerfel (rot, rotiert)
    raum_push(win)
    raum_rotiere(win, winkel, 1.0, 1.0, 0.0)
    raum_würfel(win, 0, 0, 0, 1.0, "rot")
    raum_pop(win)

    # Wuerfel links (gruen)
    raum_push(win)
    raum_verschiebe(win, -3.0, 0, 0)
    raum_rotiere(win, 0 - winkel * 1.5, 0.0, 1.0, 1.0)
    raum_würfel(win, 0, 0, 0, 0.7, "gruen")
    raum_pop(win)

    # Wuerfel rechts (blau)
    raum_push(win)
    raum_verschiebe(win, 3.0, 0, 0)
    raum_rotiere(win, winkel * 2.0, 1.0, 0.0, 1.0)
    raum_würfel(win, 0, 0, 0, 0.7, "blau")
    raum_pop(win)

    # Boden (flach)
    raum_push(win)
    raum_verschiebe(win, 0, -1.5, 0)
    raum_würfel(win, 0, 0, 0, 0.1, "grau")
    raum_pop(win)

    raum_aktualisieren(win)
    setze winkel auf winkel + 1.0
    warte(16)

raum_schliessen(win)

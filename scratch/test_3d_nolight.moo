# Test ohne Lighting — nur Farbe
setze win auf raum_erstelle("Test NoLight", 800, 600)
raum_perspektive(win, 60.0, 0.1, 100.0)
raum_löschen(win, 0.0, 0.0, 0.0)
raum_kamera(win, 0.0, 2.0, 5.0, 0.0, 0.0, 0.0)
raum_würfel(win, 0.0, 0.0, 0.0, 2.0, "rot")
raum_aktualisieren(win)
warte(3000)
raum_schliessen(win)

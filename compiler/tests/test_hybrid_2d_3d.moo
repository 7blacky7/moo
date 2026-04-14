# Hybrid 2D+3D Compile-Smoke (M5.2 k3)
# Compile-only — kein .expected, weil GUI/GL-Context. Das Skript pinnt
# die neuen Aliase aus M5.2 (fenster_unified + zeichne_*_z + sprite_*_z
# + hybrid_* helpers) damit sie nicht versehentlich aus codegen.rs
# fliegen. Live-Run braucht DISPLAY + GL3.3.

setze win auf fenster_unified("Hybrid Smoke", 320, 240)
setze offen auf hybrid_offen(win)
hybrid_löschen(win, 0.1, 0.1, 0.15)

# 2D-Quad bei Z=0.3 (nahe Kamera)
zeichne_rechteck_z(win, 50, 80, 0.3, 200, 80, "rot")
zeichne_kreis_z(win, 160, 120, 0.4, 30, "weiss")
zeichne_linie_z(win, 0, 0, 320, 240, 0.2, "gelb")

# 3D-Primitive im selben Fenster (gemeinsamer Z-Buffer)
raum_kamera(win, 0.0, 1.5, 3.0, 0.0, 0.0, 0.0)
raum_würfel(win, 0.0, 0.0, 0.5, 0.5, "blau")

# Sprite Z (id Stub — Live-Run benoetigt sprite_lade vorher)
# sprite_zeichnen_z(win, 0, 100, 100, 0.6, 32, 32)

hybrid_aktualisieren(win)
hybrid_schliessen(win)

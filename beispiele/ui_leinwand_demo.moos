# ============================================================
# ui_leinwand_demo.moo — Custom-Draw-Leinwand Smoke-Demo
#
# Zeigt die Zeichner-API aus Phase 5 (cairo/GDI/CoreGraphics).
# Zeichnet Rechtecke, Kreise, Linien und Text auf eine 380x280-
# Leinwand innerhalb eines 400x320-Fensters.
#
# Kompilieren:
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       compile beispiele/ui_leinwand_demo.moo
# Starten:
#   ./beispiele/ui_leinwand_demo
# ============================================================

importiere ui

setze g auf {}

funktion auf_zeichne(lw, z):
    # Hintergrund: dunkelgrau
    ui_zeichne_farbe(z, 40, 40, 48, 255)
    ui_zeichne_rechteck(z, 0, 0, 400, 300, wahr)

    # Blaues Rechteck (gefuellt)
    ui_zeichne_farbe(z, 40, 120, 220, 255)
    ui_zeichne_rechteck(z, 20, 20, 180, 100, wahr)

    # Roter Rechteck-Rahmen
    ui_zeichne_farbe(z, 220, 60, 60, 255)
    ui_zeichne_rechteck(z, 220, 20, 140, 100, falsch)

    # Gelber Kreis (gefuellt)
    ui_zeichne_farbe(z, 240, 220, 40, 255)
    ui_zeichne_kreis(z, 110, 190, 45, wahr)

    # Gruener Kreis-Rand
    ui_zeichne_farbe(z, 60, 200, 100, 255)
    ui_zeichne_kreis(z, 260, 190, 45, falsch)

    # Zwei Linien ueber Kreuz
    ui_zeichne_farbe(z, 255, 255, 255, 200)
    ui_zeichne_linie(z, 20, 140, 380, 140, 2)
    ui_zeichne_linie(z, 200, 20, 200, 260, 1)

    # Weisser Text unten
    ui_zeichne_farbe(z, 255, 255, 255, 255)
    ui_zeichne_text(z, 20, 252, "Hallo Leinwand!", 18)


setze g["hFenster"] auf ui_fenster("Leinwand-Demo", 400, 320, 0, nichts)
setze g["canvas"]   auf ui_leinwand(g["hFenster"], 10, 10, 380, 280, auf_zeichne)
ui_zeige(g["hFenster"])
ui_laufen()

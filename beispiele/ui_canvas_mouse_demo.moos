# ============================================================
# ui_canvas_mouse_demo.moo — Rechteck-per-Maus-ziehen Demo
#
# Demonstriert die P5-Canvas-Event-API:
#   ui_leinwand_on_maus(canvas, callback)       — Mausklick
#   ui_leinwand_on_bewegung(canvas, callback)   — Mausbewegung
#   ui_zeichne_text_breite / ui_leinwand_zentriert_text
#
# Interaktion:
#   Linksklick (Taste 1): Drag starten — oder — Rechteck ablegen
#   Mausbewegung:         Waehrend Drag Live-Vorschau (gelber Rahmen)
#   Rechtsklick (Taste 3): Alle Rechtecke loeschen + Drag abbrechen
#
# Hinweis Zwei-Klick-Muster:
#   Da on_maus nur auf MouseDown feuert (kein MouseUp-Event in
#   Phase 1), nutzt diese Demo das Zwei-Klick-Muster:
#     1. Linksklick  = Startpunkt merken, Drag beginnt
#     2. Linksklick  = Rechteck commit, Drag endet
#   Sobald MouseUp verfuegbar ist, kann auf klassisches
#   Press/Release umgestellt werden.
#
# Kompilieren:
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       compile beispiele/ui_canvas_mouse_demo.moo
# Starten:
#   ./beispiele/ui_canvas_mouse_demo
# Linux/Xvfb-Test:
#   Xvfb :99 -screen 0 1024x768x24 &
#   DISPLAY=:99 timeout 5 ./beispiele/ui_canvas_mouse_demo
# ============================================================

importiere ui

# Globaler Anwendungs-State
setze g auf {}

# on_zeichne-Callback: zeichnet Hintergrund, gespeicherte Rechtecke,
# Live-Drag-Vorschau und Hilfetext unten.
funktion auf_zeichne(lw, z):
    # Hintergrund: dunkles Anthrazit
    ui_zeichne_farbe(z, 30, 32, 38, 255)
    ui_zeichne_rechteck(z, 0, 0, 580, 400, wahr)

    # Gitternetz (dezent, 10 Spalten x 10 Zeilen fuer Orientierung)
    ui_zeichne_farbe(z, 50, 52, 60, 255)
    ui_zeichne_linie(z, 58, 0, 58, 400, 1)
    ui_zeichne_linie(z, 116, 0, 116, 400, 1)
    ui_zeichne_linie(z, 174, 0, 174, 400, 1)
    ui_zeichne_linie(z, 232, 0, 232, 400, 1)
    ui_zeichne_linie(z, 290, 0, 290, 400, 1)
    ui_zeichne_linie(z, 348, 0, 348, 400, 1)
    ui_zeichne_linie(z, 406, 0, 406, 400, 1)
    ui_zeichne_linie(z, 464, 0, 464, 400, 1)
    ui_zeichne_linie(z, 522, 0, 522, 400, 1)
    ui_zeichne_linie(z, 0, 40, 580, 40, 1)
    ui_zeichne_linie(z, 0, 80, 580, 80, 1)
    ui_zeichne_linie(z, 0, 120, 580, 120, 1)
    ui_zeichne_linie(z, 0, 160, 580, 160, 1)
    ui_zeichne_linie(z, 0, 200, 580, 200, 1)
    ui_zeichne_linie(z, 0, 240, 580, 240, 1)
    ui_zeichne_linie(z, 0, 280, 580, 280, 1)
    ui_zeichne_linie(z, 0, 320, 580, 320, 1)
    ui_zeichne_linie(z, 0, 360, 580, 360, 1)

    # Gespeicherte Rechtecke: blau gefuellt + heller Rahmen
    für r in g["rechtecke"]:
        ui_zeichne_farbe(z, 60, 120, 220, 160)
        ui_zeichne_rechteck(z, r[0], r[1], r[2], r[3], wahr)
        ui_zeichne_farbe(z, 130, 180, 255, 255)
        ui_zeichne_rechteck(z, r[0], r[1], r[2], r[3], falsch)

    # Live-Drag-Vorschau: gelber Rahmen + Masszahl
    wenn g["drag_aktiv"]:
        setze dr auf ui_leinwand_drag_rechteck(g)
        ui_zeichne_farbe(z, 255, 210, 50, 220)
        ui_zeichne_rechteck(z, dr[0], dr[1], dr[2], dr[3], falsch)
        ui_zeichne_farbe(z, 255, 240, 100, 200)
        ui_zeichne_rechteck(z, dr[0], dr[1], dr[2], dr[3], wahr)
        # Masszahl: Breite x Hoehe im Rechteck anzeigen
        setze mass_text auf "" + dr[2] + " x " + dr[3] + " px"
        ui_zeichne_farbe(z, 255, 255, 200, 255)
        ui_zeichne_text(z, dr[0] + 4, dr[1] + 15, mass_text, 11)

    # Zentrierter Hilfetext: unten auf der Leinwand
    ui_zeichne_farbe(z, 160, 165, 180, 255)
    ui_leinwand_zentriert_text(z, 290, 384, "1. Klick: Drag-Start | 2. Klick: Ablegen | Rechtsklick: Loeschen", 11)

    gib_zurück wahr


# on_maus-Callback: Signatur (leinwand, x, y, taste)
# taste 1=links, 2=mitte, 3=rechts
funktion auf_maus(lw, x, y, taste):
    # Linksklick: Drag umschalten
    wenn taste == 1:
        wenn g["drag_aktiv"]:
            # Drag beenden: Rechteck nur speichern wenn nicht zu klein
            setze dr auf ui_leinwand_drag_rechteck(g)
            wenn dr[2] > 4:
                wenn dr[3] > 4:
                    g["rechtecke"].hinzufügen(dr)
            g["drag_aktiv"] = falsch
        sonst:
            # Drag starten: Startpunkt merken
            g["drag_aktiv"] = wahr
            g["drag_x0"] = x
            g["drag_y0"] = y
            g["drag_x1"] = x
            g["drag_y1"] = y
    # Rechtsklick: alles loeschen + Drag abbrechen
    sonst wenn taste == 3:
        g["rechtecke"] = []
        g["drag_aktiv"] = falsch

    # Statusleiste aktualisieren und Leinwand neu zeichnen
    ui_label_setze(g["status"], ui_leinwand_status_text(x, y, taste))
    ui_leinwand_anfordern(lw)
    gib_zurück wahr


# on_bewegung-Callback: Signatur (leinwand, x, y)
funktion auf_bewegung(lw, x, y):
    g["maus_x"] = x
    g["maus_y"] = y
    # Statusleiste mit aktuellen Koordinaten aktualisieren
    ui_label_setze(g["status"], "x=" + x + "  y=" + y)
    # Nur neu zeichnen wenn Drag aktiv (sonst zu viele Repaints)
    wenn g["drag_aktiv"]:
        g["drag_x1"] = x
        g["drag_y1"] = y
        ui_leinwand_anfordern(lw)
    gib_zurück wahr


# ---- Fenster und Widgets aufbauen ----

g["hFenster"] = ui_fenster("Canvas-Maus-Demo — Rechteck ziehen", 600, 460, 0, nichts)

# Hauptleinwand
g["canvas"] = ui_leinwand(g["hFenster"], 10, 10, 580, 400, auf_zeichne)

# Statusleiste darunter
g["status"] = ui_label(g["hFenster"], "Bewege die Maus ueber die Leinwand ...", 10, 418, 580, 22)

# Drag-State + Rechteck-Liste initialisieren
ui_leinwand_drag_init(g)
g["maus_x"] = 0
g["maus_y"] = 0
g["rechtecke"] = []

# Mouse-Callbacks binden (je nur einer pro Event; Re-Bind loest alten)
ui_leinwand_on_maus(g["canvas"], auf_maus)
ui_leinwand_on_bewegung(g["canvas"], auf_bewegung)

# Fenster zeigen und Event-Loop starten
ui_zeige(g["hFenster"])
ui_laufen()

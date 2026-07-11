# ============================================================
# beispiele/tests/ui_uimoo1_smoke.moo — Smoke: UIMOO-1 Primitive
#
# Prueft Codegen + Linking + Grundverhalten der neuen Welle:
#   ui_zeichne_rechteck_rund, ui_zeichne_clip_setze/loesche,
#   ui_leinwand_on_maus_los / on_rad / on_taste / on_fokus,
#   ui_leinwand_fokus_setze, ui_fenster_cursor_setze.
# Headless-Lauf: frame_001-Snapshot, dann Ende ueber Timer.
# ============================================================

importiere ui

setze g auf {}
setze g["ereignisse"] auf []

funktion auf_zeichne(lw, z):
    ui_zeichne_farbe(z, 30, 32, 38, 255)
    ui_zeichne_rechteck(z, 0, 0, 580, 400, wahr)

    # Rounded-Rect: gefuellt + Outline + radius-Kappung (radius > min/2)
    ui_zeichne_farbe(z, 40, 120, 220, 255)
    ui_zeichne_rechteck_rund(z, 20, 20, 180, 100, 14, wahr)
    ui_zeichne_farbe(z, 220, 60, 60, 255)
    ui_zeichne_rechteck_rund(z, 220, 20, 140, 100, 300, falsch)

    # Clip: gelber Kreis wird am Rechteck 20..200 x 140..260 beschnitten
    ui_zeichne_clip_setze(z, 20, 140, 180, 120)
    ui_zeichne_farbe(z, 240, 220, 40, 255)
    ui_zeichne_kreis(z, 200, 200, 80, wahr)
    ui_zeichne_clip_loesche(z)

    # Doppel-Loesche muss falsch liefern (keine Ebene offen), kein Crash
    setze doppelt auf ui_zeichne_clip_loesche(z)
    wenn doppelt:
        zeige "FEHLER: clip_loesche ohne offene Ebene lieferte wahr"

    # Absichtlich EINE Ebene offen lassen → Trampoline muss abraeumen
    ui_zeichne_clip_setze(z, 0, 0, 50, 50)

    ui_zeichne_farbe(z, 255, 255, 255, 255)
    ui_zeichne_text(z, 20, 300, "UIMOO-1 Smoke", 14)
    gib_zurück wahr

funktion auf_maus_los(lw, x, y, taste):
    g["ereignisse"].hinzufügen("los:" + text(x) + "," + text(y) + "," + text(taste))
    gib_zurück wahr

funktion auf_rad(lw, x, y, delta):
    g["ereignisse"].hinzufügen("rad:" + text(delta))
    gib_zurück wahr

funktion auf_taste(lw, taste, gedrueckt, mod):
    g["ereignisse"].hinzufügen("taste:" + taste + ":" + text(gedrueckt) + ":" + text(mod))
    gib_zurück falsch

funktion auf_fokus(lw, hat_fokus):
    g["ereignisse"].hinzufügen("fokus:" + text(hat_fokus))
    gib_zurück wahr

funktion beende():
    zeige "UIMOO1-EVENTS: " + text(länge(g["ereignisse"]))
    zeige "UIMOO1-SMOKE-OK"
    ui_beenden()
    gib_zurück wahr

setze g["hFenster"] auf ui_fenster("UIMOO1-Smoke", 600, 460, 0, nichts)
setze g["canvas"]   auf ui_leinwand(g["hFenster"], 10, 10, 580, 400, auf_zeichne)

ui_leinwand_on_maus_los(g["canvas"], auf_maus_los)
ui_leinwand_on_rad(g["canvas"], auf_rad)
ui_leinwand_on_taste(g["canvas"], auf_taste)
ui_leinwand_on_fokus(g["canvas"], auf_fokus)
ui_leinwand_fokus_setze(g["canvas"])
ui_fenster_cursor_setze(g["hFenster"], "hand")

ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(600, beende)
ui_laufen()

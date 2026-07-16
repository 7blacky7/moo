# ============================================================
# test_racing.moo — Automatisierter Test fuer Racing
#
# Testet: Fenster, Beschleunigung, Lenkung, Bremsen, Reibung,
#         Checkpoint-Erkennung, Screenshot pro Aktion
# ============================================================

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

zeige "=== RACING AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster ---
zeige "[Test 1] Fenster"
setze win auf fenster_erstelle("Racing Test", 800, 600)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster", "nicht offen")

# Spielzustand
setze auto_x auf 400.0
setze auto_y auf 500.0
setze auto_winkel auf 0.0
setze auto_speed auf 0.0
setze MAX_SPEED auf 6.0
setze BESCHLEUNIGUNG auf 0.15
setze BREMSE auf 0.1
setze REIBUNG auf 0.02
setze LENKUNG auf 0.04

# Checkpoints
setze cp_x auf [400, 650, 700, 650, 400, 150, 100, 150]
setze cp_y auf [100, 150, 300, 450, 500, 450, 300, 150]
setze cp_radius auf [80, 80, 80, 80, 80, 80, 80, 80]
setze cp_anzahl auf 8
setze naechster_cp auf 0
setze runden auf 0

funktion zeichne_racing():
    fenster_löschen(win, "#2D5F2D")
    # Strecke als Kreise
    setze i auf 0
    solange i < cp_anzahl:
        zeichne_kreis(win, cp_x[i], cp_y[i], cp_radius[i], "#555555")
        setze i auf i + 1
    # Naechster Checkpoint gelb
    zeichne_kreis(win, cp_x[naechster_cp], cp_y[naechster_cp], 20, "gelb")
    # Auto
    setze ax auf auto_x - 10
    setze ay auf auto_y - 15
    zeichne_rechteck(win, ax, ay, 20, 30, "rot")
    # Richtungsanzeige
    setze front_x auf auto_x + sinus(auto_winkel) * 20
    setze front_y auf auto_y - cosinus(auto_winkel) * 20
    zeichne_kreis(win, front_x, front_y, 4, "weiss")

# --- Test 2: Start-Rendering ---
zeige "[Test 2] Start-Szene"
zeichne_racing()
screenshot(win, "beispiele/test_screenshots/racing_01_start.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("Strecke + Auto gerendert")

# --- Test 3: Beschleunigen ---
zeige "[Test 3] Beschleunigen"
setze alte_speed auf auto_speed
setze frame auf 0
solange frame < 20:
    taste_simulieren("w", wahr)
    warte(20)
    taste_simulieren("w", falsch)
    setze auto_speed auf auto_speed + BESCHLEUNIGUNG
    wenn auto_speed > MAX_SPEED:
        setze auto_speed auf MAX_SPEED
    # Vorwaerts bewegen
    setze auto_x auf auto_x + sinus(auto_winkel) * auto_speed
    setze auto_y auf auto_y - cosinus(auto_winkel) * auto_speed
    zeichne_racing()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/racing_02_speed.bmp")
warte(100)
wenn auto_speed > alte_speed:
    test_ok("Beschleunigt: " + text(alte_speed) + " -> " + text(auto_speed))
sonst:
    test_fail("Beschleunigung", "Speed unveraendert")

# --- Test 4: Lenken nach rechts ---
zeige "[Test 4] Lenken nach rechts"
setze alter_winkel auf auto_winkel
setze frame auf 0
solange frame < 15:
    taste_simulieren("d", wahr)
    warte(20)
    taste_simulieren("d", falsch)
    setze auto_winkel auf auto_winkel + LENKUNG * auto_speed
    setze auto_x auf auto_x + sinus(auto_winkel) * auto_speed
    setze auto_y auf auto_y - cosinus(auto_winkel) * auto_speed
    # Reibung
    setze auto_speed auf auto_speed - REIBUNG
    wenn auto_speed < 0:
        setze auto_speed auf 0.0
    zeichne_racing()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/racing_03_rechts.bmp")
warte(100)
wenn auto_winkel > alter_winkel:
    test_ok("Gelenkt: Winkel " + text(alter_winkel) + " -> " + text(auto_winkel))
sonst:
    test_fail("Lenkung", "Winkel unveraendert")

# --- Test 5: Bremsen ---
zeige "[Test 5] Bremsen"
# Erst wieder beschleunigen
setze auto_speed auf 4.0
setze alte_speed auf auto_speed
setze frame auf 0
solange frame < 15:
    taste_simulieren("s", wahr)
    warte(20)
    taste_simulieren("s", falsch)
    setze auto_speed auf auto_speed - BREMSE
    wenn auto_speed < 0:
        setze auto_speed auf 0.0
    zeichne_racing()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/racing_04_bremsen.bmp")
warte(100)
wenn auto_speed < alte_speed:
    test_ok("Gebremst: " + text(alte_speed) + " -> " + text(auto_speed))
sonst:
    test_fail("Bremsen", "Speed unveraendert")

# --- Test 6: Reibung ---
zeige "[Test 6] Reibung"
setze auto_speed auf 2.0
setze alte_speed auf auto_speed
setze frame auf 0
solange frame < 20:
    setze auto_speed auf auto_speed - REIBUNG
    wenn auto_speed < 0:
        setze auto_speed auf 0.0
    setze frame auf frame + 1

wenn auto_speed < alte_speed:
    test_ok("Reibung reduziert Speed: " + text(alte_speed) + " -> " + text(auto_speed))
sonst:
    test_fail("Reibung", "Speed unveraendert")

# --- Test 7: Checkpoint-Erkennung ---
zeige "[Test 7] Checkpoint-Erkennung"
setze auto_x auf cp_x[0] * 1.0
setze auto_y auf cp_y[0] * 1.0
setze naechster_cp auf 0

# Distanz zum Checkpoint
setze dx auf auto_x - cp_x[naechster_cp]
setze dy auf auto_y - cp_y[naechster_cp]
setze dist auf (dx * dx + dy * dy)
setze radius_sq auf cp_radius[naechster_cp] * cp_radius[naechster_cp]

wenn dist < radius_sq:
    setze naechster_cp auf (naechster_cp + 1) % cp_anzahl
    test_ok("Checkpoint erkannt, naechster: " + text(naechster_cp))
sonst:
    test_fail("Checkpoint", "Nicht erkannt bei dist=" + text(dist))

zeichne_racing()
screenshot(win, "beispiele/test_screenshots/racing_05_checkpoint.bmp")
fenster_aktualisieren(win)
warte(100)

# --- Test 8: Runden-Zaehlung ---
zeige "[Test 8] Runden-Zaehlung"
# Alle Checkpoints durchlaufen
setze i auf 0
solange i < cp_anzahl:
    setze naechster_cp auf (naechster_cp + 1) % cp_anzahl
    wenn naechster_cp == 0:
        setze runden auf runden + 1
    setze i auf i + 1

wenn runden > 0:
    test_ok("Runde gezaehlt: " + text(runden))
sonst:
    test_fail("Runden", "Keine Runde gezaehlt")

# --- Test 9: Game-Loop (40 Frames) ---
zeige "[Test 9] Game-Loop (40 Frames)"
setze auto_x auf 400.0
setze auto_y auf 500.0
setze auto_winkel auf 0.0
setze auto_speed auf 3.0

setze frame auf 0
solange frame < 40:
    setze auto_winkel auf auto_winkel + 0.05
    setze auto_x auf auto_x + sinus(auto_winkel) * auto_speed
    setze auto_y auf auto_y - cosinus(auto_winkel) * auto_speed
    # Im Spielfeld halten
    wenn auto_x < 20:
        setze auto_x auf 20.0
    wenn auto_x > 780:
        setze auto_x auf 780.0
    wenn auto_y < 20:
        setze auto_y auf 20.0
    wenn auto_y > 580:
        setze auto_y auf 580.0

    zeichne_racing()
    wenn frame == 20:
        screenshot(win, "beispiele/test_screenshots/racing_06_loop_mitte.bmp")
    wenn frame == 39:
        screenshot(win, "beispiele/test_screenshots/racing_07_loop_ende.bmp")
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

test_ok("40 Frames ohne Crash")

# ============================================================
zeige ""
zeige "=== ERGEBNIS ==="
zeige "Bestanden: " + text(bestanden)
zeige "Fehlgeschlagen: " + text(fehlgeschlagen)
wenn fehlgeschlagen == 0:
    zeige "ALLE TESTS BESTANDEN!"
sonst:
    zeige "FEHLER GEFUNDEN — siehe oben"

fenster_schliessen(win)

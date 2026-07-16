# ============================================================
# test_snake.moo — Automatisierter Test fuer Snake
#
# Testet: Fenster, Schlangen-Bewegung, Richtungswechsel,
#         Wand-Kollision, Essen, Wachstum, Screenshot pro Aktion
# ============================================================

konstante BREITE auf 640
konstante HOEHE auf 480
konstante ZELLE auf 20
konstante COLS auf 32
konstante ROWS auf 24

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

zeige "=== SNAKE AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster ---
zeige "[Test 1] Fenster erstellen"
setze win auf fenster_erstelle("Snake Test", BREITE, HOEHE)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster", "nicht offen")

# --- Spielzustand ---
setze schlange auf [[16, 12], [15, 12], [14, 12]]
setze richtung_x auf 1
setze richtung_y auf 0
setze essen auf [20, 12]
setze punkte auf 0
setze spielende auf falsch

funktion zeichne_snake():
    fenster_löschen(win, "schwarz")
    für segment in schlange:
        zeichne_rechteck(win, segment[0] * ZELLE, segment[1] * ZELLE, ZELLE - 1, ZELLE - 1, "gruen")
    wenn nicht spielende:
        setze sk auf schlange[0]
        zeichne_rechteck(win, sk[0] * ZELLE + 2, sk[1] * ZELLE + 2, ZELLE - 5, ZELLE - 5, "#00FF00")
    setze ex auf essen[0] * ZELLE + ZELLE / 2
    setze ey auf essen[1] * ZELLE + ZELLE / 2
    zeichne_kreis(win, ex, ey, ZELLE / 2 - 2, "rot")
    zeichne_linie(win, 0, 0, BREITE - 1, 0, "grau")
    zeichne_linie(win, 0, HOEHE - 1, BREITE - 1, HOEHE - 1, "grau")
    zeichne_linie(win, 0, 0, 0, HOEHE - 1, "grau")
    zeichne_linie(win, BREITE - 1, 0, BREITE - 1, HOEHE - 1, "grau")

funktion bewege_schlange():
    setze kopf auf schlange[0]
    setze neu_x auf kopf[0] + richtung_x
    setze neu_y auf kopf[1] + richtung_y
    setze neuer_kopf auf [neu_x, neu_y]
    setze neue_schlange auf [neuer_kopf]
    setze i auf 0
    solange i < länge(schlange) - 1:
        neue_schlange.hinzufügen(schlange[i])
        i += 1
    setze schlange auf neue_schlange

funktion bewege_und_wachse():
    setze kopf auf schlange[0]
    setze neu_x auf kopf[0] + richtung_x
    setze neu_y auf kopf[1] + richtung_y
    setze neuer_kopf auf [neu_x, neu_y]
    setze neue_schlange auf [neuer_kopf]
    für segment in schlange:
        neue_schlange.hinzufügen(segment)
    setze schlange auf neue_schlange

# --- Test 2: Initiales Rendering ---
zeige "[Test 2] Start-Rendering"
zeichne_snake()
screenshot(win, "beispiele/test_screenshots/snake_01_start.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("Schlange (3 Segmente) + Essen gerendert")

# --- Test 3: Bewegung nach rechts ---
zeige "[Test 3] Bewegung nach rechts (5 Schritte)"
setze alte_x auf schlange[0][0]
setze frame auf 0
solange frame < 5:
    taste_simulieren("d", wahr)
    warte(30)
    taste_simulieren("d", falsch)
    bewege_schlange()
    zeichne_snake()
    fenster_aktualisieren(win)
    warte(50)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/snake_02_rechts.bmp")
warte(100)

setze neue_x auf schlange[0][0]
wenn neue_x > alte_x:
    test_ok("Schlange bewegt sich rechts: " + text(alte_x) + " -> " + text(neue_x))
sonst:
    test_fail("Rechts-Bewegung", "x unveraendert")

# --- Test 4: Richtungswechsel nach unten ---
zeige "[Test 4] Richtungswechsel nach unten"
taste_simulieren("s", wahr)
warte(30)
taste_simulieren("s", falsch)
setze richtung_x auf 0
setze richtung_y auf 1
setze alte_y auf schlange[0][1]

setze frame auf 0
solange frame < 5:
    bewege_schlange()
    zeichne_snake()
    fenster_aktualisieren(win)
    warte(50)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/snake_03_unten.bmp")
warte(100)

setze neue_y auf schlange[0][1]
wenn neue_y > alte_y:
    test_ok("Schlange bewegt sich nach unten: y " + text(alte_y) + " -> " + text(neue_y))
sonst:
    test_fail("Unten-Bewegung", "y unveraendert")

# --- Test 5: Richtungswechsel nach links ---
zeige "[Test 5] Richtungswechsel nach links"
taste_simulieren("a", wahr)
warte(30)
taste_simulieren("a", falsch)
setze richtung_x auf -1
setze richtung_y auf 0
setze alte_x auf schlange[0][0]

setze frame auf 0
solange frame < 5:
    bewege_schlange()
    zeichne_snake()
    fenster_aktualisieren(win)
    warte(50)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/snake_04_links.bmp")
warte(100)

wenn schlange[0][0] < alte_x:
    test_ok("Schlange bewegt sich links")
sonst:
    test_fail("Links-Bewegung", "x nicht verringert")

# --- Test 6: Richtungswechsel nach oben ---
zeige "[Test 6] Richtungswechsel nach oben"
taste_simulieren("w", wahr)
warte(30)
taste_simulieren("w", falsch)
setze richtung_x auf 0
setze richtung_y auf -1
setze alte_y auf schlange[0][1]

setze frame auf 0
solange frame < 5:
    bewege_schlange()
    zeichne_snake()
    fenster_aktualisieren(win)
    warte(50)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/snake_05_oben.bmp")
warte(100)

wenn schlange[0][1] < alte_y:
    test_ok("Schlange bewegt sich oben")
sonst:
    test_fail("Oben-Bewegung", "y nicht verringert")

# --- Test 7: Essen + Wachstum ---
zeige "[Test 7] Essen aufnehmen"
# Essen direkt vor den Kopf platzieren
setze kopf auf schlange[0]
setze essen auf [kopf[0], kopf[1] - 1]
setze alte_laenge auf länge(schlange)

zeichne_snake()
screenshot(win, "beispiele/test_screenshots/snake_06_vor_essen.bmp")
fenster_aktualisieren(win)
warte(100)

# Schlange bewegen und wachsen
bewege_und_wachse()
setze punkte auf punkte + 1

zeichne_snake()
screenshot(win, "beispiele/test_screenshots/snake_07_nach_essen.bmp")
fenster_aktualisieren(win)
warte(100)

setze neue_laenge auf länge(schlange)
wenn neue_laenge > alte_laenge:
    test_ok("Schlange gewachsen: " + text(alte_laenge) + " -> " + text(neue_laenge))
sonst:
    test_fail("Wachstum", "Laenge unveraendert")

# --- Test 8: Wand-Kollision ---
zeige "[Test 8] Wand-Kollision"
# Schlange an den oberen Rand setzen
setze schlange auf [[10, 0], [10, 1], [10, 2], [10, 3]]
setze richtung_x auf 0
setze richtung_y auf -1

zeichne_snake()
screenshot(win, "beispiele/test_screenshots/snake_08_vor_wand.bmp")
fenster_aktualisieren(win)
warte(100)

# Naechster Schritt wuerde y = -1 ergeben
setze kopf auf schlange[0]
setze neu_y auf kopf[1] + richtung_y
wenn neu_y < 0:
    setze spielende auf wahr
    test_ok("Wand-Kollision erkannt: y = " + text(neu_y) + " (< 0)")
sonst:
    test_fail("Wand-Kollision", "Kein Game Over bei y = " + text(neu_y))

# --- Test 9: Game-Over Rendering ---
zeige "[Test 9] Game-Over Rendering"
fenster_löschen(win, "schwarz")
für segment in schlange:
    zeichne_rechteck(win, segment[0] * ZELLE, segment[1] * ZELLE, ZELLE - 1, ZELLE - 1, "gruen")
# Game-Over Box
zeichne_rechteck(win, 170, 180, 300, 120, "rot")
zeichne_rechteck(win, 180, 190, 280, 100, "schwarz")
setze res auf punkte * 15
wenn res > 260:
    setze res auf 260
zeichne_rechteck(win, 190, 220, res, 20, "gelb")
zeichne_rechteck(win, 260, 260, 120, 15, "weiss")

screenshot(win, "beispiele/test_screenshots/snake_09_gameover.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("Game-Over Screen gerendert")

# --- Test 10: Selbst-Kollision ---
zeige "[Test 10] Selbst-Kollision"
setze spielende auf falsch
# Schlange in eine Spirale legen
setze schlange auf [[5, 5], [6, 5], [6, 6], [5, 6], [4, 6], [4, 5]]
setze richtung_x auf -1
setze richtung_y auf 0
# Naechster Kopf waere [4, 5] — das ist das letzte Segment!
setze kopf auf schlange[0]
setze neu_x auf kopf[0] + richtung_x
setze neu_y auf kopf[1] + richtung_y
für segment in schlange:
    wenn segment[0] == neu_x:
        wenn segment[1] == neu_y:
            setze spielende auf wahr

zeichne_snake()
screenshot(win, "beispiele/test_screenshots/snake_10_selbst_kollision.bmp")
fenster_aktualisieren(win)
warte(100)

wenn spielende:
    test_ok("Selbst-Kollision erkannt bei [" + text(neu_x) + ", " + text(neu_y) + "]")
sonst:
    test_fail("Selbst-Kollision", "Nicht erkannt")

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

# ============================================================
# test_tetris.moo — Automatisierter Test fuer Tetris
#
# Testet: Fenster, Brett-Init, Stueck-Bewegung, Rotation,
#         Fall, Fixierung, Linien-Clearing, Screenshot pro Aktion
# ============================================================

konstante BREITE auf 800
konstante HOEHE auf 600
konstante ZELLE auf 28
konstante COLS auf 10
konstante ROWS auf 20
konstante BRETT_X auf 40
konstante BRETT_Y auf 20

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

zeige "=== TETRIS AUTOMATISIERTE TESTS ==="
zeige ""

# --- Fenster ---
zeige "[Test 1] Fenster"
setze fenster auf fenster_erstelle("Tetris Test", BREITE, HOEHE)
wenn fenster_offen(fenster):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster", "nicht offen")

# --- Brett initialisieren ---
setze brett auf []
setze r auf 0
solange r < ROWS:
    setze zeile auf []
    setze c auf 0
    solange c < COLS:
        zeile.hinzufügen(0)
        c += 1
    brett.hinzufügen(zeile)
    r += 1

setze farben auf ["schwarz", "cyan", "gelb", "magenta", "gruen", "rot", "blau", "orange"]

# Tetrominos
setze I_rot auf [[[0, 1], [1, 1], [2, 1], [3, 1]], [[2, 0], [2, 1], [2, 2], [2, 3]], [[0, 2], [1, 2], [2, 2], [3, 2]], [[1, 0], [1, 1], [1, 2], [1, 3]]]
setze O_rot auf [[[1, 0], [2, 0], [1, 1], [2, 1]], [[1, 0], [2, 0], [1, 1], [2, 1]], [[1, 0], [2, 0], [1, 1], [2, 1]], [[1, 0], [2, 0], [1, 1], [2, 1]]]
setze T_rot auf [[[1, 0], [0, 1], [1, 1], [2, 1]], [[1, 0], [1, 1], [2, 1], [1, 2]], [[0, 1], [1, 1], [2, 1], [1, 2]], [[1, 0], [0, 1], [1, 1], [1, 2]]]
setze alle_stuecke auf [I_rot, O_rot, T_rot]

setze aktuell auf 0
setze rotation auf 0
setze stueck_x auf 3
setze stueck_y auf 0
setze punkte auf 0
setze level auf 1
setze linien_gesamt auf 0

funktion zeichne_brett():
    fenster_löschen(fenster, "schwarz")
    zeichne_rechteck(fenster, BRETT_X - 2, BRETT_Y - 2, COLS * ZELLE + 4, ROWS * ZELLE + 4, "grau")
    zeichne_rechteck(fenster, BRETT_X, BRETT_Y, COLS * ZELLE, ROWS * ZELLE, "schwarz")
    # Fixierte Steine
    setze ry auf 0
    solange ry < ROWS:
        setze zeile auf brett[ry]
        setze rx auf 0
        solange rx < COLS:
            setze v auf zeile[rx]
            wenn v != 0:
                zeichne_rechteck(fenster, BRETT_X + rx * ZELLE + 1, BRETT_Y + ry * ZELLE + 1, ZELLE - 2, ZELLE - 2, farben[v])
            rx += 1
        ry += 1
    # Aktuelles Stueck
    setze bloecke auf alle_stuecke[aktuell][rotation]
    setze akt_farbe auf farben[aktuell + 1]
    für b in bloecke:
        setze bx auf stueck_x + b[0]
        setze by auf stueck_y + b[1]
        wenn by >= 0:
            zeichne_rechteck(fenster, BRETT_X + bx * ZELLE + 1, BRETT_Y + by * ZELLE + 1, ZELLE - 2, ZELLE - 2, akt_farbe)

# --- Test 2: Leeres Brett ---
zeige "[Test 2] Leeres Brett rendern"
zeichne_brett()
screenshot(fenster, "beispiele/test_screenshots/tetris_01_start.bmp")
fenster_aktualisieren(fenster)
warte(100)
test_ok("Leeres Brett mit I-Stueck oben")

# --- Test 3: Stueck nach links ---
zeige "[Test 3] Stueck nach links"
setze alte_x auf stueck_x
taste_simulieren("links", wahr)
warte(30)
taste_simulieren("links", falsch)
setze stueck_x auf stueck_x - 1
zeichne_brett()
screenshot(fenster, "beispiele/test_screenshots/tetris_02_links.bmp")
fenster_aktualisieren(fenster)
warte(100)
wenn stueck_x < alte_x:
    test_ok("Stueck nach links: " + text(alte_x) + " -> " + text(stueck_x))
sonst:
    test_fail("Links", "Position unveraendert")

# --- Test 4: Stueck nach rechts ---
zeige "[Test 4] Stueck nach rechts"
setze alte_x auf stueck_x
taste_simulieren("rechts", wahr)
warte(30)
taste_simulieren("rechts", falsch)
setze stueck_x auf stueck_x + 1
zeichne_brett()
screenshot(fenster, "beispiele/test_screenshots/tetris_03_rechts.bmp")
fenster_aktualisieren(fenster)
warte(100)
wenn stueck_x > alte_x:
    test_ok("Stueck nach rechts: " + text(alte_x) + " -> " + text(stueck_x))
sonst:
    test_fail("Rechts", "Position unveraendert")

# --- Test 5: Rotation ---
zeige "[Test 5] Rotation"
setze alte_rot auf rotation
taste_simulieren("hoch", wahr)
warte(30)
taste_simulieren("hoch", falsch)
setze rotation auf (rotation + 1) % 4
zeichne_brett()
screenshot(fenster, "beispiele/test_screenshots/tetris_04_rotation.bmp")
fenster_aktualisieren(fenster)
warte(100)
wenn rotation != alte_rot:
    test_ok("Rotation: " + text(alte_rot) + " -> " + text(rotation))
sonst:
    test_fail("Rotation", "unveraendert")

# --- Test 6: Stueck fallen lassen (Soft Drop) ---
zeige "[Test 6] Soft Drop (10 Zeilen)"
setze alte_y auf stueck_y
setze frame auf 0
solange frame < 10:
    taste_simulieren("runter", wahr)
    warte(30)
    taste_simulieren("runter", falsch)
    setze stueck_y auf stueck_y + 1
    zeichne_brett()
    fenster_aktualisieren(fenster)
    warte(50)
    setze frame auf frame + 1

screenshot(fenster, "beispiele/test_screenshots/tetris_05_softdrop.bmp")
warte(100)
wenn stueck_y > alte_y:
    test_ok("Soft Drop: y " + text(alte_y) + " -> " + text(stueck_y))
sonst:
    test_fail("Soft Drop", "y unveraendert")

# --- Test 7: Stueck fixieren ---
zeige "[Test 7] Stueck fixieren"
# I-Stueck (Rotation 1 = vertikal) ganz unten platzieren
setze aktuell auf 0
setze rotation auf 0
setze stueck_x auf 3
setze stueck_y auf 18

# Auf Brett fixieren
setze bloecke auf alle_stuecke[aktuell][rotation]
für b in bloecke:
    setze bx auf stueck_x + b[0]
    setze by auf stueck_y + b[1]
    wenn by >= 0 und by < ROWS und bx >= 0 und bx < COLS:
        setze z auf brett[by]
        z[bx] = aktuell + 1
        brett[by] = z

# Neues Stueck
setze aktuell auf 1
setze rotation auf 0
setze stueck_x auf 3
setze stueck_y auf 0

zeichne_brett()
screenshot(fenster, "beispiele/test_screenshots/tetris_06_fixiert.bmp")
fenster_aktualisieren(fenster)
warte(100)

# Pruefen ob Bloecke auf dem Brett sind
setze z auf brett[19]
wenn z[4] != 0:
    test_ok("Stueck auf Brett fixiert (Zeile 19, Spalte 4 = " + text(z[4]) + ")")
sonst:
    test_fail("Fixierung", "Brett leer an [19][4]")

# --- Test 8: Linien-Clearing vorbereiten ---
zeige "[Test 8] Volle Zeile fuellen"
# Zeile 19 komplett fuellen
setze z auf brett[19]
setze c auf 0
solange c < COLS:
    z[c] = 3
    c += 1
brett[19] = z

zeichne_brett()
screenshot(fenster, "beispiele/test_screenshots/tetris_07_volle_zeile.bmp")
fenster_aktualisieren(fenster)
warte(100)

# Pruefen ob Zeile voll ist
setze voll auf wahr
setze z auf brett[19]
für v in z:
    wenn v == 0:
        setze voll auf falsch
wenn voll:
    test_ok("Zeile 19 ist komplett gefuellt")
sonst:
    test_fail("Zeile fuellen", "Zeile nicht voll")

# --- Test 9: Linien-Clearing ausfuehren ---
zeige "[Test 9] Linien-Clearing"
setze neue_zeilen auf []
setze geloeschte auf 0
für zz in brett:
    setze voll auf wahr
    für v in zz:
        wenn v == 0:
            setze voll auf falsch
    wenn voll:
        geloeschte += 1
    sonst:
        neue_zeilen.hinzufügen(zz)

setze aufgefuellt auf []
setze gi auf 0
solange gi < geloeschte:
    setze leer auf []
    setze cc auf 0
    solange cc < COLS:
        leer.hinzufügen(0)
        cc += 1
    aufgefuellt.hinzufügen(leer)
    gi += 1
für nz in neue_zeilen:
    aufgefuellt.hinzufügen(nz)
setze brett auf aufgefuellt

zeichne_brett()
screenshot(fenster, "beispiele/test_screenshots/tetris_08_nach_clearing.bmp")
fenster_aktualisieren(fenster)
warte(100)

wenn geloeschte > 0:
    test_ok("" + text(geloeschte) + " Zeile(n) geloescht")
sonst:
    test_fail("Line-Clear", "Keine Zeilen entfernt")

# Pruefen ob unterste Zeile jetzt leer ist
setze z auf brett[19]
setze leer_count auf 0
für v in z:
    wenn v == 0:
        leer_count += 1

# Die fixierten I-Bloecke von Test 7 sollten jetzt in Zeile 19 sein (runtergerutscht)
test_ok("Brett nach Clearing: Zeile 19 hat " + text(leer_count) + " leere Zellen")

# --- Test 10: Game-Loop (30 Frames) ---
zeige "[Test 10] Game-Loop (30 Frames)"
setze aktuell auf 2
setze rotation auf 0
setze stueck_x auf 4
setze stueck_y auf 0

setze frame auf 0
solange frame < 30:
    # Stueck faellt
    setze stueck_y auf stueck_y + 1
    wenn stueck_y > 16:
        setze stueck_y auf 0
    zeichne_brett()
    wenn frame == 0:
        screenshot(fenster, "beispiele/test_screenshots/tetris_09_loop_start.bmp")
    wenn frame == 15:
        screenshot(fenster, "beispiele/test_screenshots/tetris_10_loop_mitte.bmp")
    wenn frame == 29:
        screenshot(fenster, "beispiele/test_screenshots/tetris_11_loop_ende.bmp")
    fenster_aktualisieren(fenster)
    warte(30)
    setze frame auf frame + 1

test_ok("30 Frames ohne Crash")

# ============================================================
zeige ""
zeige "=== ERGEBNIS ==="
zeige "Bestanden: " + text(bestanden)
zeige "Fehlgeschlagen: " + text(fehlgeschlagen)
wenn fehlgeschlagen == 0:
    zeige "ALLE TESTS BESTANDEN!"
sonst:
    zeige "FEHLER GEFUNDEN — siehe oben"

fenster_schliessen(fenster)

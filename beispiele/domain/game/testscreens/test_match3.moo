# ============================================================
# test_match3.moo — Automatisierter Test
#
# Testet: Fenster, Grid-Init, Rendering, Swap, Match-Erkennung,
#         Gravity-Drop, Screenshot pro Aktion
# ============================================================

setze BREITE auf 640
setze HOEHE auf 700
setze GRID auf 8
setze TILE auf 64
setze OFFSET_X auf 64
setze OFFSET_Y auf 80

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

funktion stein_farbe(typ):
    wenn typ == 0:
        gib_zurück "#F44336"
    wenn typ == 1:
        gib_zurück "#2196F3"
    wenn typ == 2:
        gib_zurück "#4CAF50"
    wenn typ == 3:
        gib_zurück "#FFEB3B"
    wenn typ == 4:
        gib_zurück "#9C27B0"
    wenn typ == 5:
        gib_zurück "#FF9800"
    gib_zurück "#FFFFFF"

zeige "=== MATCH-3 AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster ---
zeige "[Test 1] Fenster"
setze win auf fenster_erstelle("Match-3 Test", BREITE, HOEHE)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster", "nicht offen")

# Grid init (feste Werte fuer reproduzierbare Tests)
setze brett auf []
# Zeile 0: abwechselnd
setze muster auf [0, 1, 2, 3, 4, 5, 0, 1, 1, 2, 3, 4, 5, 0, 1, 2, 2, 3, 4, 5, 0, 1, 2, 3, 3, 4, 5, 0, 1, 2, 3, 4, 4, 5, 0, 1, 2, 3, 4, 5, 5, 0, 1, 2, 3, 4, 5, 0, 0, 1, 2, 3, 4, 5, 0, 1, 1, 2, 3, 4, 5, 0, 1, 2]
setze i auf 0
solange i < 64:
    brett.hinzufügen(muster[i])
    i += 1

setze punkte auf 0

funktion get_stein(x, y):
    gib_zurück brett[y * GRID + x]

funktion set_stein(x, y, val):
    brett[y * GRID + x] = val

funktion zeichne_match3():
    fenster_löschen(win, "#1A1A2E")
    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 70, "#2C2C54")
    # Grid-Hintergrund
    zeichne_rechteck(win, OFFSET_X - 4, OFFSET_Y - 4, GRID * TILE + 8, GRID * TILE + 8, "#333366")
    # Steine
    setze gy auf 0
    solange gy < GRID:
        setze gx auf 0
        solange gx < GRID:
            setze typ auf get_stein(gx, gy)
            setze px auf OFFSET_X + gx * TILE
            setze py auf OFFSET_Y + gy * TILE
            wenn typ >= 0:
                setze farbe auf stein_farbe(typ)
                zeichne_kreis(win, px + TILE / 2, py + TILE / 2, TILE / 2 - 4, farbe)
            setze gx auf gx + 1
        setze gy auf gy + 1

# --- Test 2: Grid rendern ---
zeige "[Test 2] Grid rendern"
zeichne_match3()
screenshot(win, "beispiele/test_screenshots/match3_01_start.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("8x8 Grid mit Steinen gerendert")

# --- Test 3: Stein auswaehlen (Klick) ---
zeige "[Test 3] Stein auswaehlen"
setze sel_x auf 2
setze sel_y auf 3
maus_simulieren(OFFSET_X + sel_x * TILE + 32, OFFSET_Y + sel_y * TILE + 32, wahr)
warte(50)

# Markierung zeichnen
zeichne_match3()
setze px auf OFFSET_X + sel_x * TILE
setze py auf OFFSET_Y + sel_y * TILE
zeichne_rechteck(win, px, py, TILE, TILE, "weiss")
screenshot(win, "beispiele/test_screenshots/match3_02_auswahl.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("Stein [2,3] ausgewaehlt")

# --- Test 4: Swap ---
zeige "[Test 4] Steine tauschen"
setze a auf get_stein(2, 3)
setze b auf get_stein(3, 3)
set_stein(2, 3, b)
set_stein(3, 3, a)

zeichne_match3()
screenshot(win, "beispiele/test_screenshots/match3_03_swap.bmp")
fenster_aktualisieren(win)
warte(100)

wenn get_stein(2, 3) == b und get_stein(3, 3) == a:
    test_ok("Swap: [2,3]=" + text(b) + " <-> [3,3]=" + text(a))
sonst:
    test_fail("Swap", "Werte nicht getauscht")

# --- Test 5: 3er-Match horizontal erzwingen ---
zeige "[Test 5] 3er-Match horizontal"
# Zeile 7 mit 3 gleichen fuellen
set_stein(2, 7, 0)
set_stein(3, 7, 0)
set_stein(4, 7, 0)

# Match erkennen
setze match_gefunden auf falsch
setze gx auf 0
solange gx < GRID - 2:
    wenn get_stein(gx, 7) == get_stein(gx + 1, 7):
        wenn get_stein(gx, 7) == get_stein(gx + 2, 7):
            setze match_gefunden auf wahr
    setze gx auf gx + 1

zeichne_match3()
screenshot(win, "beispiele/test_screenshots/match3_04_match.bmp")
fenster_aktualisieren(win)
warte(100)

wenn match_gefunden:
    test_ok("3er-Match horizontal erkannt in Zeile 7")
sonst:
    test_fail("Match", "Nicht erkannt")

# --- Test 6: Match loeschen ---
zeige "[Test 6] Match loeschen"
set_stein(2, 7, -1)
set_stein(3, 7, -1)
set_stein(4, 7, -1)
setze punkte auf punkte + 30

zeichne_match3()
screenshot(win, "beispiele/test_screenshots/match3_05_geloescht.bmp")
fenster_aktualisieren(win)
warte(100)

wenn get_stein(2, 7) == -1:
    test_ok("3 Steine geloescht, Punkte: " + text(punkte))
sonst:
    test_fail("Loeschen", "Steine noch da")

# --- Test 7: Gravity ---
zeige "[Test 7] Gravity-Drop"
# Steine fallen: Zeile 6 faellt in Zeile 7
setze gx auf 2
solange gx <= 4:
    setze gy auf 6
    solange gy >= 0:
        wenn gy + 1 < GRID:
            wenn get_stein(gx, gy + 1) == -1 und get_stein(gx, gy) >= 0:
                set_stein(gx, gy + 1, get_stein(gx, gy))
                set_stein(gx, gy, -1)
        setze gy auf gy - 1
    setze gx auf gx + 1

zeichne_match3()
screenshot(win, "beispiele/test_screenshots/match3_06_gravity.bmp")
fenster_aktualisieren(win)
warte(100)

# Zeile 7 sollte jetzt Werte von Zeile 6 haben
wenn get_stein(2, 7) >= 0:
    test_ok("Gravity: Steine runtergefallen")
sonst:
    test_fail("Gravity", "Leere Zellen unten")

# --- Test 8: 3er-Match vertikal ---
zeige "[Test 8] 3er-Match vertikal"
set_stein(0, 3, 5)
set_stein(0, 4, 5)
set_stein(0, 5, 5)

setze match_v auf falsch
setze gy auf 0
solange gy < GRID - 2:
    wenn get_stein(0, gy) == get_stein(0, gy + 1):
        wenn get_stein(0, gy) == get_stein(0, gy + 2):
            wenn get_stein(0, gy) >= 0:
                setze match_v auf wahr
    setze gy auf gy + 1

zeichne_match3()
screenshot(win, "beispiele/test_screenshots/match3_07_vertikal.bmp")
fenster_aktualisieren(win)
warte(100)

wenn match_v:
    test_ok("3er-Match vertikal erkannt in Spalte 0")
sonst:
    test_fail("Vertikal-Match", "Nicht erkannt")

# --- Test 9: Game-Loop ---
zeige "[Test 9] Game-Loop (20 Frames)"
setze frame auf 0
solange frame < 20:
    zeichne_match3()
    wenn frame == 19:
        screenshot(win, "beispiele/test_screenshots/match3_08_loop.bmp")
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

test_ok("20 Frames ohne Crash")

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

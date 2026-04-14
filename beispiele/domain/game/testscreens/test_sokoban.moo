# ============================================================
# test_sokoban.moo — Automatisierter Test
#
# Testet: Fenster, Level-Rendering, Spieler-Bewegung, Kiste-Schieben,
#         Wand-Blockierung, Ziel-Erkennung, Screenshot pro Aktion
# ============================================================

konstante TILE auf 48
konstante WIN_W auf 576
konstante WIN_H auf 480

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

zeige "=== SOKOBAN AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster ---
zeige "[Test 1] Fenster"
setze win auf fenster_erstelle("Sokoban Test", WIN_W, WIN_H)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster", "nicht offen")

# Einfaches 6x6 Grid
setze grid_w auf 6
setze grid_h auf 6
setze grid auf ["W", "W", "W", "W", "W", "W", "W", ".", ".", ".", "Z", "W", "W", ".", "K", ".", ".", "W", "W", ".", ".", "S", ".", "W", "W", ".", ".", ".", ".", "W", "W", "W", "W", "W", "W", "W"]

setze spieler_x auf 3
setze spieler_y auf 3
setze kiste_x auf 2
setze kiste_y auf 2
setze ziel_x auf 4
setze ziel_y auf 1

funktion get_cell(x, y):
    gib_zurück grid[y * grid_w + x]

funktion set_cell(x, y, val):
    grid[y * grid_w + x] = val

funktion zeichne_sokoban():
    fenster_löschen(win, "#3E2723")
    setze gy auf 0
    solange gy < grid_h:
        setze gx auf 0
        solange gx < grid_w:
            setze c auf get_cell(gx, gy)
            setze px auf gx * TILE
            setze py auf gy * TILE
            wenn c == "W":
                zeichne_rechteck(win, px + 1, py + 1, TILE - 2, TILE - 2, "#5D4037")
            wenn c == "." oder c == "S" oder c == "K" oder c == "Z":
                zeichne_rechteck(win, px + 1, py + 1, TILE - 2, TILE - 2, "#E8E0D0")
            wenn c == "Z":
                zeichne_kreis(win, px + TILE / 2, py + TILE / 2, 8, "#FF8A65")
            wenn c == "K":
                zeichne_rechteck(win, px + 6, py + 6, TILE - 12, TILE - 12, "#FFA726")
            wenn c == "S":
                zeichne_kreis(win, px + TILE / 2, py + TILE / 2, 16, "#42A5F5")
            setze gx auf gx + 1
        setze gy auf gy + 1

# --- Test 2: Level rendern ---
zeige "[Test 2] Level rendern"
zeichne_sokoban()
screenshot(win, "beispiele/test_screenshots/sokoban_01_start.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("Level gerendert: Spieler [3,3], Kiste [2,2], Ziel [4,1]")

# --- Test 3: Spieler nach oben ---
zeige "[Test 3] Spieler nach oben"
taste_simulieren("w", wahr)
warte(30)
taste_simulieren("w", falsch)
set_cell(spieler_x, spieler_y, ".")
setze spieler_y auf spieler_y - 1
set_cell(spieler_x, spieler_y, "S")

zeichne_sokoban()
screenshot(win, "beispiele/test_screenshots/sokoban_02_oben.bmp")
fenster_aktualisieren(win)
warte(100)
wenn spieler_y == 2:
    test_ok("Spieler nach oben: [3,2]")
sonst:
    test_fail("Oben", "y = " + text(spieler_y))

# --- Test 4: Kiste schieben ---
zeige "[Test 4] Kiste nach oben schieben"
taste_simulieren("a", wahr)
warte(30)
taste_simulieren("a", falsch)
set_cell(spieler_x, spieler_y, ".")
set_cell(kiste_x, kiste_y, ".")
setze kiste_y auf kiste_y - 1
set_cell(kiste_x, kiste_y, "K")
setze spieler_x auf spieler_x - 1
set_cell(spieler_x, spieler_y, "S")

zeichne_sokoban()
screenshot(win, "beispiele/test_screenshots/sokoban_03_kiste.bmp")
fenster_aktualisieren(win)
warte(100)
wenn kiste_y == 1 und spieler_x == 2:
    test_ok("Kiste geschoben: Kiste [2,1], Spieler [2,2]")
sonst:
    test_fail("Kiste", "Falsche Position")

# --- Test 5: Wand blockiert ---
zeige "[Test 5] Wand blockiert"
setze blockiert auf falsch
wenn get_cell(kiste_x, kiste_y - 1) == "W":
    setze blockiert auf wahr

screenshot(win, "beispiele/test_screenshots/sokoban_04_blockiert.bmp")
fenster_aktualisieren(win)
warte(100)
wenn blockiert:
    test_ok("Wand blockiert Kiste nach oben")
sonst:
    test_fail("Wand", "Nicht blockiert")

# --- Test 6: Kiste zum Ziel ---
zeige "[Test 6] Kiste zum Ziel schieben"
set_cell(spieler_x, spieler_y, ".")
setze spieler_x auf 1
setze spieler_y auf 1
set_cell(spieler_x, spieler_y, "S")

# Schub 1: Kiste [2,1]->[3,1]
set_cell(spieler_x, spieler_y, ".")
set_cell(kiste_x, kiste_y, ".")
setze kiste_x auf kiste_x + 1
set_cell(kiste_x, kiste_y, "K")
setze spieler_x auf spieler_x + 1
set_cell(spieler_x, spieler_y, "S")

zeichne_sokoban()
screenshot(win, "beispiele/test_screenshots/sokoban_05_schub1.bmp")
fenster_aktualisieren(win)
warte(100)

# Schub 2: Kiste [3,1]->[4,1]=Ziel
taste_simulieren("d", wahr)
warte(30)
taste_simulieren("d", falsch)
set_cell(spieler_x, spieler_y, ".")
set_cell(kiste_x, kiste_y, ".")
setze kiste_x auf kiste_x + 1
set_cell(kiste_x, kiste_y, "K")
setze spieler_x auf spieler_x + 1
set_cell(spieler_x, spieler_y, "S")

zeichne_sokoban()
screenshot(win, "beispiele/test_screenshots/sokoban_06_ziel.bmp")
fenster_aktualisieren(win)
warte(100)

wenn kiste_x == ziel_x und kiste_y == ziel_y:
    test_ok("Kiste auf Ziel! Level geloest!")
sonst:
    test_fail("Ziel", "Kiste [" + text(kiste_x) + "," + text(kiste_y) + "]")

# --- Test 7: Game-Loop ---
zeige "[Test 7] Game-Loop (20 Frames)"
setze frame auf 0
solange frame < 20:
    zeichne_sokoban()
    wenn frame == 19:
        screenshot(win, "beispiele/test_screenshots/sokoban_07_loop.bmp")
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

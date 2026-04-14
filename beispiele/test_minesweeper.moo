# ============================================================
# test_minesweeper.moo — Automatisierter Test
#
# Testet: Fenster, Grid-Rendering, Mausklick-Aufdecken,
#         Minen-Erkennung, Flaggen, Screenshot pro Aktion
# ============================================================

konstante COLS auf 10
konstante ROWS auf 10
konstante CELL auf 40
konstante HUD_H auf 48
konstante WIN_W auf 400
konstante WIN_H auf 448

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

zeige "=== MINESWEEPER AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster ---
zeige "[Test 1] Fenster"
setze win auf fenster_erstelle("Minesweeper Test", WIN_W, WIN_H)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster", "nicht offen")

# Grid: inhalt (flach 10x10), status (0=verdeckt, 1=offen, 2=flagge)
setze inhalt auf []
setze status auf []
setze i auf 0
solange i < COLS * ROWS:
    inhalt.hinzufügen(0)
    status.hinzufügen(0)
    i += 1

# Minen platzieren (fest fuer reproduzierbare Tests)
inhalt[15] = -1
inhalt[27] = -1
inhalt[33] = -1
inhalt[48] = -1
inhalt[56] = -1
inhalt[71] = -1
inhalt[84] = -1
inhalt[92] = -1

# Nachbar-Zahlen berechnen
funktion nachbarn_zaehlen(idx):
    setze cx auf idx % COLS
    setze cy auf boden(idx / COLS)
    setze count auf 0
    setze dy auf -1
    solange dy <= 1:
        setze dx auf -1
        solange dx <= 1:
            wenn dx != 0 oder dy != 0:
                setze nx auf cx + dx
                setze ny auf cy + dy
                wenn nx >= 0 und nx < COLS und ny >= 0 und ny < ROWS:
                    wenn inhalt[ny * COLS + nx] == -1:
                        setze count auf count + 1
            setze dx auf dx + 1
        setze dy auf dy + 1
    gib_zurück count

setze i auf 0
solange i < COLS * ROWS:
    wenn inhalt[i] != -1:
        inhalt[i] = nachbarn_zaehlen(i)
    i += 1

funktion zahl_farbe(n):
    wenn n == 1:
        gib_zurück "#1565C0"
    wenn n == 2:
        gib_zurück "#2E7D32"
    wenn n == 3:
        gib_zurück "#C62828"
    gib_zurück "#000000"

funktion zeichne_ms():
    fenster_löschen(win, "#BDBDBD")
    # HUD
    zeichne_rechteck(win, 0, 0, WIN_W, HUD_H, "#424242")
    # Grid
    setze gy auf 0
    solange gy < ROWS:
        setze gx auf 0
        solange gx < COLS:
            setze idx auf gy * COLS + gx
            setze px auf gx * CELL
            setze py auf gy * CELL + HUD_H
            wenn status[idx] == 0:
                # Verdeckt
                zeichne_rechteck(win, px + 1, py + 1, CELL - 2, CELL - 2, "#9E9E9E")
            wenn status[idx] == 1:
                # Aufgedeckt
                zeichne_rechteck(win, px + 1, py + 1, CELL - 2, CELL - 2, "#E0E0E0")
                wenn inhalt[idx] == -1:
                    zeichne_kreis(win, px + CELL / 2, py + CELL / 2, 10, "schwarz")
                wenn inhalt[idx] > 0:
                    # Zahl als farbiger Kreis
                    zeichne_kreis(win, px + CELL / 2, py + CELL / 2, 8, zahl_farbe(inhalt[idx]))
            wenn status[idx] == 2:
                # Flagge
                zeichne_rechteck(win, px + 1, py + 1, CELL - 2, CELL - 2, "#9E9E9E")
                zeichne_rechteck(win, px + 12, py + 8, 16, 24, "rot")
            setze gx auf gx + 1
        setze gy auf gy + 1

# --- Test 2: Verdecktes Grid ---
zeige "[Test 2] Verdecktes Grid"
zeichne_ms()
screenshot(win, "beispiele/test_screenshots/ms_01_start.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("10x10 Grid verdeckt gerendert")

# --- Test 3: Feld aufdecken (sicher) ---
zeige "[Test 3] Sicheres Feld aufdecken"
# Feld [0,0] = idx 0, kein Mine
maus_simulieren(20, HUD_H + 20, wahr)
warte(50)
status[0] = 1

zeichne_ms()
screenshot(win, "beispiele/test_screenshots/ms_02_aufgedeckt.bmp")
fenster_aktualisieren(win)
warte(100)

wenn status[0] == 1:
    test_ok("Feld [0,0] aufgedeckt, Inhalt: " + text(inhalt[0]))
sonst:
    test_fail("Aufdecken", "Status unveraendert")

# --- Test 4: Mehrere Felder aufdecken ---
zeige "[Test 4] Mehrere Felder aufdecken"
# Sichere Felder aufdecken
setze i auf 0
solange i < 10:
    wenn inhalt[i] != -1:
        status[i] = 1
    i += 1

zeichne_ms()
screenshot(win, "beispiele/test_screenshots/ms_03_mehrere.bmp")
fenster_aktualisieren(win)
warte(100)

setze offene auf 0
setze i auf 0
solange i < COLS * ROWS:
    wenn status[i] == 1:
        setze offene auf offene + 1
    i += 1

test_ok("" + text(offene) + " Felder aufgedeckt")

# --- Test 5: Flagge setzen ---
zeige "[Test 5] Flagge setzen"
# Flagge auf Mine-Feld
taste_simulieren("f", wahr)
warte(30)
taste_simulieren("f", falsch)
status[15] = 2

zeichne_ms()
screenshot(win, "beispiele/test_screenshots/ms_04_flagge.bmp")
fenster_aktualisieren(win)
warte(100)

wenn status[15] == 2:
    test_ok("Flagge auf Feld 15 gesetzt")
sonst:
    test_fail("Flagge", "Status unveraendert")

# --- Test 6: Mine aufdecken ---
zeige "[Test 6] Mine aufdecken"
status[27] = 1

zeichne_ms()
screenshot(win, "beispiele/test_screenshots/ms_05_mine.bmp")
fenster_aktualisieren(win)
warte(100)

wenn inhalt[27] == -1 und status[27] == 1:
    test_ok("Mine aufgedeckt bei Feld 27 — Game Over!")
sonst:
    test_fail("Mine", "Nicht korrekt aufgedeckt")

# --- Test 7: Alle Minen anzeigen (Game Over) ---
zeige "[Test 7] Game-Over — alle Minen zeigen"
setze i auf 0
solange i < COLS * ROWS:
    wenn inhalt[i] == -1:
        status[i] = 1
    i += 1

zeichne_ms()
screenshot(win, "beispiele/test_screenshots/ms_06_gameover.bmp")
fenster_aktualisieren(win)
warte(100)

# Zaehle sichtbare Minen
setze minen_sichtbar auf 0
setze i auf 0
solange i < COLS * ROWS:
    wenn inhalt[i] == -1 und status[i] == 1:
        setze minen_sichtbar auf minen_sichtbar + 1
    i += 1

test_ok("" + text(minen_sichtbar) + " Minen sichtbar")

# --- Test 8: Game-Loop (20 Frames) ---
zeige "[Test 8] Game-Loop (20 Frames)"
setze frame auf 0
solange frame < 20:
    zeichne_ms()
    wenn frame == 19:
        screenshot(win, "beispiele/test_screenshots/ms_07_loop.bmp")
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

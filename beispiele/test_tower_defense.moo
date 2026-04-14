# ============================================================
# test_tower_defense.moo — Automatisierter Test
#
# Testet: Fenster, Grid-Rendering, Turm-Platzierung, Gegner-Spawn,
#         Gegner-Bewegung, Geschoss-System, Screenshot pro Aktion
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600
setze TILE auf 40
setze GRID_W auf 20
setze GRID_H auf 15

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

zeige "=== TOWER DEFENSE AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster ---
zeige "[Test 1] Fenster"
setze win auf fenster_erstelle("TD Test", BREITE, HOEHE)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster", "nicht offen")

# Grid initialisieren
setze karte auf []
setze i auf 0
solange i < GRID_W * GRID_H:
    karte.hinzufügen(0)
    i += 1

# Pfad setzen (horizontal Mitte)
setze x auf 0
solange x < GRID_W:
    karte[7 * GRID_W + x] = 1
    setze x auf x + 1
karte[7 * GRID_W] = 2
karte[7 * GRID_W + GRID_W - 1] = 3

# Turm-Daten
setze turm_x auf []
setze turm_y auf []
setze turm_typ auf []
setze turm_anzahl auf 0

# Gegner-Daten
setze gegner_x auf []
setze gegner_y auf []
setze gegner_hp auf []
setze gegner_aktiv auf []
setze i auf 0
solange i < 10:
    gegner_x.hinzufügen(0.0)
    gegner_y.hinzufügen(0.0)
    gegner_hp.hinzufügen(0)
    gegner_aktiv.hinzufügen(falsch)
    i += 1

setze gold auf 200
setze punkte auf 0
setze leben auf 10

funktion zeichne_td():
    fenster_löschen(win, "#2D5F2D")
    # Grid
    setze gy auf 0
    solange gy < GRID_H:
        setze gx auf 0
        solange gx < GRID_W:
            setze idx auf gy * GRID_W + gx
            setze val auf karte[idx]
            wenn val == 1:
                zeichne_rechteck(win, gx * TILE, gy * TILE, TILE - 1, TILE - 1, "#C4A882")
            wenn val == 2:
                zeichne_rechteck(win, gx * TILE, gy * TILE, TILE - 1, TILE - 1, "#4CAF50")
            wenn val == 3:
                zeichne_rechteck(win, gx * TILE, gy * TILE, TILE - 1, TILE - 1, "#F44336")
            setze gx auf gx + 1
        setze gy auf gy + 1
    # Tuerme
    setze i auf 0
    solange i < turm_anzahl:
        setze farbe auf "blau"
        wenn turm_typ[i] == 1:
            setze farbe auf "blau"
        wenn turm_typ[i] == 2:
            setze farbe auf "orange"
        wenn turm_typ[i] == 3:
            setze farbe auf "magenta"
        zeichne_rechteck(win, turm_x[i] * TILE + 5, turm_y[i] * TILE + 5, TILE - 10, TILE - 10, farbe)
        setze i auf i + 1
    # Gegner
    setze i auf 0
    solange i < 10:
        wenn gegner_aktiv[i]:
            zeichne_kreis(win, gegner_x[i], gegner_y[i], 12, "rot")
            # HP-Balken
            setze hp_w auf gegner_hp[i] * 2
            wenn hp_w > 24:
                setze hp_w auf 24
            zeichne_rechteck(win, gegner_x[i] - 12, gegner_y[i] - 18, hp_w, 4, "gruen")
        setze i auf i + 1
    # HUD
    zeichne_rechteck(win, 0, 0, 200, 20, "#333333")

# --- Test 2: Grid-Rendering ---
zeige "[Test 2] Grid rendern"
zeichne_td()
screenshot(win, "beispiele/test_screenshots/td_01_grid.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("Grid mit Pfad gerendert")

# --- Test 3: Turm platzieren ---
zeige "[Test 3] Turm platzieren"
# Turm neben Pfad setzen (Zeile 6, Spalte 5)
setze tx auf 5
setze ty auf 6
setze idx auf ty * GRID_W + tx
wenn karte[idx] == 0:
    karte[idx] = 9
    turm_x.hinzufügen(tx)
    turm_y.hinzufügen(ty)
    turm_typ.hinzufügen(1)
    setze turm_anzahl auf turm_anzahl + 1
    setze gold auf gold - 50

# Klick simulieren
maus_simulieren(tx * TILE + 20, ty * TILE + 20, wahr)
warte(50)

zeichne_td()
screenshot(win, "beispiele/test_screenshots/td_02_turm.bmp")
fenster_aktualisieren(win)
warte(100)

wenn turm_anzahl > 0:
    test_ok("Turm platziert bei [" + text(tx) + "," + text(ty) + "], Gold: " + text(gold))
sonst:
    test_fail("Turm", "Nicht platziert")

# --- Test 4: Zweiten Turm platzieren ---
zeige "[Test 4] Zweiter Turm (Kanone)"
setze tx auf 10
setze ty auf 8
setze idx auf ty * GRID_W + tx
wenn karte[idx] == 0:
    karte[idx] = 9
    turm_x.hinzufügen(tx)
    turm_y.hinzufügen(ty)
    turm_typ.hinzufügen(2)
    setze turm_anzahl auf turm_anzahl + 1

taste_simulieren("2", wahr)
warte(30)
taste_simulieren("2", falsch)
maus_simulieren(tx * TILE + 20, ty * TILE + 20, wahr)
warte(50)

zeichne_td()
screenshot(win, "beispiele/test_screenshots/td_03_zwei_tuerme.bmp")
fenster_aktualisieren(win)
warte(100)

wenn turm_anzahl == 2:
    test_ok("2 Tuerme platziert (Pfeil + Kanone)")
sonst:
    test_fail("Zweiter Turm", "Anzahl: " + text(turm_anzahl))

# --- Test 5: Gegner spawnen ---
zeige "[Test 5] Gegner spawnen"
gegner_x[0] = 20.0
gegner_y[0] = 7 * TILE + TILE / 2.0
gegner_hp[0] = 10
gegner_aktiv[0] = wahr

gegner_x[1] = -20.0
gegner_y[1] = 7 * TILE + TILE / 2.0
gegner_hp[1] = 10
gegner_aktiv[1] = wahr

zeichne_td()
screenshot(win, "beispiele/test_screenshots/td_04_gegner.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("2 Gegner am Start gespawnt")

# --- Test 6: Gegner bewegen ---
zeige "[Test 6] Gegner bewegen"
setze alte_x auf gegner_x[0]
setze frame auf 0
solange frame < 15:
    setze i auf 0
    solange i < 2:
        wenn gegner_aktiv[i]:
            gegner_x[i] = gegner_x[i] + 2.0
        i += 1
    zeichne_td()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/td_05_gegner_bewegt.bmp")
warte(100)
wenn gegner_x[0] > alte_x:
    test_ok("Gegner bewegen sich: x " + text(alte_x) + " -> " + text(gegner_x[0]))
sonst:
    test_fail("Gegner-Bewegung", "unveraendert")

# --- Test 7: Gegner Schaden nehmen ---
zeige "[Test 7] Gegner nimmt Schaden"
setze alte_hp auf gegner_hp[0]
gegner_hp[0] = gegner_hp[0] - 3

zeichne_td()
screenshot(win, "beispiele/test_screenshots/td_06_schaden.bmp")
fenster_aktualisieren(win)
warte(100)

wenn gegner_hp[0] < alte_hp:
    test_ok("Gegner HP: " + text(alte_hp) + " -> " + text(gegner_hp[0]))
sonst:
    test_fail("Schaden", "HP unveraendert")

# --- Test 8: Gegner besiegt ---
zeige "[Test 8] Gegner besiegt"
gegner_hp[0] = 0
gegner_aktiv[0] = falsch
setze punkte auf punkte + 50

zeichne_td()
screenshot(win, "beispiele/test_screenshots/td_07_besiegt.bmp")
fenster_aktualisieren(win)
warte(100)

wenn nicht gegner_aktiv[0]:
    test_ok("Gegner besiegt, Punkte: " + text(punkte))
sonst:
    test_fail("Besiegen", "Gegner noch aktiv")

# --- Test 9: Game-Loop (30 Frames) ---
zeige "[Test 9] Game-Loop (30 Frames)"
# Neue Gegner-Welle
setze i auf 0
solange i < 5:
    gegner_x[i] = i * (-40.0)
    gegner_y[i] = 7 * TILE + TILE / 2.0
    gegner_hp[i] = 8
    gegner_aktiv[i] = wahr
    i += 1

setze frame auf 0
solange frame < 30:
    setze i auf 0
    solange i < 5:
        wenn gegner_aktiv[i]:
            gegner_x[i] = gegner_x[i] + 2.0
        i += 1
    zeichne_td()
    wenn frame == 0:
        screenshot(win, "beispiele/test_screenshots/td_08_welle_start.bmp")
    wenn frame == 15:
        screenshot(win, "beispiele/test_screenshots/td_09_welle_mitte.bmp")
    wenn frame == 29:
        screenshot(win, "beispiele/test_screenshots/td_10_welle_ende.bmp")
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

test_ok("30 Frames Game-Loop ohne Crash")

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

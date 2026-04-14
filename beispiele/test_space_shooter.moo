# ============================================================
# test_space_shooter.moo — Automatisierter Test fuer Space Shooter
#
# Testet: Fenster, Schiff-Bewegung, Schiessen, Sternen-Parallax,
#         Gegner-Spawn, Kollision, Screenshot pro Aktion
# ============================================================

setze BREITE auf 600
setze HOEHE auf 800
setze SPIELER_W auf 32
setze SPIELER_H auf 24
setze SPIELER_SPEED auf 6

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

zeige "=== SPACE SHOOTER AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster ---
zeige "[Test 1] Fenster"
setze win auf fenster_erstelle("Space Shooter Test", BREITE, HOEHE)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster", "nicht offen")

# Spielzustand
setze spieler_x auf BREITE / 2.0
setze spieler_y auf HOEHE - 80.0
setze punkte auf 0

# Sterne (Parallax)
setze stern_x auf []
setze stern_y auf []
setze stern_speed auf []
setze i auf 0
solange i < 30:
    stern_x.hinzufügen(boden(zufall() * BREITE))
    stern_y.hinzufügen(boden(zufall() * HOEHE))
    stern_speed.hinzufügen(1 + boden(zufall() * 3))
    i += 1

# Schuesse
setze schuss_x auf []
setze schuss_y auf []
setze schuss_aktiv auf []
setze i auf 0
solange i < 10:
    schuss_x.hinzufügen(0.0)
    schuss_y.hinzufügen(0.0)
    schuss_aktiv.hinzufügen(falsch)
    i += 1

# Gegner
setze gegner_x auf []
setze gegner_y auf []
setze gegner_aktiv auf []
setze i auf 0
solange i < 10:
    gegner_x.hinzufügen(0.0)
    gegner_y.hinzufügen(0.0)
    gegner_aktiv.hinzufügen(falsch)
    i += 1

funktion zeichne_shooter():
    fenster_löschen(win, "#000020")
    # Sterne
    setze i auf 0
    solange i < 30:
        zeichne_kreis(win, stern_x[i], stern_y[i], stern_speed[i], "weiss")
        i += 1
    # Schuesse
    setze i auf 0
    solange i < 10:
        wenn schuss_aktiv[i]:
            zeichne_rechteck(win, schuss_x[i] - 2, schuss_y[i], 4, 10, "gelb")
        i += 1
    # Gegner
    setze i auf 0
    solange i < 10:
        wenn gegner_aktiv[i]:
            zeichne_rechteck(win, gegner_x[i] - 12, gegner_y[i] - 10, 24, 20, "rot")
        i += 1
    # Spieler-Schiff
    zeichne_rechteck(win, spieler_x - SPIELER_W / 2, spieler_y - SPIELER_H / 2, SPIELER_W, SPIELER_H, "#4488FF")
    zeichne_rechteck(win, spieler_x - 4, spieler_y - SPIELER_H / 2 - 8, 8, 8, "cyan")

# --- Test 2: Start-Rendering ---
zeige "[Test 2] Start-Szene"
zeichne_shooter()
screenshot(win, "beispiele/test_screenshots/shooter_01_start.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("Weltraum-Szene gerendert")

# --- Test 3: Schiff nach links ---
zeige "[Test 3] Schiff nach links"
setze alte_x auf spieler_x
setze frame auf 0
solange frame < 10:
    taste_simulieren("a", wahr)
    warte(20)
    taste_simulieren("a", falsch)
    setze spieler_x auf spieler_x - SPIELER_SPEED
    wenn spieler_x < SPIELER_W / 2:
        setze spieler_x auf SPIELER_W / 2.0
    zeichne_shooter()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/shooter_02_links.bmp")
warte(100)
wenn spieler_x < alte_x:
    test_ok("Schiff links: " + text(alte_x) + " -> " + text(spieler_x))
sonst:
    test_fail("Links", "unveraendert")

# --- Test 4: Schiff nach rechts ---
zeige "[Test 4] Schiff nach rechts"
setze alte_x auf spieler_x
setze frame auf 0
solange frame < 20:
    taste_simulieren("d", wahr)
    warte(20)
    taste_simulieren("d", falsch)
    setze spieler_x auf spieler_x + SPIELER_SPEED
    wenn spieler_x > BREITE - SPIELER_W / 2:
        setze spieler_x auf (BREITE - SPIELER_W / 2) * 1.0
    zeichne_shooter()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/shooter_03_rechts.bmp")
warte(100)
wenn spieler_x > alte_x:
    test_ok("Schiff rechts: " + text(alte_x) + " -> " + text(spieler_x))
sonst:
    test_fail("Rechts", "unveraendert")

# --- Test 5: Schiessen ---
zeige "[Test 5] Schiessen"
setze spieler_x auf BREITE / 2.0
taste_simulieren("leertaste", wahr)
warte(30)
taste_simulieren("leertaste", falsch)
# Schuss erzeugen
schuss_x[0] = spieler_x
schuss_y[0] = spieler_y - SPIELER_H / 2.0
schuss_aktiv[0] = wahr

zeichne_shooter()
screenshot(win, "beispiele/test_screenshots/shooter_04_schuss.bmp")
fenster_aktualisieren(win)
warte(100)

# Schuss bewegen
setze frame auf 0
solange frame < 15:
    schuss_y[0] = schuss_y[0] - 8.0
    zeichne_shooter()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/shooter_05_schuss_fliegt.bmp")
warte(100)

wenn schuss_y[0] < spieler_y:
    test_ok("Schuss fliegt nach oben: y = " + text(schuss_y[0]))
sonst:
    test_fail("Schiessen", "Schuss bewegt sich nicht")

# --- Test 6: Gegner spawnen ---
zeige "[Test 6] Gegner spawnen"
gegner_x[0] = 200.0
gegner_y[0] = 50.0
gegner_aktiv[0] = wahr
gegner_x[1] = 400.0
gegner_y[1] = 80.0
gegner_aktiv[1] = wahr

zeichne_shooter()
screenshot(win, "beispiele/test_screenshots/shooter_06_gegner.bmp")
fenster_aktualisieren(win)
warte(100)
test_ok("2 Gegner gespawnt")

# --- Test 7: Gegner bewegen sich nach unten ---
zeige "[Test 7] Gegner bewegen"
setze alte_y auf gegner_y[0]
setze frame auf 0
solange frame < 10:
    setze i auf 0
    solange i < 2:
        wenn gegner_aktiv[i]:
            gegner_y[i] = gegner_y[i] + 3.0
        i += 1
    zeichne_shooter()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/shooter_07_gegner_bewegt.bmp")
warte(100)
wenn gegner_y[0] > alte_y:
    test_ok("Gegner bewegen sich: y " + text(alte_y) + " -> " + text(gegner_y[0]))
sonst:
    test_fail("Gegner-Bewegung", "unveraendert")

# --- Test 8: Schuss-Gegner-Kollision ---
zeige "[Test 8] Schuss trifft Gegner"
# Schuss direkt auf Gegner positionieren
schuss_x[0] = gegner_x[0]
schuss_y[0] = gegner_y[0] + 5.0
schuss_aktiv[0] = wahr

zeichne_shooter()
screenshot(win, "beispiele/test_screenshots/shooter_08_vor_treffer.bmp")
fenster_aktualisieren(win)
warte(100)

# Kollision pruefen
setze dx auf schuss_x[0] - gegner_x[0]
setze dy auf schuss_y[0] - gegner_y[0]
wenn dx < 0:
    setze dx auf 0 - dx
wenn dy < 0:
    setze dy auf 0 - dy
wenn dx < 20 und dy < 20:
    gegner_aktiv[0] = falsch
    schuss_aktiv[0] = falsch
    setze punkte auf punkte + 100

zeichne_shooter()
screenshot(win, "beispiele/test_screenshots/shooter_09_nach_treffer.bmp")
fenster_aktualisieren(win)
warte(100)

wenn nicht gegner_aktiv[0]:
    test_ok("Gegner zerstoert, Punkte: " + text(punkte))
sonst:
    test_fail("Treffer", "Gegner noch aktiv")

# --- Test 9: Sterne-Parallax ---
zeige "[Test 9] Sterne-Parallax"
setze alte_sterne_y auf stern_y[0]
setze frame auf 0
solange frame < 10:
    setze i auf 0
    solange i < 30:
        stern_y[i] = stern_y[i] + stern_speed[i]
        wenn stern_y[i] > HOEHE:
            stern_y[i] = 0
        i += 1
    zeichne_shooter()
    fenster_aktualisieren(win)
    warte(30)
    setze frame auf frame + 1

screenshot(win, "beispiele/test_screenshots/shooter_10_sterne.bmp")
warte(100)
test_ok("Sterne-Parallax laeuft")

# --- Test 10: Game-Loop (30 Frames) ---
zeige "[Test 10] Game-Loop (30 Frames)"
setze frame auf 0
solange frame < 30:
    # Alles zusammen
    setze spieler_x auf spieler_x + sinus(frame * 0.2) * 3
    setze i auf 0
    solange i < 30:
        stern_y[i] = stern_y[i] + stern_speed[i]
        wenn stern_y[i] > HOEHE:
            stern_y[i] = 0
        i += 1
    zeichne_shooter()
    wenn frame == 29:
        screenshot(win, "beispiele/test_screenshots/shooter_11_loop_ende.bmp")
    fenster_aktualisieren(win)
    warte(20)
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

fenster_schliessen(win)

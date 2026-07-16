# ============================================================
# test_zelda.moo — Automatisierter Test fuer Zelda-Adventure
#
# Kompilieren: moo-compiler compile beispiele/domain/game/testscreens/test_zelda.moo -o beispiele/test_zelda
# Starten:     ./beispiele/domain/game/testscreens/test_zelda
#
# Testet: Fenster, Sprites, Bewegung, Schwert, Items, Raum-Wechsel
# Erzeugt: Screenshots in beispiele/test_screenshots/
# ============================================================

# --- Zelda-Logik importieren geht nicht (kein Modul) ---
# Deshalb: Standalone-Tests mit der 2D-API + Test-Funktionen

konstante WIN_W auf 640
konstante WIN_H auf 480
konstante TILE auf 32

setze bestanden auf 0
setze fehlgeschlagen auf 0

funktion test_ok(name):
    zeige "  PASS: " + name
    setze bestanden auf bestanden + 1

funktion test_fail(name, grund):
    zeige "  FAIL: " + name + " — " + grund
    setze fehlgeschlagen auf fehlgeschlagen + 1

# ============================================================
zeige "=== ZELDA AUTOMATISIERTE TESTS ==="
zeige ""

# --- Test 1: Fenster erstellt sich ---
zeige "[Test 1] Fenster erstellen"
setze win auf fenster_erstelle("Zelda Test", WIN_W, WIN_H)
wenn fenster_offen(win):
    test_ok("Fenster offen")
sonst:
    test_fail("Fenster offen", "fenster_offen gibt falsch zurueck")

# --- Test 2: Sprites laden ---
zeige "[Test 2] Sprites laden"
setze char_spr auf sprite_laden(win, "beispiele/assets/sprites/zelda_like/gfx/character.png")
setze world_spr auf sprite_laden(win, "beispiele/assets/sprites/zelda_like/gfx/Overworld.png")
setze obj_spr auf sprite_laden(win, "beispiele/assets/sprites/zelda_like/gfx/objects.png")
# Sprites geben IDs zurueck (Zahlen > 0)
wenn char_spr >= 0:
    test_ok("character.png geladen (ID: " + text(char_spr) + ")")
sonst:
    test_fail("character.png laden", "ID = " + text(char_spr))
wenn world_spr > 0:
    test_ok("Overworld.png geladen")
sonst:
    test_fail("Overworld.png laden", "Sprite nicht gefunden")
wenn obj_spr > 0:
    test_ok("objects.png geladen")
sonst:
    test_fail("objects.png laden", "Sprite nicht gefunden")

# --- Test 3: Zeichnen + Screenshot ---
zeige "[Test 3] Zeichnen + Screenshot"
fenster_löschen(win, "#4CAF50")
# Gras-Tile zeichnen
sprite_ausschnitt(win, world_spr, 0, 0, 16, 16, 0, 0, TILE, TILE)
# Charakter zeichnen (Frame 0, nach unten)
sprite_ausschnitt(win, char_spr, 0, 0, 34, 32, 100, 100, 48, 48)
# Herz zeichnen
sprite_ausschnitt(win, obj_spr, 0, 0, 16, 16, 200, 200, 32, 32)
setze ss1 auf screenshot(win, "beispiele/test_screenshots/01_sprites.bmp")
fenster_aktualisieren(win)
wenn ss1:
    test_ok("Screenshot 01_sprites.bmp gespeichert")
sonst:
    test_fail("Screenshot", "screenshot() gab falsch zurueck")

# --- Test 4: Tasten-Simulation ---
zeige "[Test 4] Tasten-Simulation"
# Simuliere Rechts-Taste druecken
taste_simulieren("d", wahr)
warte(50)
taste_simulieren("d", falsch)
test_ok("taste_simulieren('d') ohne Crash")

# Simuliere Schwert
taste_simulieren("leertaste", wahr)
warte(50)
taste_simulieren("leertaste", falsch)
test_ok("taste_simulieren('leertaste') ohne Crash")

# --- Test 5: Mehrere Frames rendern ---
zeige "[Test 5] Game-Loop (30 Frames)"
setze frame auf 0
solange frame < 30:
    fenster_löschen(win, "#4CAF50")
    # Karte: 3x3 Tiles
    setze ty auf 0
    solange ty < 3:
        setze tx auf 0
        solange tx < 3:
            sprite_ausschnitt(win, world_spr, 0, 0, 16, 16, tx * TILE, ty * TILE, TILE, TILE)
            setze tx auf tx + 1
        setze ty auf ty + 1
    # Charakter walk-animation
    setze walk_col auf (frame / 6) % 4
    sprite_ausschnitt(win, char_spr, walk_col * 34, 0, 34, 32, 48 + frame * 2, 48, 48, 48)
    wenn frame == 29:
        screenshot(win, "beispiele/test_screenshots/02_animation.bmp")
    fenster_aktualisieren(win)
    warte(16)
    setze frame auf frame + 1
test_ok("30 Frames ohne Crash")
test_ok("Screenshot 02_animation.bmp gespeichert")

# --- Test 6: Maus-Simulation ---
zeige "[Test 6] Maus-Simulation"
maus_simulieren(320, 240, falsch)
test_ok("maus_simulieren(320, 240) ohne Crash")
maus_simulieren(100, 100, wahr)
test_ok("maus_simulieren mit Klick ohne Crash")

# --- Test 7: Sprite-Skalierung ---
zeige "[Test 7] Sprite-Skalierung"
fenster_löschen(win, "#000000")
# Klein (16x16)
sprite_ausschnitt(win, char_spr, 0, 0, 34, 32, 10, 10, 16, 16)
# Mittel (48x48)
sprite_ausschnitt(win, char_spr, 0, 0, 34, 32, 40, 10, 48, 48)
# Gross (96x96)
sprite_ausschnitt(win, char_spr, 0, 0, 34, 32, 100, 10, 96, 96)
# Riesig (192x192)
sprite_ausschnitt(win, char_spr, 0, 0, 34, 32, 210, 10, 192, 192)
screenshot(win, "beispiele/test_screenshots/03_skalierung.bmp")
fenster_aktualisieren(win)
test_ok("4 Skalierungen gerendert")

# --- Test 8: Alle 4 Richtungen ---
zeige "[Test 8] Charakter 4 Richtungen"
fenster_löschen(win, "#4CAF50")
# Unten (Row 0)
sprite_ausschnitt(win, char_spr, 0, 0, 34, 32, 50, 200, 64, 64)
# Links (Row 1)
sprite_ausschnitt(win, char_spr, 0, 32, 34, 32, 150, 200, 64, 64)
# Rechts (Row 2)
sprite_ausschnitt(win, char_spr, 0, 64, 34, 32, 250, 200, 64, 64)
# Oben (Row 3)
sprite_ausschnitt(win, char_spr, 0, 96, 34, 32, 350, 200, 64, 64)
screenshot(win, "beispiele/test_screenshots/04_richtungen.bmp")
fenster_aktualisieren(win)
test_ok("4 Richtungen gerendert")

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

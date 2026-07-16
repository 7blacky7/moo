# ============================================================
# dungeon_selftest.moo — Gameplay-Selftest für dungeon.moo
#
# White-Box-Test über das Zustands-Dict S: da der Dungeon
# prozedural ist, wird der Spieler gezielt neben Treppen/Gegner
# gesetzt (deterministischer Seed 777) und spiel_schritt(inp)
# direkt getrieben. Geprüft werden:
#   1. Start-Invarianten + Spieler-Spawn im ersten Raum
#      (Regression für den alten Globals-Shadowing-Bug)
#   2. Level-Progression über die Treppe bis Level 10 -> SIEG
#   3. Kampf: Bump-Angriff tötet Gegner, Score steigt
#   4. Tod durch Gegner -> GAME OVER -> R-Neustart
#   5. Render-Stichproben (test_frame_region/test_frame_diff)
#      + Screenshots (PNG) + Playthrough-GIF
#
# Start (Pflicht-Env unterdrückt den interaktiven Loop):
#   DUNGEON_SELFTEST=1 xvfb-run -a -s "-screen 0 800x600x24" \
#     ./compiler/target/release/moo-compiler run beispiele/dungeon_selftest.moo
# Artefakt-Ordner via env SELFTEST_OUT (Default /tmp).
# ============================================================

importiere dungeon

setze out auf umgebung("SELFTEST_OUT")
wenn out == nichts:
    setze out auf "/tmp"

setze fehler auf 0

funktion pruefe(name, ok):
    wenn ok:
        zeige "ASSERT OK   " + name
        gib_zurück 0
    zeige "ASSERT FAIL " + name
    gib_zurück 1

funktion inp_leer():
    setze d auf {}
    d["hoch"] = falsch
    d["runter"] = falsch
    d["links"] = falsch
    d["rechts"] = falsch
    d["neustart"] = falsch
    gib_zurück d

funktion zug(taste):
    # Ein Turn + Cooldown ablaufen lassen
    setze d auf inp_leer()
    d[taste] = wahr
    spiel_schritt(d)
    setze j auf 0
    solange j < 6:
        spiel_schritt(inp_leer())
        setze j auf j + 1

funktion gegner_deaktivieren():
    setze gi auf 0
    solange gi < MAX_GEGNER:
        S["geg_aktiv"][gi] = falsch
        setze gi auf gi + 1

zeige "=== dungeon Gameplay-Selftest ==="
setze win auf fenster_erstelle("dungeon selftest", BREITE, HOEHE)

# ------------------------------------------------------------
# Test 1: Start-Invarianten + Spawn im ersten Raum
# ------------------------------------------------------------
neustart()
setze fehler auf fehler + pruefe("start: level 1", S["level"] == 1)
setze fehler auf fehler + pruefe("start: 30 hp", S["spieler_hp"] == 30)
setze fehler auf fehler + pruefe("start: laeuft", S["modus"] == 0)
setze fehler auf fehler + pruefe("start: mehrere raeume generiert", länge(S["raum_x"]) > 1)
setze fehler auf fehler + pruefe("start: spieler steht auf boden", karte_get(S["spieler_x"], S["spieler_y"]) == BODEN)
setze im_raum0 auf S["spieler_x"] >= S["raum_x"][0] und S["spieler_x"] < S["raum_x"][0] + S["raum_w"][0] und S["spieler_y"] >= S["raum_y"][0] und S["spieler_y"] < S["raum_y"][0] + S["raum_h"][0]
setze fehler auf fehler + pruefe("start: spawn im ersten raum (shadowing-regression)", im_raum0)

# Render-Stichproben + Start-Screenshot
welt_zeichnen(win)
setze f_start auf test_frame_grab(win)
setze r_hud auf test_frame_region(f_start, 400, 8, 60, 12)
setze fehler auf fehler + pruefe("hud-hintergrund (region)", r_hud["rot"] == 27 und r_hud["gruen"] == 38 und r_hud["blau"] == 49)
test_frame_save_png(f_start, out + "/dungeon_01_start.png")
zeige "screenshot: " + out + "/dungeon_01_start.png"
fenster_aktualisieren(win)

# ------------------------------------------------------------
# Test 2: GIF-Aufnahme + Bewegung ändert das Bild
# ------------------------------------------------------------
welt_zeichnen(win)
setze gif auf test_gif_start(win, out + "/dungeon_playthrough.gif", 8)
zug("rechts")
welt_zeichnen(win)
setze f_bewegt auf test_frame_grab(win)
setze d auf test_frame_diff(f_start, f_bewegt)
setze fehler auf fehler + pruefe("bewegung aendert frame (diff > 0)", d["geaenderte_pixel"] > 0)
test_gif_frame(gif, win)
fenster_aktualisieren(win)

# ------------------------------------------------------------
# Test 3: Level-Progression bis SIEG (Treppe, Gegner deaktiviert)
# ------------------------------------------------------------
setze runde auf 0
solange S["modus"] == 0 und runde < 12:
    gegner_deaktivieren()
    setze vorher_level auf S["level"]
    S["spieler_x"] = S["treppe_x"] - 1
    S["spieler_y"] = S["treppe_y"]
    zug("rechts")
    wenn S["modus"] == 0:
        wenn S["level"] != vorher_level + 1:
            setze fehler auf fehler + pruefe("level-aufstieg in runde " + text(runde), falsch)
            setze runde auf 12
        sonst:
            # Regression: Spieler wurde ins neue Level versetzt (nicht auf alter Treppe)
            wenn karte_get(S["spieler_x"], S["spieler_y"]) != BODEN:
                setze fehler auf fehler + pruefe("spawn nach levelwechsel auf boden", falsch)
                setze runde auf 12
    welt_zeichnen(win)
    test_gif_frame(gif, win)
    fenster_aktualisieren(win)
    setze runde auf runde + 1
setze fehler auf fehler + pruefe("SIEG nach level 10", S["modus"] == 2)

welt_zeichnen(win)
setze f_sieg auf test_frame_grab(win)
setze r_sieg auf test_frame_region(f_sieg, 120, 250, 40, 40)
setze fehler auf fehler + pruefe("sieg-overlay (region)", r_sieg["rot"] == 62 und r_sieg["gruen"] == 47 und r_sieg["blau"] == 0)
test_frame_save_png(f_sieg, out + "/dungeon_02_sieg.png")
zeige "screenshot: " + out + "/dungeon_02_sieg.png"
test_gif_frame(gif, win)
test_gif_ende(gif)
zeige "gif: " + out + "/dungeon_playthrough.gif"
fenster_aktualisieren(win)

# ------------------------------------------------------------
# Test 4: Neustart + Kampf (Bump-Angriff tötet Gegner)
# ------------------------------------------------------------
setze d auf inp_leer()
d["neustart"] = wahr
spiel_schritt(d)
setze fehler auf fehler + pruefe("neustart nach sieg", S["modus"] == 0 und S["level"] == 1)

# Ersten Gegner mit freiem Feld links suchen
setze ziel auf -1
setze gi auf 0
solange gi < MAX_GEGNER:
    wenn ziel < 0 und S["geg_aktiv"][gi] und karte_get(S["geg_x"][gi] - 1, S["geg_y"][gi]) == BODEN:
        setze ziel auf gi
    setze gi auf gi + 1
setze fehler auf fehler + pruefe("gegner vorhanden", ziel >= 0)
wenn ziel >= 0:
    S["spieler_x"] = S["geg_x"][ziel] - 1
    S["spieler_y"] = S["geg_y"][ziel]
    setze score_vorher auf S["score"]
    setze schlaege auf 0
    solange S["geg_aktiv"][ziel] und schlaege < 20 und S["modus"] == 0:
        # Gegner darf sich nicht wegbewegen: Angriff = in ihn laufen
        S["spieler_x"] = S["geg_x"][ziel] - 1
        S["spieler_y"] = S["geg_y"][ziel]
        zug("rechts")
        setze schlaege auf schlaege + 1
    setze fehler auf fehler + pruefe("gegner besiegt (bump-angriff)", S["geg_aktiv"][ziel] == falsch)
    setze fehler auf fehler + pruefe("score gestiegen", S["score"] > score_vorher)
    setze fehler auf fehler + pruefe("kampf hat hp gekostet oder ueberlebt", S["spieler_hp"] <= 30 und S["modus"] == 0)

# ------------------------------------------------------------
# Test 5: Tod -> GAME OVER -> R-Neustart
# ------------------------------------------------------------
setze ziel auf -1
setze gi auf 0
solange gi < MAX_GEGNER:
    wenn ziel < 0 und S["geg_aktiv"][gi] und karte_get(S["geg_x"][gi] - 1, S["geg_y"][gi]) == BODEN:
        setze ziel auf gi
    setze gi auf gi + 1
wenn ziel >= 0:
    S["spieler_hp"] = 1
    S["spieler_x"] = S["geg_x"][ziel] - 1
    S["spieler_y"] = S["geg_y"][ziel]
    setze zuege auf 0
    solange S["modus"] == 0 und zuege < 10:
        S["spieler_x"] = S["geg_x"][ziel] - 1
        S["spieler_y"] = S["geg_y"][ziel]
        zug("rechts")
        setze zuege auf zuege + 1
    setze fehler auf fehler + pruefe("tod: game over", S["modus"] == 1)
    welt_zeichnen(win)
    setze f_go auf test_frame_grab(win)
    setze r_go auf test_frame_region(f_go, 120, 250, 40, 40)
    setze fehler auf fehler + pruefe("gameover-overlay (region)", r_go["rot"] == 26 und r_go["gruen"] == 26 und r_go["blau"] == 46)
    test_frame_save_png(f_go, out + "/dungeon_03_gameover.png")
    zeige "screenshot: " + out + "/dungeon_03_gameover.png"
    fenster_aktualisieren(win)
    setze d auf inp_leer()
    d["neustart"] = wahr
    spiel_schritt(d)
    setze fehler auf fehler + pruefe("R-neustart nach tod", S["modus"] == 0 und S["level"] == 1 und S["spieler_hp"] == 30)
sonst:
    setze fehler auf fehler + pruefe("tod-test: gegner vorhanden", falsch)

fenster_schliessen(win)

wenn fehler == 0:
    zeige "SELFTEST_RESULT: PASS dungeon_gameplay"
sonst:
    zeige "SELFTEST_RESULT: FAIL dungeon_gameplay (" + text(fehler) + " Fehler)"

# ============================================================
# brawler_selftest.moo — Gameplay-Selftest für brawler.moo
#
# Prüft: Wellen spawnen wirklich Gegner (Regression für den alten
# gegner_count-Shadowing-Bug, durch den NIE Gegner erschienen),
# Kick besiegt Gegner (+Punkte), Gegner-Treffer kosten HP,
# Durchlauf bis ans Levelende -> SIEG, beide tot -> GAME OVER,
# R-Neustart. White-Box über S (deterministische Logik, kein RNG).
#
# Start:
#   BRAWLER_SELFTEST=1 xvfb-run -a -s "-screen 0 800x600x24" \
#     ./compiler/target/release/moo-compiler run beispiele/brawler_selftest.moo
# ============================================================

importiere brawler

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
    d["links"] = falsch
    d["rechts"] = falsch
    d["hoch"] = falsch
    d["schlag"] = falsch
    d["kick"] = falsch
    d["neustart"] = falsch
    gib_zurück d

funktion gegner_weg():
    setze gi auf 0
    solange gi < länge(S["g_x"]):
        S["g_aktiv"][gi] = falsch
        setze gi auf gi + 1

zeige "=== brawler Gameplay-Selftest ==="
setze win auf fenster_erstelle("brawler selftest", BREITE, HOEHE)
setze spr_pfad auf "beispiele/assets/sprites/kenney_platformer/PNG/Characters/"
setze sprs auf {}
sprs["p1_idle"] = sprite_laden(win, spr_pfad + "platformChar_idle.png")
sprs["p1_walk"] = sprite_laden(win, spr_pfad + "platformChar_walk1.png")
sprs["p1_attack"] = sprite_laden(win, spr_pfad + "platformChar_jump.png")
sprs["p2_idle"] = sprite_laden(win, spr_pfad + "platformChar_happy.png")
sprs["p2_walk"] = sprite_laden(win, spr_pfad + "platformChar_climb1.png")
sprs["p2_attack"] = sprite_laden(win, spr_pfad + "platformChar_duck.png")
sprs["feind"] = sprite_laden(win, spr_pfad + "platformChar_happy.png")

# ------------------------------------------------------------
# Phase 0: Start + Render-Stichprobe
# ------------------------------------------------------------
neustart()
setze fehler auf fehler + pruefe("start: beide 100 hp", S["p1_hp"] == 100 und S["p2_hp"] == 100)
setze fehler auf fehler + pruefe("start: keine gegner", länge(S["g_x"]) == 0)
welt_zeichnen(win, sprs)
setze f0 auf test_frame_grab(win)
setze r_p1 auf test_frame_region(f0, 20, 12, 100, 10)
setze fehler auf fehler + pruefe("p1 hp-balken blau", r_p1["blau"] > 200 und r_p1["rot"] < 120)
test_frame_save_png(f0, out + "/brawler_01_start.png")
zeige "screenshot: " + out + "/brawler_01_start.png"
fenster_aktualisieren(win)

# ------------------------------------------------------------
# Phase 1: Wellen-Spawn-Regression — Kamera vor, Gegner erscheinen
# ------------------------------------------------------------
setze z auf 0
solange länge(S["g_x"]) == 0 und z < 1000:
    setze i1 auf inp_leer()
    i1["rechts"] = wahr
    setze i2 auf inp_leer()
    i2["rechts"] = wahr
    spiel_schritt(i1, i2)
    setze z auf z + 1
setze fehler auf fehler + pruefe("welle spawnt gegner (shadowing-regression)", länge(S["g_x"]) > 0)

# ------------------------------------------------------------
# Phase 2: Kampf — Kick besiegt Gegner, Punkte steigen; Treffer kosten HP
# ------------------------------------------------------------
setze hp_vorher auf S["p1_hp"] + S["p2_hp"]
setze z auf 0
solange S["p1_hp"] + S["p2_hp"] == hp_vorher und z < 600:
    spiel_schritt(inp_leer(), inp_leer())
    setze z auf z + 1
setze fehler auf fehler + pruefe("gegner-treffer kosten hp", S["p1_hp"] + S["p2_hp"] < hp_vorher)

setze punkte_vorher auf S["punkte"]
setze z auf 0
setze besiegt_vorher auf 0
setze gi auf 0
solange gi < länge(S["g_x"]):
    wenn S["g_aktiv"][gi] == falsch:
        setze besiegt_vorher auf besiegt_vorher + 1
    setze gi auf gi + 1
solange S["punkte"] == punkte_vorher und z < 900:
    setze i1 auf inp_leer()
    i1["kick"] = wahr
    setze i2 auf inp_leer()
    i2["kick"] = wahr
    spiel_schritt(i1, i2)
    setze z auf z + 1
setze fehler auf fehler + pruefe("kick besiegt gegner, punkte steigen", S["punkte"] > punkte_vorher)
welt_zeichnen(win, sprs)
setze f_kampf auf test_frame_grab(win)
test_frame_save_png(f_kampf, out + "/brawler_02_kampf.png")
zeige "screenshot: " + out + "/brawler_02_kampf.png"
fenster_aktualisieren(win)

# ------------------------------------------------------------
# Phase 3: Durchlauf bis ans Levelende -> SIEG
# (Gegner werden pro Schritt deaktiviert — Progression isoliert testen)
# ------------------------------------------------------------
setze z auf 0
solange S["modus"] == 0 und z < 6000:
    gegner_weg()
    setze i1 auf inp_leer()
    i1["rechts"] = wahr
    setze i2 auf inp_leer()
    i2["rechts"] = wahr
    spiel_schritt(i1, i2)
    setze z auf z + 1
setze fehler auf fehler + pruefe("SIEG am levelende", S["modus"] == 2)
welt_zeichnen(win, sprs)
setze f_sieg auf test_frame_grab(win)
setze r_sieg auf test_frame_region(f_sieg, 170, 210, 40, 40)
setze fehler auf fehler + pruefe("sieg-overlay (region)", r_sieg["rot"] == 62 und r_sieg["gruen"] == 47 und r_sieg["blau"] == 0)
test_frame_save_png(f_sieg, out + "/brawler_03_sieg.png")
zeige "screenshot: " + out + "/brawler_03_sieg.png"
fenster_aktualisieren(win)

# ------------------------------------------------------------
# Phase 4: R-Neustart, dann beide tot -> GAME OVER
# ------------------------------------------------------------
setze i1 auf inp_leer()
i1["neustart"] = wahr
spiel_schritt(i1, inp_leer())
setze fehler auf fehler + pruefe("neustart nach sieg", S["modus"] == 0 und S["p1_hp"] == 100)

S["p1_hp"] = 1
S["p2_hp"] = 1
# Ohne Kamerabewegung spawnt keine Welle — Gegner direkt daneben setzen
gegner_spawnen(S["p1_x"] + 30, 0)
gegner_spawnen(S["p2_x"] + 30, 0)
setze z auf 0
solange S["modus"] == 0 und z < 2000:
    spiel_schritt(inp_leer(), inp_leer())
    setze z auf z + 1
setze fehler auf fehler + pruefe("beide tot: game over", S["modus"] == 1)
setze i1 auf inp_leer()
i1["neustart"] = wahr
spiel_schritt(i1, inp_leer())
setze fehler auf fehler + pruefe("R-neustart nach game over", S["modus"] == 0 und S["p1_hp"] == 100 und S["p2_hp"] == 100)

fenster_schliessen(win)

wenn fehler == 0:
    zeige "SELFTEST_RESULT: PASS brawler_gameplay"
sonst:
    zeige "SELFTEST_RESULT: FAIL brawler_gameplay (" + text(fehler) + " Fehler)"

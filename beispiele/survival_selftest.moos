# ============================================================
# survival_selftest.moo — Gameplay-Selftest für survival.moo
#
# White-Box über S (prozedurale Welt): Tiles/Monster werden gezielt
# platziert, Zeit wird vorgespult. Geprüft: Abbauen, Crafting,
# Kampf (+Nahrungs-Drop), Monster-Schaden, Hunger-Tod -> GAME OVER,
# R-Neustart, 3 Nächte überstehen -> SIEG, Render-Stichproben.
#
# Start:
#   SURVIVAL_SELFTEST=1 xvfb-run -a -s "-screen 0 800x600x24" \
#     ./compiler/target/release/moo-compiler run beispiele/survival_selftest.moo
# ============================================================

importiere survival

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
    d["aktion"] = falsch
    d["mauer"] = falsch
    d["craft"] = falsch
    d["w1"] = falsch
    d["w2"] = falsch
    d["w3"] = falsch
    d["neustart"] = falsch
    gib_zurück d

funktion zug(taste):
    setze d auf inp_leer()
    wenn taste != "":
        d[taste] = wahr
    spiel_schritt(d)
    setze j auf 0
    solange j < 10:
        spiel_schritt(inp_leer())
        setze j auf j + 1

zeige "=== survival Gameplay-Selftest ==="
setze win auf fenster_erstelle("survival selftest", BREITE, HOEHE)

# ------------------------------------------------------------
# Test 1: Start-Invarianten + Render
# ------------------------------------------------------------
neustart()
setze fehler auf fehler + pruefe("start: 100 hp", S["spieler_hp"] == 100)
setze fehler auf fehler + pruefe("start: 0 naechte", S["naechte"] == 0)
setze fehler auf fehler + pruefe("start: laeuft", S["modus"] == 0)
setze fehler auf fehler + pruefe("start: spieler auf begehbarem tile", ist_begehbar(S["spieler_x"], S["spieler_y"]))
welt_zeichnen(win)
setze f0 auf test_frame_grab(win)
setze r_hud auf test_frame_region(f0, 500, 570, 40, 20)
setze fehler auf fehler + pruefe("hud-hintergrund (region)", r_hud["rot"] == 27 und r_hud["gruen"] == 38 und r_hud["blau"] == 49)
test_frame_save_png(f0, out + "/survival_01_start.png")
zeige "screenshot: " + out + "/survival_01_start.png"
fenster_aktualisieren(win)

# ------------------------------------------------------------
# Test 2: Abbauen + Crafting (Baum -> Holz, 5 Holz -> Axt)
# ------------------------------------------------------------
setze holz_vorher auf S["inv_holz"]
welt_set(S["spieler_x"] + 1, S["spieler_y"], BAUM)
zug("rechts")
zug("aktion")
setze fehler auf fehler + pruefe("baum abgebaut: +1 holz", S["inv_holz"] == holz_vorher + 1)
S["inv_holz"] = 5
zug("craft")
setze fehler auf fehler + pruefe("crafting: axt aus 5 holz", S["hat_axt"] und S["inv_holz"] == 0)
# Mit Axt: Baum gibt 3 Holz
welt_set(S["spieler_x"] + 1, S["spieler_y"], BAUM)
zug("aktion")
setze fehler auf fehler + pruefe("axt: baum gibt 3 holz", S["inv_holz"] == 3)

# ------------------------------------------------------------
# Test 3: Kampf — Monster totschlagen (+Nahrung), Kontakt kostet HP
# ------------------------------------------------------------
S["mon_x"][0] = S["spieler_x"] + 1
S["mon_y"][0] = S["spieler_y"]
S["mon_hp"][0] = 15
S["mon_aktiv"][0] = wahr
setze nahrung_vorher auf S["inv_nahrung"]
setze z auf 0
solange S["mon_aktiv"][0] und z < 20:
    S["mon_x"][0] = S["spieler_x"] + 1
    S["mon_y"][0] = S["spieler_y"]
    zug("aktion")
    setze z auf z + 1
setze fehler auf fehler + pruefe("monster besiegt", S["mon_aktiv"][0] == falsch)
setze fehler auf fehler + pruefe("monster droppt nahrung", S["inv_nahrung"] == nahrung_vorher + 3)

setze hp_vorher auf S["spieler_hp"]
# Monster so setzen, dass es NACH dem Rechts-Schritt adjazent ist
S["mon_x"][1] = S["spieler_x"] + 2
S["mon_y"][1] = S["spieler_y"]
S["mon_hp"][1] = 15
S["mon_aktiv"][1] = wahr
zug("rechts")
S["mon_aktiv"][1] = falsch
setze fehler auf fehler + pruefe("monster-kontakt kostet hp", S["spieler_hp"] < hp_vorher)

# ------------------------------------------------------------
# Test 4: 3 Nächte überstehen -> SIEG (Zeit vorspulen)
# ------------------------------------------------------------
setze runde auf 0
solange S["modus"] == 0 und runde < 5:
    S["tageszeit"] = 0.9
    spiel_schritt(inp_leer())
    S["tageszeit"] = 0.3
    spiel_schritt(inp_leer())
    setze runde auf runde + 1
setze fehler auf fehler + pruefe("SIEG nach 3 naechten", S["modus"] == 2)
setze fehler auf fehler + pruefe("genau 3 naechte gezaehlt", S["naechte"] == 3)
welt_zeichnen(win)
setze f_sieg auf test_frame_grab(win)
setze r_sieg auf test_frame_region(f_sieg, 170, 210, 40, 40)
setze fehler auf fehler + pruefe("sieg-overlay (region)", r_sieg["rot"] == 62 und r_sieg["gruen"] == 47 und r_sieg["blau"] == 0)
test_frame_save_png(f_sieg, out + "/survival_02_sieg.png")
zeige "screenshot: " + out + "/survival_02_sieg.png"
fenster_aktualisieren(win)

# ------------------------------------------------------------
# Test 5: R-Neustart, Hunger-Tod -> GAME OVER, R-Neustart
# ------------------------------------------------------------
setze d auf inp_leer()
d["neustart"] = wahr
spiel_schritt(d)
setze fehler auf fehler + pruefe("neustart nach sieg", S["modus"] == 0 und S["naechte"] == 0)

S["spieler_hp"] = 1.0
S["spieler_hunger"] = 0.0
S["inv_nahrung"] = 0
setze z auf 0
solange S["modus"] == 0 und z < 100:
    spiel_schritt(inp_leer())
    setze z auf z + 1
setze fehler auf fehler + pruefe("verhungert: game over", S["modus"] == 1)
welt_zeichnen(win)
setze f_go auf test_frame_grab(win)
test_frame_save_png(f_go, out + "/survival_03_gameover.png")
zeige "screenshot: " + out + "/survival_03_gameover.png"
fenster_aktualisieren(win)
setze d auf inp_leer()
d["neustart"] = wahr
spiel_schritt(d)
setze fehler auf fehler + pruefe("R-neustart nach tod", S["modus"] == 0 und S["spieler_hp"] == 100)

fenster_schliessen(win)

wenn fehler == 0:
    zeige "SELFTEST_RESULT: PASS survival_gameplay"
sonst:
    zeige "SELFTEST_RESULT: FAIL survival_gameplay (" + text(fehler) + " Fehler)"

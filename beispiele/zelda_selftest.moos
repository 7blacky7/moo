# ============================================================
# zelda_selftest.moo — Gameplay-Selftest für zelda.moo
#
# Spielt das ECHTE Spiel (import + spiel_schritt) mit einer
# deterministischen Input-Sequenz durch:
#   1. Tod-Pfad: 3 Gegner-Treffer ohne Gegenwehr -> GAME OVER
#   2. Neustart per R
#   3. Sieg-Pfad: Wache besiegen -> Schlüssel -> verschlossene
#      Tür -> Boss (5 Treffer) -> Triforce -> SIEG
# plus Zustands-Asserts, test_pixel-Stichproben und Screenshots.
#
# WICHTIG: test_pixel/test_screenshot lesen den Render-Backbuffer
# und laufen deshalb VOR fenster_aktualisieren.
#
# Start (Pflicht-Env unterdrückt den interaktiven Loop des Spiels):
#   ZELDA_SELFTEST=1 xvfb-run -a -s "-screen 0 800x600x24" \
#     ./compiler/target/release/moo-compiler run beispiele/zelda_selftest.moo
# Artefakt-Ordner via env SELFTEST_OUT (Default /tmp).
# ============================================================

importiere zelda

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

funktion pruefe_farbe(name, p, r_soll, g_soll, b_soll, tol):
    setze dr auf p["rot"] - r_soll
    wenn dr < 0:
        setze dr auf 0 - dr
    setze dg auf p["gruen"] - g_soll
    wenn dg < 0:
        setze dg auf 0 - dg
    setze db auf p["blau"] - b_soll
    wenn db < 0:
        setze db auf 0 - db
    wenn dr <= tol und dg <= tol und db <= tol:
        zeige "ASSERT OK   " + name
        gib_zurück 0
    zeige "ASSERT FAIL " + name + " = (" + text(p["rot"]) + "," + text(p["gruen"]) + "," + text(p["blau"]) + ") erwartet (" + text(r_soll) + "," + text(g_soll) + "," + text(b_soll) + ")"
    gib_zurück 1

funktion inp_leer():
    setze d auf {}
    d["hoch"] = falsch
    d["runter"] = falsch
    d["links"] = falsch
    d["rechts"] = falsch
    d["schwert"] = falsch
    d["neustart"] = falsch
    gib_zurück d

funktion tue(taste, n):
    setze j auf 0
    solange j < n:
        setze d auf inp_leer()
        wenn taste != "":
            d[taste] = wahr
        spiel_schritt(d)
        setze j auf j + 1

funktion foto(win, name):
    welt_zeichnen(win)
    setze pfad auf out + "/" + name + ".bmp"
    test_screenshot(win, pfad)
    zeige "screenshot: " + pfad
    fenster_aktualisieren(win)

zeige "=== zelda Gameplay-Selftest ==="
setze win auf fenster_erstelle("zelda selftest", WIN_W, WIN_H)

# ------------------------------------------------------------
# Phase 0: Startzustand
# ------------------------------------------------------------
neustart()
setze fehler auf fehler + pruefe("start: raum 0", S["raum_nr"] == 0)
setze fehler auf fehler + pruefe("start: 3 leben", S["leben"] == 3)
setze fehler auf fehler + pruefe("start: kein schluessel", S["hat_key"] == falsch)

# Pixel-Stichproben der Start-Szene (vor Present!)
welt_zeichnen(win)
setze p auf test_pixel(win, 4, 4)
setze fehler auf fehler + pruefe_farbe("hud-hintergrund", p, 26, 26, 46, 6)
setze p auf test_pixel(win, 10, 44)
setze fehler auf fehler + pruefe_farbe("herz voll", p, 229, 57, 53, 8)
setze p auf test_pixel(win, 4, 68)
setze fehler auf fehler + pruefe_farbe("wand", p, 93, 64, 55, 8)
setze p auf test_pixel(win, 320, 272)
setze fehler auf fehler + pruefe_farbe("boden", p, 47, 107, 47, 8)
setze pfad auf out + "/zelda_01_start.bmp"
test_screenshot(win, pfad)
zeige "screenshot: " + pfad
fenster_aktualisieren(win)

# ------------------------------------------------------------
# Phase 1: Tod-Pfad — in den Hub laufen und sich treffen lassen
# ------------------------------------------------------------
tue("rechts", 240)
setze fehler auf fehler + pruefe("tod-pfad: hub erreicht", S["raum_nr"] == 1)
# Durch die Busch-Lücke (Spalte 8) hoch zum Patrouillen-Korridor (y=96)
tue("hoch", 33)
setze z auf 0
solange S["modus"] == 0 und z < 2000:
    tue("", 1)
    setze z auf z + 1
setze fehler auf fehler + pruefe("tod-pfad: game over", S["modus"] == 1)
setze fehler auf fehler + pruefe("tod-pfad: 0 leben", S["leben"] == 0)
foto(win, "zelda_02_gameover")

# Neustart per R
tue("neustart", 1)
setze fehler auf fehler + pruefe("neustart: raum 0", S["raum_nr"] == 0)
setze fehler auf fehler + pruefe("neustart: 3 leben", S["leben"] == 3)
setze fehler auf fehler + pruefe("neustart: laeuft", S["modus"] == 0)

# ------------------------------------------------------------
# Phase 2: Sieg-Pfad — Raum 0 -> Hub -> Schlüsselraum
# ------------------------------------------------------------
tue("rechts", 200)
setze fehler auf fehler + pruefe("hub erreicht", S["raum_nr"] == 1)
tue("rechts", 200)
setze fehler auf fehler + pruefe("schluesselraum erreicht", S["raum_nr"] == 2)

# Wache im Engpass mit gezogenem Schwert besiegen
setze z auf 0
solange S["e_lebt"][0] und z < 600:
    setze d auf inp_leer()
    d["rechts"] = wahr
    d["schwert"] = wahr
    spiel_schritt(d)
    setze z auf z + 1
setze fehler auf fehler + pruefe("wache besiegt", S["e_lebt"][0] == falsch)
setze fehler auf fehler + pruefe("kampf ueberlebt", S["leben"] >= 1)

# Weiter zum Schlüssel
setze z auf 0
solange S["hat_key"] == falsch und z < 400:
    tue("rechts", 1)
    setze z auf z + 1
setze fehler auf fehler + pruefe("schluessel geholt", S["hat_key"])
foto(win, "zelda_03_schluessel")

# ------------------------------------------------------------
# Phase 3: Zurück zum Hub, verschlossene Tür im Norden öffnen
# ------------------------------------------------------------
setze z auf 0
solange S["raum_nr"] != 1 und z < 400:
    tue("links", 1)
    setze z auf z + 1
setze fehler auf fehler + pruefe("zurueck im hub", S["raum_nr"] == 1)

# Zur Tür-Spalte (x=322) laufen, dann nach oben durch die S-Tür
setze z auf 0
solange S["px"] > 322 und z < 200:
    tue("links", 1)
    setze z auf z + 1
setze z auf 0
solange S["raum_nr"] != 3 und z < 200:
    tue("hoch", 1)
    setze z auf z + 1
setze fehler auf fehler + pruefe("bossraum erreicht (tuer geoeffnet)", S["raum_nr"] == 3)

# ------------------------------------------------------------
# Phase 4: Bosskampf — stehen bleiben, Schwert nach oben halten
# ------------------------------------------------------------
setze boss_foto auf falsch
setze z auf 0
solange S["boss_tot"] == falsch und S["modus"] == 0 und z < 3000:
    setze d auf inp_leer()
    d["schwert"] = wahr
    spiel_schritt(d)
    wenn boss_foto == falsch und S["boss_iv"] > 0:
        foto(win, "zelda_04_bosskampf")
        setze boss_foto auf wahr
    setze z auf z + 1
setze fehler auf fehler + pruefe("boss besiegt", S["boss_tot"])
setze fehler auf fehler + pruefe("bosskampf ueberlebt", S["modus"] == 0 und S["leben"] >= 1)
setze fehler auf fehler + pruefe("boss-tor offen", S["karten"][3][10] == 4)

# ------------------------------------------------------------
# Phase 5: Durchs Tor zum Triforce -> SIEG
# ------------------------------------------------------------
setze z auf 0
solange S["raum_nr"] != 4 und z < 300:
    tue("hoch", 1)
    setze z auf z + 1
setze fehler auf fehler + pruefe("triforce-kammer erreicht", S["raum_nr"] == 4)
setze z auf 0
solange S["modus"] != 2 und z < 300:
    tue("hoch", 1)
    setze z auf z + 1
setze fehler auf fehler + pruefe("SIEG erreicht", S["modus"] == 2)

# Sieg-Szene rendern + prüfen (vor Present!)
welt_zeichnen(win)
setze p auf test_pixel(win, 100, 190)
setze fehler auf fehler + pruefe_farbe("sieg-overlay", p, 62, 47, 0, 8)
setze pfad auf out + "/zelda_05_sieg.bmp"
test_screenshot(win, pfad)
zeige "screenshot: " + pfad
fenster_aktualisieren(win)

# Neustart nach Sieg funktioniert ebenfalls
tue("neustart", 1)
setze fehler auf fehler + pruefe("neustart nach sieg", S["modus"] == 0 und S["raum_nr"] == 0)

fenster_schliessen(win)

wenn fehler == 0:
    zeige "SELFTEST_RESULT: PASS zelda_gameplay"
sonst:
    zeige "SELFTEST_RESULT: FAIL zelda_gameplay (" + text(fehler) + " Fehler)"

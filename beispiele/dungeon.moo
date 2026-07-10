# ============================================================
# dungeon.moo — Roguelike mit prozeduraler Dungeon-Generierung
#
# ZIEL: Steige über die goldenen Treppen bis Level 10 hinab!
#
# Start: moo-compiler run beispiele/dungeon.moo
#
# Steuerung: WASD/Pfeile (turn-based), in Gegner laufen = Angriff,
#            R = Neustart, Escape = Beenden
# Aufbau: 1. Pixelfont  2. Zustands-Dict S (Globals-Shadowing!
#   siehe zelda.moo)  3. Generierung (Räume + L-Gänge, exakter LCG)
#   4. spiel_schritt(inp) — headless testbar (dungeon_selftest.moo)
#   5. Fog-of-War + Zeichnen  6. Loop (Env-Guard DUNGEON_SELFTEST)
# ============================================================

konstante BREITE auf 800
konstante HOEHE auf 640
konstante TILE auf 16
konstante MAP_W auf 100
konstante MAP_H auf 100
konstante OFFSET_X auf 0
konstante OFFSET_Y auf 32
konstante SICHT auf 7
konstante MAX_RAEUME auf 30
konstante MAX_GEGNER auf 20
konstante MAX_ITEMS auf 15
konstante ZIEL_LEVEL auf 10

konstante WAND auf 0
konstante BODEN auf 1
konstante TREPPE auf 2

# ============================================================
# 1. Pixelfont (3x5) — nur die Zeichen, die wir anzeigen
# ============================================================
setze FONT auf {}
FONT[" "] = [0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0]
FONT[":"] = [0,0,0, 0,1,0, 0,0,0, 0,1,0, 0,0,0]
FONT["0"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["1"] = [0,1,0, 1,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["A"] = [0,1,0, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
FONT["C"] = [1,1,1, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["D"] = [1,1,0, 1,0,1, 1,0,1, 1,0,1, 1,1,0]
FONT["E"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1]
FONT["G"] = [1,1,1, 1,0,0, 1,0,1, 1,0,1, 1,1,1]
FONT["I"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["K"] = [1,0,1, 1,1,0, 1,0,0, 1,1,0, 1,0,1]
FONT["L"] = [1,0,0, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["M"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT["O"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["R"] = [1,1,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1]
FONT["S"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
FONT["U"] = [1,0,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["V"] = [1,0,1, 1,0,1, 1,0,1, 1,0,1, 0,1,0]
FONT["Z"] = [1,1,1, 0,0,1, 0,1,0, 1,0,0, 1,1,1]

funktion zeichne_bitmap(win, bits, x, y, px, farbe):
    setze zy auf 0
    solange zy < 5:
        setze zx auf 0
        solange zx < 3:
            wenn bits[zy * 3 + zx] == 1:
                zeichne_rechteck(win, x + zx * px, y + zy * px, px, px, farbe)
            setze zx auf zx + 1
        setze zy auf zy + 1

funktion zeichne_text_px(win, s, x, y, px, farbe):
    setze i auf 0
    setze cx auf x
    solange i < länge(s):
        setze ch auf s[i]
        wenn FONT.enthält(ch):
            zeichne_bitmap(win, FONT[ch], cx, y, px, farbe)
        setze cx auf cx + 4 * px
        setze i auf i + 1

# ============================================================
# 2. Zustands-Dict + Helfer
#    modus: 0=spielen, 1=game over, 2=sieg
# ============================================================
setze S auf {}

funktion rng():
    # Exakter Klein-LCG (mod 65537): bleibt weit unter 2^53, damit
    # die double-Arithmetik von moo nie Präzision verliert.
    S["rng"] = (S["rng"] * 75 + 74) % 65537
    gib_zurück S["rng"]

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

funktion karte_get(x, y):
    wenn x < 0 oder x >= MAP_W oder y < 0 oder y >= MAP_H:
        gib_zurück WAND
    gib_zurück S["karte"][y * MAP_W + x]

funktion karte_set(x, y, val):
    wenn x >= 0 und x < MAP_W und y >= 0 und y < MAP_H:
        setze k auf S["karte"]
        k[y * MAP_W + x] = val

# ============================================================
# 3. Prozedurale Generierung: Räume + L-förmige Gänge
# ============================================================
funktion dungeon_generieren(level):
    setze k auf S["karte"]
    setze si auf S["sichtbar"]
    setze en auf S["entdeckt"]
    setze idx auf 0
    solange idx < MAP_W * MAP_H:
        k[idx] = WAND
        si[idx] = falsch
        en[idx] = falsch
        setze idx auf idx + 1
    S["raum_x"] = []
    S["raum_y"] = []
    S["raum_w"] = []
    S["raum_h"] = []

    # Räume platzieren (ohne Überlappung, +2 Rand)
    setze rx_l auf S["raum_x"]
    setze ry_l auf S["raum_y"]
    setze rw_l auf S["raum_w"]
    setze rh_l auf S["raum_h"]
    setze versuch auf 0
    solange versuch < 150 und länge(rx_l) < MAX_RAEUME:
        setze rw auf 5 + rng() % 8
        setze rh auf 4 + rng() % 6
        setze rx auf 2 + rng() % (MAP_W - rw - 4)
        setze ry auf 2 + rng() % (MAP_H - rh - 4)
        setze ok auf wahr
        setze ri auf 0
        solange ri < länge(rx_l):
            wenn rx < rx_l[ri] + rw_l[ri] + 2 und rx + rw + 2 > rx_l[ri]:
                wenn ry < ry_l[ri] + rh_l[ri] + 2 und ry + rh + 2 > ry_l[ri]:
                    setze ok auf falsch
            setze ri auf ri + 1
        wenn ok:
            setze dy auf 0
            solange dy < rh:
                setze dx auf 0
                solange dx < rw:
                    karte_set(rx + dx, ry + dy, BODEN)
                    setze dx auf dx + 1
                setze dy auf dy + 1
            rx_l.hinzufügen(rx)
            ry_l.hinzufügen(ry)
            rw_l.hinzufügen(rw)
            rh_l.hinzufügen(rh)
        setze versuch auf versuch + 1

    # L-förmige Gänge zwischen aufeinanderfolgenden Räumen
    setze ri auf 1
    solange ri < länge(rx_l):
        setze cx1 auf rx_l[ri - 1] + boden(rw_l[ri - 1] / 2)
        setze cy1 auf ry_l[ri - 1] + boden(rh_l[ri - 1] / 2)
        setze cx2 auf rx_l[ri] + boden(rw_l[ri] / 2)
        setze cy2 auf ry_l[ri] + boden(rh_l[ri] / 2)
        wenn rng() % 2 == 0:
            setze gx auf cx1
            solange gx != cx2:
                karte_set(gx, cy1, BODEN)
                wenn gx < cx2:
                    setze gx auf gx + 1
                sonst:
                    setze gx auf gx - 1
            setze gy auf cy1
            solange gy != cy2:
                karte_set(cx2, gy, BODEN)
                wenn gy < cy2:
                    setze gy auf gy + 1
                sonst:
                    setze gy auf gy - 1
        sonst:
            setze gy auf cy1
            solange gy != cy2:
                karte_set(cx1, gy, BODEN)
                wenn gy < cy2:
                    setze gy auf gy + 1
                sonst:
                    setze gy auf gy - 1
            setze gx auf cx1
            solange gx != cx2:
                karte_set(gx, cy2, BODEN)
                wenn gx < cx2:
                    setze gx auf gx + 1
                sonst:
                    setze gx auf gx - 1
        setze ri auf ri + 1

    # Treppe im letzten Raum (Position für Selftest in S abrufbar)
    setze last auf länge(rx_l) - 1
    wenn last >= 0:
        setze tx auf rx_l[last] + boden(rw_l[last] / 2)
        setze ty auf ry_l[last] + boden(rh_l[last] / 2)
        karte_set(tx, ty, TREPPE)
        S["treppe_x"] = tx
        S["treppe_y"] = ty

funktion gegner_platzieren(level):
    setze anzahl auf 3 + level * 2
    wenn anzahl > MAX_GEGNER:
        setze anzahl auf MAX_GEGNER
    setze platziert auf 0
    setze tries auf 0
    setze gx_l auf S["geg_x"]
    setze gy_l auf S["geg_y"]
    setze ghp auf S["geg_hp"]
    setze gmax auf S["geg_max_hp"]
    setze gdmg auf S["geg_dmg"]
    setze gtyp auf S["geg_typ"]
    setze gakt auf S["geg_aktiv"]
    solange platziert < anzahl und tries < 200:
        setze ri auf rng() % länge(S["raum_x"])
        wenn ri > 0:
            setze gx auf S["raum_x"][ri] + rng() % S["raum_w"][ri]
            setze gy auf S["raum_y"][ri] + rng() % S["raum_h"][ri]
            wenn karte_get(gx, gy) == BODEN:
                setze gi auf 0
                solange gi < MAX_GEGNER:
                    wenn gakt[gi] == falsch:
                        gx_l[gi] = gx
                        gy_l[gi] = gy
                        setze typ auf rng() % (1 + boden(level / 2))
                        wenn typ > 4:
                            setze typ auf 4
                        gtyp[gi] = typ
                        ghp[gi] = 8 + typ * 5 + level * 2
                        gmax[gi] = ghp[gi]
                        gdmg[gi] = 3 + typ * 2 + level
                        gakt[gi] = wahr
                        setze platziert auf platziert + 1
                        setze gi auf MAX_GEGNER
                    setze gi auf gi + 1
        setze tries auf tries + 1

funktion items_platzieren(level):
    # 0=Heiltrank(+15) 1=Stärke(+2) 2=Max-HP(+5)
    setze anzahl auf 3 + level
    wenn anzahl > MAX_ITEMS:
        setze anzahl auf MAX_ITEMS
    setze platziert auf 0
    setze tries auf 0
    setze iakt auf S["item_aktiv"]
    solange platziert < anzahl und tries < 100:
        setze ri auf rng() % länge(S["raum_x"])
        setze ix auf S["raum_x"][ri] + rng() % S["raum_w"][ri]
        setze iy auf S["raum_y"][ri] + rng() % S["raum_h"][ri]
        wenn karte_get(ix, iy) == BODEN:
            setze ii auf 0
            solange ii < MAX_ITEMS:
                wenn iakt[ii] == falsch:
                    S["item_x"][ii] = ix
                    S["item_y"][ii] = iy
                    S["item_typ"][ii] = rng() % 3
                    iakt[ii] = wahr
                    setze platziert auf platziert + 1
                    setze ii auf MAX_ITEMS
                setze ii auf ii + 1
        setze tries auf tries + 1

funktion sicht_berechnen():
    setze si auf S["sichtbar"]
    setze en auf S["entdeckt"]
    setze idx auf 0
    solange idx < MAP_W * MAP_H:
        si[idx] = falsch
        setze idx auf idx + 1
    setze dy auf 0 - SICHT
    solange dy <= SICHT:
        setze dx auf 0 - SICHT
        solange dx <= SICHT:
            wenn dx * dx + dy * dy <= SICHT * SICHT:
                setze tx auf S["spieler_x"] + dx
                setze ty auf S["spieler_y"] + dy
                wenn tx >= 0 und tx < MAP_W und ty >= 0 und ty < MAP_H:
                    si[ty * MAP_W + tx] = wahr
                    en[ty * MAP_W + tx] = wahr
            setze dx auf dx + 1
        setze dy auf dy + 1

funktion neues_level():
    # Fix für den alten Shadowing-Bug: alles über S mutieren
    setze gi auf 0
    solange gi < MAX_GEGNER:
        S["geg_aktiv"][gi] = falsch
        setze gi auf gi + 1
    setze ii auf 0
    solange ii < MAX_ITEMS:
        S["item_aktiv"][ii] = falsch
        setze ii auf ii + 1
    dungeon_generieren(S["level"])
    gegner_platzieren(S["level"])
    items_platzieren(S["level"])
    # Spieler in die Mitte des ERSTEN Raums
    wenn länge(S["raum_x"]) > 0:
        S["spieler_x"] = S["raum_x"][0] + boden(S["raum_w"][0] / 2)
        S["spieler_y"] = S["raum_y"][0] + boden(S["raum_h"][0] / 2)
    sicht_berechnen()

funktion neustart():
    S["rng"] = 777
    S["karte"] = []
    S["sichtbar"] = []
    S["entdeckt"] = []
    setze idx auf 0
    solange idx < MAP_W * MAP_H:
        S["karte"].hinzufügen(WAND)
        S["sichtbar"].hinzufügen(falsch)
        S["entdeckt"].hinzufügen(falsch)
        setze idx auf idx + 1
    S["geg_x"] = []
    S["geg_y"] = []
    S["geg_hp"] = []
    S["geg_max_hp"] = []
    S["geg_dmg"] = []
    S["geg_typ"] = []
    S["geg_aktiv"] = []
    setze gi auf 0
    solange gi < MAX_GEGNER:
        S["geg_x"].hinzufügen(0)
        S["geg_y"].hinzufügen(0)
        S["geg_hp"].hinzufügen(0)
        S["geg_max_hp"].hinzufügen(0)
        S["geg_dmg"].hinzufügen(0)
        S["geg_typ"].hinzufügen(0)
        S["geg_aktiv"].hinzufügen(falsch)
        setze gi auf gi + 1
    S["item_x"] = []
    S["item_y"] = []
    S["item_typ"] = []
    S["item_aktiv"] = []
    setze ii auf 0
    solange ii < MAX_ITEMS:
        S["item_x"].hinzufügen(0)
        S["item_y"].hinzufügen(0)
        S["item_typ"].hinzufügen(0)
        S["item_aktiv"].hinzufügen(falsch)
        setze ii auf ii + 1
    S["spieler_x"] = 0
    S["spieler_y"] = 0
    S["spieler_hp"] = 30
    S["spieler_max_hp"] = 30
    S["spieler_dmg"] = 5
    S["spieler_dx"] = 0
    S["spieler_dy"] = 1
    S["level"] = 1
    S["score"] = 0
    S["cooldown"] = 0
    S["modus"] = 0
    neues_level()

# ============================================================
# 4. Spiellogik — ein Frame pro Aufruf, komplett headless testbar.
#    inp = {"hoch","runter","links","rechts","neustart"}
#    Turn-based: in einen Gegner laufen = Angriff.
# ============================================================
funktion spiel_schritt(inp):
    wenn S["modus"] != 0:
        wenn inp["neustart"]:
            neustart()
        gib_zurück 0
    wenn S["cooldown"] > 0:
        S["cooldown"] = S["cooldown"] - 1
        gib_zurück 0
    setze ndx auf 0
    setze ndy auf 0
    wenn inp["hoch"]:
        setze ndy auf -1
    wenn inp["runter"]:
        setze ndy auf 1
    wenn inp["links"]:
        setze ndx auf -1
    wenn inp["rechts"]:
        setze ndx auf 1
    wenn ndx == 0 und ndy == 0:
        gib_zurück 0

    S["spieler_dx"] = ndx
    S["spieler_dy"] = ndy
    setze nx auf S["spieler_x"] + ndx
    setze ny auf S["spieler_y"] + ndy
    setze gakt auf S["geg_aktiv"]
    setze ghp auf S["geg_hp"]

    # Gegner auf Zielfeld? -> Angriff statt Bewegung
    setze ziel_gegner auf -1
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn gakt[gi] und S["geg_x"][gi] == nx und S["geg_y"][gi] == ny:
            setze ziel_gegner auf gi
        setze gi auf gi + 1

    wenn ziel_gegner >= 0:
        ghp[ziel_gegner] = ghp[ziel_gegner] - S["spieler_dmg"]
        wenn ghp[ziel_gegner] <= 0:
            gakt[ziel_gegner] = falsch
            S["score"] = S["score"] + (S["geg_typ"][ziel_gegner] + 1) * 20
    sonst:
        wenn karte_get(nx, ny) != WAND:
            S["spieler_x"] = nx
            S["spieler_y"] = ny

    # Treppe erreicht -> nächstes Level / Sieg
    wenn karte_get(S["spieler_x"], S["spieler_y"]) == TREPPE:
        S["level"] = S["level"] + 1
        wenn S["level"] > ZIEL_LEVEL:
            S["modus"] = 2
            gib_zurück 0
        neues_level()
        S["cooldown"] = 6
        gib_zurück 0

    # Items einsammeln
    setze ii auf 0
    solange ii < MAX_ITEMS:
        wenn S["item_aktiv"][ii] und S["item_x"][ii] == S["spieler_x"] und S["item_y"][ii] == S["spieler_y"]:
            wenn S["item_typ"][ii] == 0:
                S["spieler_hp"] = min(S["spieler_hp"] + 15, S["spieler_max_hp"])
            wenn S["item_typ"][ii] == 1:
                S["spieler_dmg"] = S["spieler_dmg"] + 2
            wenn S["item_typ"][ii] == 2:
                S["spieler_max_hp"] = S["spieler_max_hp"] + 5
                S["spieler_hp"] = S["spieler_hp"] + 5
            S["item_aktiv"][ii] = falsch
            S["score"] = S["score"] + 10
        setze ii auf ii + 1

    # Gegner: zum Spieler laufen wenn in Sicht, daneben = Angriff
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn gakt[gi]:
            setze dist auf abs_wert(S["geg_x"][gi] - S["spieler_x"]) + abs_wert(S["geg_y"][gi] - S["spieler_y"])
            wenn dist < SICHT und dist > 1:
                setze gdx auf 0
                setze gdy auf 0
                wenn S["geg_x"][gi] < S["spieler_x"]:
                    setze gdx auf 1
                wenn S["geg_x"][gi] > S["spieler_x"]:
                    setze gdx auf -1
                wenn S["geg_y"][gi] < S["spieler_y"]:
                    setze gdy auf 1
                wenn S["geg_y"][gi] > S["spieler_y"]:
                    setze gdy auf -1
                wenn abs_wert(S["geg_x"][gi] - S["spieler_x"]) >= abs_wert(S["geg_y"][gi] - S["spieler_y"]):
                    wenn karte_get(S["geg_x"][gi] + gdx, S["geg_y"][gi]) == BODEN:
                        S["geg_x"][gi] = S["geg_x"][gi] + gdx
                    sonst wenn karte_get(S["geg_x"][gi], S["geg_y"][gi] + gdy) == BODEN:
                        S["geg_y"][gi] = S["geg_y"][gi] + gdy
                sonst:
                    wenn karte_get(S["geg_x"][gi], S["geg_y"][gi] + gdy) == BODEN:
                        S["geg_y"][gi] = S["geg_y"][gi] + gdy
                    sonst wenn karte_get(S["geg_x"][gi] + gdx, S["geg_y"][gi]) == BODEN:
                        S["geg_x"][gi] = S["geg_x"][gi] + gdx
            wenn abs_wert(S["geg_x"][gi] - S["spieler_x"]) + abs_wert(S["geg_y"][gi] - S["spieler_y"]) == 1:
                S["spieler_hp"] = S["spieler_hp"] - S["geg_dmg"][gi]
                wenn S["spieler_hp"] <= 0:
                    S["spieler_hp"] = 0
                    S["modus"] = 1
        setze gi auf gi + 1

    sicht_berechnen()
    S["cooldown"] = 6
    gib_zurück 0

# ============================================================
# 5. Zeichnen (Fog-of-War: sichtbar hell, entdeckt dunkel)
# ============================================================
funktion welt_zeichnen(win):
    fenster_löschen(win, "#0A0A0A")
    setze cam_x auf S["spieler_x"] - 25
    setze cam_y auf S["spieler_y"] - 19
    setze k auf S["karte"]
    setze si auf S["sichtbar"]
    setze en auf S["entdeckt"]

    setze sy auf 0
    solange sy < MAP_H:
        setze sx auf 0
        solange sx < MAP_W:
            setze screen_x auf (sx - cam_x) * TILE + OFFSET_X
            setze screen_y auf (sy - cam_y) * TILE + OFFSET_Y
            wenn screen_x > -TILE und screen_x < BREITE und screen_y > -TILE und screen_y < HOEHE:
                setze idx auf sy * MAP_W + sx
                setze val auf k[idx]
                wenn si[idx]:
                    wenn val == WAND:
                        zeichne_rechteck(win, screen_x, screen_y, TILE, TILE, "#37474F")
                    wenn val == BODEN:
                        zeichne_rechteck(win, screen_x, screen_y, TILE, TILE, "#263238")
                    wenn val == TREPPE:
                        zeichne_rechteck(win, screen_x, screen_y, TILE, TILE, "#263238")
                        zeichne_rechteck(win, screen_x + 3, screen_y + 3, TILE - 6, TILE - 6, "#FFD700")
                        zeichne_rechteck(win, screen_x + 5, screen_y + 5, TILE - 10, TILE - 10, "#FFA000")
                sonst wenn en[idx]:
                    wenn val == WAND:
                        zeichne_rechteck(win, screen_x, screen_y, TILE, TILE, "#1A1A2E")
                    sonst:
                        zeichne_rechteck(win, screen_x, screen_y, TILE, TILE, "#111122")
            setze sx auf sx + 1
        setze sy auf sy + 1

    # Items (nur sichtbare)
    setze ii auf 0
    solange ii < MAX_ITEMS:
        wenn S["item_aktiv"][ii] und si[S["item_y"][ii] * MAP_W + S["item_x"][ii]]:
            setze screen_x auf (S["item_x"][ii] - cam_x) * TILE + OFFSET_X + TILE / 2
            setze screen_y auf (S["item_y"][ii] - cam_y) * TILE + OFFSET_Y + TILE / 2
            setze ifarbe auf "#F44336"
            wenn S["item_typ"][ii] == 1:
                setze ifarbe auf "#FF9800"
            wenn S["item_typ"][ii] == 2:
                setze ifarbe auf "#4CAF50"
            zeichne_kreis(win, screen_x, screen_y, 5, ifarbe)
        setze ii auf ii + 1

    # Gegner (nur sichtbare) + HP-Balken
    setze gi auf 0
    solange gi < MAX_GEGNER:
        wenn S["geg_aktiv"][gi] und si[S["geg_y"][gi] * MAP_W + S["geg_x"][gi]]:
            setze screen_x auf (S["geg_x"][gi] - cam_x) * TILE + OFFSET_X
            setze screen_y auf (S["geg_y"][gi] - cam_y) * TILE + OFFSET_Y
            setze gfarbe auf "#66BB6A"
            wenn S["geg_typ"][gi] == 1:
                setze gfarbe auf "#42A5F5"
            wenn S["geg_typ"][gi] == 2:
                setze gfarbe auf "#FF9800"
            wenn S["geg_typ"][gi] == 3:
                setze gfarbe auf "#F44336"
            wenn S["geg_typ"][gi] >= 4:
                setze gfarbe auf "#9C27B0"
            zeichne_rechteck(win, screen_x + 2, screen_y + 2, TILE - 4, TILE - 4, gfarbe)
            setze hp_w auf (TILE - 4) * S["geg_hp"][gi] / S["geg_max_hp"][gi]
            zeichne_rechteck(win, screen_x + 2, screen_y - 2, hp_w, 2, "#F44336")
        setze gi auf gi + 1

    # Spieler
    setze screen_x auf (S["spieler_x"] - cam_x) * TILE + OFFSET_X
    setze screen_y auf (S["spieler_y"] - cam_y) * TILE + OFFSET_Y
    zeichne_rechteck(win, screen_x + 1, screen_y + 1, TILE - 2, TILE - 2, "#FFD700")
    zeichne_rechteck(win, screen_x + 3, screen_y + 3, TILE - 6, TILE - 6, "#FFC107")
    zeichne_rechteck(win, screen_x + 5 + S["spieler_dx"] * 3, screen_y + 4 + S["spieler_dy"] * 3, 2, 2, "#000000")
    zeichne_rechteck(win, screen_x + 9 + S["spieler_dx"] * 3, screen_y + 4 + S["spieler_dy"] * 3, 2, 2, "#000000")

    # HUD: Ziel, HP-Balken, Level-Pips
    zeichne_rechteck(win, 0, 0, BREITE, 28, "#1B2631")
    zeichne_text_px(win, "ZIEL: LEVEL 10", 560, 7, 2, "#FFFFFF")
    setze hp_w auf S["spieler_hp"] * 150 / S["spieler_max_hp"]
    zeichne_rechteck(win, 10, 6, 150, 14, "#333333")
    wenn S["spieler_hp"] > S["spieler_max_hp"] / 3:
        zeichne_rechteck(win, 10, 6, hp_w, 14, "#4CAF50")
    sonst:
        zeichne_rechteck(win, 10, 6, hp_w, 14, "#F44336")
    setze li auf 0
    solange li < S["level"] und li < ZIEL_LEVEL:
        zeichne_rechteck(win, 180 + li * 14, 8, 10, 10, "#FFD700")
        setze li auf li + 1

    # Overlays
    wenn S["modus"] == 1:
        zeichne_rechteck(win, 100, 240, 600, 150, "#1A1A2E")
        zeichne_text_px(win, "GAME OVER", 160, 270, 6, "#E53935")
        zeichne_text_px(win, "DRUECKE R", 290, 340, 3, "#FFFFFF")
    wenn S["modus"] == 2:
        zeichne_rechteck(win, 100, 240, 600, 150, "#3E2F00")
        zeichne_text_px(win, "SIEG", 280, 260, 8, "#FFD54F")
        zeichne_text_px(win, "DRUECKE R", 290, 340, 3, "#FFFFFF")
    gib_zurück 0

neustart()

# ============================================================
# 6. Interaktiver Loop (im Selftest per Env-Variable übersprungen)
# ============================================================
wenn umgebung("DUNGEON_SELFTEST") == nichts:
    zeige "=== moo Dungeon ==="
    zeige "ZIEL: Steige über die goldenen Treppen bis Level 10 hinab!"
    zeige "WASD/Pfeile=Bewegen, in Gegner laufen=Angriff, R=Neustart, Escape=Beenden"

    setze win auf fenster_erstelle("moo Dungeon", BREITE, HOEHE)

    solange fenster_offen(win):
        wenn taste_gedrückt("escape"):
            stopp
        setze inp auf {}
        inp["hoch"] = taste_gedrückt("w") oder taste_gedrückt("hoch")
        inp["runter"] = taste_gedrückt("s") oder taste_gedrückt("runter")
        inp["links"] = taste_gedrückt("a") oder taste_gedrückt("links")
        inp["rechts"] = taste_gedrückt("d") oder taste_gedrückt("rechts")
        inp["neustart"] = taste_gedrückt("r")
        spiel_schritt(inp)
        welt_zeichnen(win)
        fenster_aktualisieren(win)
        warte(16)

    fenster_schliessen(win)
    zeige "Dungeon beendet."

# ============================================================
# zelda.moo — Top-Down Adventure mit Zelda-like Sprites
#
# Kompilieren: moo-compiler compile beispiele/zelda.moo -o beispiele/zelda
# Starten:     ./beispiele/zelda
#
# Steuerung: WASD=Bewegen, Leertaste=Schwert, Escape=Beenden
#
# Assets: opengameart.org/content/zelda-like-tilesets-and-sprites
# ============================================================

konstante TILE auf 32
konstante MAP_W auf 20
konstante MAP_H auf 15
konstante WIN_W auf 640
konstante WIN_H auf 480
konstante SPEED auf 3
konstante P_SIZE auf 28
konstante F_SIZE auf 28

# Sprite-Pfade
konstante GFX auf "beispiele/assets/sprites/zelda_like/gfx/"

# Richtungen: 0=unten, 1=links, 2=rechts, 3=oben
# Character-Sheet: 34x32 Frames, 8 Spalten x 8 Reihen
# Row 0=unten walk, Row 1=links walk, Row 2=rechts walk, Row 3=oben walk

# === Sprites laden ===
funktion lade_sprites(win):
    setze s auf {}
    s["char"] = sprite_laden(win, GFX + "character.png")
    s["world"] = sprite_laden(win, GFX + "Overworld.png")
    s["obj"] = sprite_laden(win, GFX + "objects.png")
    s["cave"] = sprite_laden(win, GFX + "cave.png")
    gib_zurück s

# === Charakter zeichnen (animiert) ===
funktion held_zeichnen(win, s, x, y, richtung, frame, angriff):
    # Walk: Row = richtung, Col = frame % 4 (34x32 Frames)
    setze row auf richtung
    setze col auf frame % 4
    wenn angriff > 0:
        setze col auf 4
    setze sx auf col * 34
    setze sy auf row * 32
    setze char_spr auf s["char"]
    sprite_ausschnitt(win, char_spr, sx, sy, 34, 32, x - 8, y - 8, 48, 48)

# === Overworld-Tile zeichnen ===
# Overworld.png: 16x16 Tiles, 40 Spalten x 36 Reihen
# Gras: ~(0,0), Wand/Stein: ~(0,16), Wasser: ~(0,128), Baum: ~(16,0)
funktion tile_zeichnen(win, s, typ, dx, dy):
    setze world_spr auf s["world"]
    wenn typ == 0:
        sprite_ausschnitt(win, world_spr, 0, 0, 16, 16, dx, dy, TILE, TILE)
    wenn typ == 1:
        sprite_ausschnitt(win, world_spr, 96, 0, 16, 16, dx, dy, TILE, TILE)
    wenn typ == 2:
        sprite_ausschnitt(win, world_spr, 128, 0, 16, 16, dx, dy, TILE, TILE)
    wenn typ == 3:
        sprite_ausschnitt(win, world_spr, 0, 0, 16, 16, dx, dy, TILE, TILE)
        sprite_ausschnitt(win, world_spr, 16, 0, 16, 16, dx, dy, TILE, TILE)
    wenn typ == 4:
        sprite_ausschnitt(win, world_spr, 48, 16, 16, 16, dx, dy, TILE, TILE)

# === Items zeichnen ===
# Objects.png: Herz ~(0,0), Muenze ~(32,16), Schluessel ~(64,0)
funktion item_zeichnen(win, s, typ, dx, dy):
    setze obj_spr auf s["obj"]
    wenn typ == 0:
        sprite_ausschnitt(win, obj_spr, 32, 32, 16, 16, dx, dy, 24, 24)
    wenn typ == 1:
        sprite_ausschnitt(win, obj_spr, 0, 0, 16, 16, dx, dy, 24, 24)
    wenn typ == 2:
        sprite_ausschnitt(win, obj_spr, 0, 16, 16, 16, dx, dy, 24, 24)

# === Feind zeichnen ===
funktion feind_zeichnen(win, s, x, y):
    setze char_spr auf s["char"]
    sprite_ausschnitt(win, char_spr, 0, 128, 34, 32, x - 8, y - 8, 48, 48)

funktion tile_fest(t):
    gib_zurück t == 1 oder t == 2 oder t == 3

# === Karten ===
funktion erstelle_raum_0():
    setze k auf []
    setze y auf 0
    solange y < MAP_H:
        setze x auf 0
        solange x < MAP_W:
            wenn y == 0 oder y == MAP_H - 1 oder x == 0 oder x == MAP_W - 1:
                k.hinzufügen(1)
            sonst wenn x >= 9 und x <= 11 und y >= 4 und y <= 7:
                k.hinzufügen(2)
            sonst wenn (x == 4 oder x == 5) und (y == 4 oder y == 5):
                k.hinzufügen(3)
            sonst wenn (x == 14 oder x == 15) und (y == 9 oder y == 10):
                k.hinzufügen(3)
            sonst wenn x == 3 und y >= 8 und y <= 11:
                k.hinzufügen(3)
            sonst:
                k.hinzufügen(0)
            setze x auf x + 1
        setze y auf y + 1
    k[7 * MAP_W + MAP_W - 1] = 4
    gib_zurück k

funktion erstelle_raum_1():
    setze k auf []
    setze y auf 0
    solange y < MAP_H:
        setze x auf 0
        solange x < MAP_W:
            wenn y == 0 oder y == MAP_H - 1 oder x == 0 oder x == MAP_W - 1:
                k.hinzufügen(1)
            sonst wenn x == 5 und y >= 2 und y <= 8:
                k.hinzufügen(1)
            sonst wenn x == 10 und y >= 6 und y <= 12:
                k.hinzufügen(1)
            sonst wenn x == 15 und y >= 2 und y <= 8:
                k.hinzufügen(1)
            sonst wenn y == 5 und x >= 7 und x <= 9:
                k.hinzufügen(1)
            sonst wenn y == 10 und x >= 12 und x <= 14:
                k.hinzufügen(1)
            sonst:
                k.hinzufügen(0)
            setze x auf x + 1
        setze y auf y + 1
    k[7 * MAP_W + 0] = 4
    gib_zurück k

funktion karte_zeichnen(win, s, karte):
    setze y auf 0
    solange y < MAP_H:
        setze x auf 0
        solange x < MAP_W:
            setze t auf karte[y * MAP_W + x]
            tile_zeichnen(win, s, t, x * TILE, y * TILE)
            setze x auf x + 1
        setze y auf y + 1

# === Feinde ===
setze fx auf []
setze fy auf []
setze fd auf []
setze feind_a auf []

funktion feinde_init(raum):
    setze fx auf []
    setze fy auf []
    setze fd auf []
    setze feind_a auf []
    wenn raum == 0:
        fx.hinzufügen(224.0)
        fy.hinzufügen(128.0)
        fd.hinzufügen(1)
        feind_a.hinzufügen(wahr)
        fx.hinzufügen(384.0)
        fy.hinzufügen(320.0)
        fd.hinzufügen(-1)
        feind_a.hinzufügen(wahr)
        fx.hinzufügen(480.0)
        fy.hinzufügen(192.0)
        fd.hinzufügen(1)
        feind_a.hinzufügen(wahr)
    wenn raum == 1:
        fx.hinzufügen(224.0)
        fy.hinzufügen(96.0)
        fd.hinzufügen(1)
        feind_a.hinzufügen(wahr)
        fx.hinzufügen(384.0)
        fy.hinzufügen(288.0)
        fd.hinzufügen(-1)
        feind_a.hinzufügen(wahr)

funktion feinde_update(karte):
    setze i auf 0
    solange i < länge(fx):
        wenn feind_a[i]:
            setze nx auf fx[i] + fd[i] * 2
            setze tx auf boden(nx / TILE)
            setze ty auf boden(fy[i] / TILE)
            wenn tx < 1 oder tx >= MAP_W - 1 oder tile_fest(karte[ty * MAP_W + tx]):
                fd[i] = fd[i] * -1
            sonst:
                fx[i] = nx
        setze i auf i + 1

funktion feinde_malen(win, s):
    setze i auf 0
    solange i < länge(fx):
        wenn feind_a[i]:
            feind_zeichnen(win, s, fx[i], fy[i])
        setze i auf i + 1

# === Items ===
setze ix auf []
setze iy auf []
setze it auf []
setze ia auf []

funktion items_init(raum):
    setze ix auf []
    setze iy auf []
    setze it auf []
    setze ia auf []
    wenn raum == 0:
        ix.hinzufügen(128.0)
        iy.hinzufügen(96.0)
        it.hinzufügen(0)
        ia.hinzufügen(wahr)
        ix.hinzufügen(320.0)
        iy.hinzufügen(288.0)
        it.hinzufügen(0)
        ia.hinzufügen(wahr)
        ix.hinzufügen(448.0)
        iy.hinzufügen(96.0)
        it.hinzufügen(0)
        ia.hinzufügen(wahr)
        ix.hinzufügen(192.0)
        iy.hinzufügen(384.0)
        it.hinzufügen(0)
        ia.hinzufügen(wahr)
        ix.hinzufügen(64.0)
        iy.hinzufügen(384.0)
        it.hinzufügen(1)
        ia.hinzufügen(wahr)
        ix.hinzufügen(544.0)
        iy.hinzufügen(384.0)
        it.hinzufügen(2)
        ia.hinzufügen(wahr)
    wenn raum == 1:
        ix.hinzufügen(288.0)
        iy.hinzufügen(128.0)
        it.hinzufügen(0)
        ia.hinzufügen(wahr)
        ix.hinzufügen(416.0)
        iy.hinzufügen(352.0)
        it.hinzufügen(0)
        ia.hinzufügen(wahr)
        ix.hinzufügen(160.0)
        iy.hinzufügen(256.0)
        it.hinzufügen(1)
        ia.hinzufügen(wahr)

funktion items_malen(win, s):
    setze i auf 0
    solange i < länge(ix):
        wenn ia[i]:
            item_zeichnen(win, s, it[i], ix[i], iy[i])
        setze i auf i + 1

# === Kollision ===
funktion aabb(x1, y1, w1, h1, x2, y2, w2, h2):
    gib_zurück x1 < x2 + w2 und x1 + w1 > x2 und y1 < y2 + h2 und y1 + h1 > y2

funktion tile_check(karte, px, py, pw, ph):
    setze tx1 auf boden(px / TILE)
    setze ty1 auf boden(py / TILE)
    setze tx2 auf boden((px + pw - 1) / TILE)
    setze ty2 auf boden((py + ph - 1) / TILE)
    wenn tx1 < 0 oder ty1 < 0 oder tx2 >= MAP_W oder ty2 >= MAP_H:
        gib_zurück wahr
    wenn tile_fest(karte[ty1 * MAP_W + tx1]):
        gib_zurück wahr
    wenn tile_fest(karte[ty1 * MAP_W + tx2]):
        gib_zurück wahr
    wenn tile_fest(karte[ty2 * MAP_W + tx1]):
        gib_zurück wahr
    wenn tile_fest(karte[ty2 * MAP_W + tx2]):
        gib_zurück wahr
    gib_zurück falsch

# === State ===
setze px auf 64.0
setze py auf 224.0
setze leben auf 3
setze score auf 0
setze hat_key auf falsch
setze raum_nr auf 0
setze schwert auf 0
setze richtung auf 0
setze walk_frame auf 0
setze walk_timer auf 0
setze unverw auf 0

setze karten auf [erstelle_raum_0(), erstelle_raum_1()]
setze karte auf karten[0]
feinde_init(0)
items_init(0)

funktion wechsel(nr, sx, sy):
    setze raum_nr auf nr
    setze karte auf karten[nr]
    setze px auf sx
    setze py auf sy
    feinde_init(nr)
    items_init(nr)

funktion hud(win, s):
    setze obj_spr auf s["obj"]
    zeichne_rechteck(win, 0, 0, WIN_W, 28, "#1A1A2E")
    setze i auf 0
    solange i < leben:
        sprite_ausschnitt(win, obj_spr, 0, 0, 16, 16, 8 + i * 26, 2, 24, 24)
        setze i auf i + 1
    setze sc auf 0
    solange sc < score und sc < 20:
        sprite_ausschnitt(win, obj_spr, 32, 32, 16, 16, WIN_W - 28 - sc * 14, 4, 20, 20)
        setze sc auf sc + 1
    wenn hat_key:
        sprite_ausschnitt(win, obj_spr, 0, 16, 16, 16, WIN_W / 2 - 12, 2, 24, 24)

# === Hauptprogramm ===
zeige "=== moo Zelda-Adventure ==="
zeige "WASD=Bewegen, Leertaste=Schwert, Escape=Beenden"

setze win auf fenster_erstelle("moo Zelda", WIN_W, WIN_H)
setze spr auf lade_sprites(win)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    setze bewegt auf falsch
    setze nx auf px
    setze ny auf py

    wenn taste_gedrückt("w"):
        setze ny auf ny - SPEED
        setze richtung auf 3
        setze bewegt auf wahr
    wenn taste_gedrückt("s"):
        setze ny auf ny + SPEED
        setze richtung auf 0
        setze bewegt auf wahr
    wenn taste_gedrückt("a"):
        setze nx auf nx - SPEED
        setze richtung auf 1
        setze bewegt auf wahr
    wenn taste_gedrückt("d"):
        setze nx auf nx + SPEED
        setze richtung auf 2
        setze bewegt auf wahr

    wenn tile_check(karte, nx, py, P_SIZE, P_SIZE) == falsch:
        setze px auf nx
    wenn tile_check(karte, px, ny, P_SIZE, P_SIZE) == falsch:
        setze py auf ny

    # Walk-Animation
    wenn bewegt:
        setze walk_timer auf walk_timer + 1
        wenn walk_timer >= 6:
            setze walk_frame auf walk_frame + 1
            setze walk_timer auf 0

    # Schwert
    wenn taste_gedrückt("leertaste") und schwert == 0:
        setze schwert auf 8
    wenn schwert > 0:
        setze schwert auf schwert - 1

    # Schwert-Richtungsvektoren
    setze sdx auf 0
    setze sdy auf 0
    wenn richtung == 0:
        setze sdy auf 1
    wenn richtung == 1:
        setze sdx auf -1
    wenn richtung == 2:
        setze sdx auf 1
    wenn richtung == 3:
        setze sdy auf -1

    setze swx auf px + sdx * 24
    setze swy auf py + sdy * 24

    # Feinde
    feinde_update(karte)

    wenn schwert > 0:
        setze i auf 0
        solange i < länge(fx):
            wenn feind_a[i]:
                wenn aabb(swx, swy, 20, 20, fx[i], fy[i], F_SIZE, F_SIZE):
                    feind_a[i] = falsch
                    setze score auf score + 10
            setze i auf i + 1

    wenn unverw > 0:
        setze unverw auf unverw - 1
    sonst:
        setze i auf 0
        solange i < länge(fx):
            wenn feind_a[i]:
                wenn aabb(px, py, P_SIZE, P_SIZE, fx[i], fy[i], F_SIZE, F_SIZE):
                    setze leben auf leben - 1
                    setze unverw auf 45
                    wenn leben <= 0:
                        zeige "GAME OVER! Score: " + text(score)
                        stopp
            setze i auf i + 1

    # Items
    setze i auf 0
    solange i < länge(ix):
        wenn ia[i]:
            wenn aabb(px, py, P_SIZE, P_SIZE, ix[i], iy[i], 24, 24):
                wenn it[i] == 0:
                    setze score auf score + 5
                wenn it[i] == 1:
                    setze leben auf min(leben + 1, 5)
                wenn it[i] == 2:
                    setze hat_key auf wahr
                ia[i] = falsch
        setze i auf i + 1

    # Tuer
    setze tcx auf boden((px + 14) / TILE)
    setze tcy auf boden((py + 14) / TILE)
    wenn tcx >= 0 und tcx < MAP_W und tcy >= 0 und tcy < MAP_H:
        wenn karte[tcy * MAP_W + tcx] == 4:
            wenn raum_nr == 0 und tcx == MAP_W - 1 und hat_key:
                wechsel(1, 48.0, 224.0)
            sonst wenn raum_nr == 1 und tcx == 0:
                wechsel(0, 576.0, 224.0)

    # === Zeichnen ===
    fenster_löschen(win, "#1A1A2E")
    karte_zeichnen(win, spr, karte)
    items_malen(win, spr)
    feinde_malen(win, spr)

    # Spieler (blinkt bei Unverwundbarkeit)
    wenn unverw == 0 oder unverw % 4 < 2:
        held_zeichnen(win, spr, px, py, richtung, walk_frame, schwert)

    # Schwert-Effekt (Richtungsabhaengig)
    wenn schwert > 0:
        wenn richtung == 0:
            zeichne_rechteck(win, px + 4, py + P_SIZE, 20, 6, "#E0E0E0")
        wenn richtung == 3:
            zeichne_rechteck(win, px + 4, py - 8, 20, 6, "#E0E0E0")
        wenn richtung == 1:
            zeichne_rechteck(win, px - 10, py + 4, 6, 20, "#E0E0E0")
        wenn richtung == 2:
            zeichne_rechteck(win, px + P_SIZE + 2, py + 4, 6, 20, "#E0E0E0")

    hud(win, spr)
    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Zelda-Adventure beendet. Score: " + text(score)

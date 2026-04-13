# ============================================================
# zelda.moo — Top-Down Adventure mit Kenney Sprites
#
# Kompilieren: moo-compiler compile beispiele/zelda.moo -o beispiele/zelda
# Starten:     ./beispiele/zelda
#
# Steuerung:
#   WASD      - Bewegen (4 Richtungen)
#   Leertaste - Schwert-Angriff
#   Escape    - Beenden
#
# Features:
#   * Tile-basierte Karte mit Kenney Roguelike Sprites
#   * 2 Raeume mit Tuer-Wechsel
#   * Feinde mit Patrouille + Schwert-Kampf
#   * Items: Muenzen, Herzen, Schluessel
#   * HUD: Leben + Score + Schluessel
# ============================================================

# === Konstanten ===
konstante TILE auf 32
konstante MAP_W auf 20
konstante MAP_H auf 15
konstante WIN_W auf 640
konstante WIN_H auf 480
konstante SPEED auf 3
konstante SPIELER_SIZE auf 28
konstante FEIND_SIZE auf 28

# Sprite-Basis-Pfad
konstante SPR_DIR auf "beispiele/assets/sprites/kenney_roguelike/Tiles/Colored/"

# === Sprites laden ===
funktion sprites_laden(win):
    setze spr auf {}
    # Terrain
    spr["gras"] = sprite_laden(win, SPR_DIR + "tile_0048.png")
    spr["wand"] = sprite_laden(win, SPR_DIR + "tile_0001.png")
    spr["wasser"] = sprite_laden(win, SPR_DIR + "tile_0089.png")
    spr["baum"] = sprite_laden(win, SPR_DIR + "tile_0052.png")
    spr["tuer"] = sprite_laden(win, SPR_DIR + "tile_0006.png")
    # Spieler (Ritter)
    spr["held"] = sprite_laden(win, SPR_DIR + "tile_0025.png")
    # Feinde
    spr["feind"] = sprite_laden(win, SPR_DIR + "tile_0032.png")
    # Items
    spr["muenze"] = sprite_laden(win, SPR_DIR + "tile_0067.png")
    spr["herz"] = sprite_laden(win, SPR_DIR + "tile_0060.png")
    spr["schluessel"] = sprite_laden(win, SPR_DIR + "tile_0068.png")
    # Schwert
    spr["schwert"] = sprite_laden(win, SPR_DIR + "tile_0057.png")
    gib_zurück spr

# === Tile-Typen ===
# 0=Gras, 1=Wand, 2=Wasser, 3=Baum, 4=Tuer
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

# === Karte zeichnen (mit Sprites) ===
funktion karte_zeichnen(win, karte, spr):
    setze y auf 0
    solange y < MAP_H:
        setze x auf 0
        solange x < MAP_W:
            setze t auf karte[y * MAP_W + x]
            # Immer Gras als Hintergrund
            sprite_zeichnen_skaliert(win, spr["gras"], x * TILE, y * TILE, TILE, TILE)
            # Dann Overlay
            wenn t == 1:
                sprite_zeichnen_skaliert(win, spr["wand"], x * TILE, y * TILE, TILE, TILE)
            wenn t == 2:
                sprite_zeichnen_skaliert(win, spr["wasser"], x * TILE, y * TILE, TILE, TILE)
            wenn t == 3:
                sprite_zeichnen_skaliert(win, spr["baum"], x * TILE, y * TILE, TILE, TILE)
            wenn t == 4:
                sprite_zeichnen_skaliert(win, spr["tuer"], x * TILE, y * TILE, TILE, TILE)
            setze x auf x + 1
        setze y auf y + 1

# === Feinde ===
setze feinde_x auf []
setze feinde_y auf []
setze feinde_dir auf []
setze feinde_aktiv auf []

funktion feinde_init(raum):
    setze feinde_x auf []
    setze feinde_y auf []
    setze feinde_dir auf []
    setze feinde_aktiv auf []
    wenn raum == 0:
        feinde_x.hinzufügen(224.0)
        feinde_y.hinzufügen(128.0)
        feinde_dir.hinzufügen(1)
        feinde_aktiv.hinzufügen(wahr)
        feinde_x.hinzufügen(384.0)
        feinde_y.hinzufügen(320.0)
        feinde_dir.hinzufügen(-1)
        feinde_aktiv.hinzufügen(wahr)
        feinde_x.hinzufügen(480.0)
        feinde_y.hinzufügen(192.0)
        feinde_dir.hinzufügen(1)
        feinde_aktiv.hinzufügen(wahr)
    wenn raum == 1:
        feinde_x.hinzufügen(224.0)
        feinde_y.hinzufügen(96.0)
        feinde_dir.hinzufügen(1)
        feinde_aktiv.hinzufügen(wahr)
        feinde_x.hinzufügen(384.0)
        feinde_y.hinzufügen(288.0)
        feinde_dir.hinzufügen(-1)
        feinde_aktiv.hinzufügen(wahr)

funktion feinde_update(karte):
    setze i auf 0
    solange i < länge(feinde_x):
        wenn feinde_aktiv[i]:
            setze nx auf feinde_x[i] + feinde_dir[i] * 2
            setze tx auf boden(nx / TILE)
            setze ty auf boden(feinde_y[i] / TILE)
            wenn tx < 1 oder tx >= MAP_W - 1 oder tile_fest(karte[ty * MAP_W + tx]):
                feinde_dir[i] = feinde_dir[i] * -1
            sonst:
                feinde_x[i] = nx
        setze i auf i + 1

funktion feinde_zeichnen(win, spr):
    setze i auf 0
    solange i < länge(feinde_x):
        wenn feinde_aktiv[i]:
            sprite_zeichnen_skaliert(win, spr["feind"], feinde_x[i], feinde_y[i], TILE, TILE)
        setze i auf i + 1

# === Items ===
setze item_x auf []
setze item_y auf []
setze item_typ auf []
setze item_aktiv auf []

funktion items_init(raum):
    setze item_x auf []
    setze item_y auf []
    setze item_typ auf []
    setze item_aktiv auf []
    wenn raum == 0:
        item_x.hinzufügen(128.0)
        item_y.hinzufügen(96.0)
        item_typ.hinzufügen(0)
        item_aktiv.hinzufügen(wahr)
        item_x.hinzufügen(320.0)
        item_y.hinzufügen(288.0)
        item_typ.hinzufügen(0)
        item_aktiv.hinzufügen(wahr)
        item_x.hinzufügen(448.0)
        item_y.hinzufügen(96.0)
        item_typ.hinzufügen(0)
        item_aktiv.hinzufügen(wahr)
        item_x.hinzufügen(192.0)
        item_y.hinzufügen(384.0)
        item_typ.hinzufügen(0)
        item_aktiv.hinzufügen(wahr)
        item_x.hinzufügen(64.0)
        item_y.hinzufügen(384.0)
        item_typ.hinzufügen(1)
        item_aktiv.hinzufügen(wahr)
        item_x.hinzufügen(544.0)
        item_y.hinzufügen(384.0)
        item_typ.hinzufügen(2)
        item_aktiv.hinzufügen(wahr)
    wenn raum == 1:
        item_x.hinzufügen(288.0)
        item_y.hinzufügen(128.0)
        item_typ.hinzufügen(0)
        item_aktiv.hinzufügen(wahr)
        item_x.hinzufügen(416.0)
        item_y.hinzufügen(352.0)
        item_typ.hinzufügen(0)
        item_aktiv.hinzufügen(wahr)
        item_x.hinzufügen(160.0)
        item_y.hinzufügen(256.0)
        item_typ.hinzufügen(1)
        item_aktiv.hinzufügen(wahr)

funktion items_zeichnen(win, spr):
    setze i auf 0
    solange i < länge(item_x):
        wenn item_aktiv[i]:
            wenn item_typ[i] == 0:
                sprite_zeichnen_skaliert(win, spr["muenze"], item_x[i], item_y[i], 24, 24)
            wenn item_typ[i] == 1:
                sprite_zeichnen_skaliert(win, spr["herz"], item_x[i], item_y[i], 24, 24)
            wenn item_typ[i] == 2:
                sprite_zeichnen_skaliert(win, spr["schluessel"], item_x[i], item_y[i], 24, 24)
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

# === Spieler-State ===
setze px auf 64.0
setze py auf 224.0
setze leben auf 3
setze score auf 0
setze hat_key auf falsch
setze raum_nr auf 0
setze schwert auf 0
setze sdx auf 0
setze sdy auf 1
setze unverw auf 0

# === Karten + Init ===
setze karten auf [erstelle_raum_0(), erstelle_raum_1()]
setze karte auf karten[0]
feinde_init(0)
items_init(0)

# === Raum wechseln ===
funktion wechsel(nr, sx, sy):
    setze raum_nr auf nr
    setze karte auf karten[nr]
    setze px auf sx
    setze py auf sy
    feinde_init(nr)
    items_init(nr)

# === HUD ===
funktion hud(win, spr):
    zeichne_rechteck(win, 0, 0, WIN_W, 28, "#1A1A2E")
    setze i auf 0
    solange i < leben:
        sprite_zeichnen_skaliert(win, spr["herz"], 8 + i * 26, 2, 24, 24)
        setze i auf i + 1
    setze s auf 0
    solange s < score und s < 20:
        sprite_zeichnen_skaliert(win, spr["muenze"], WIN_W - 28 - s * 14, 4, 20, 20)
        setze s auf s + 1
    wenn hat_key:
        sprite_zeichnen_skaliert(win, spr["schluessel"], WIN_W / 2 - 12, 2, 24, 24)

# === Hauptprogramm ===
zeige "=== moo Zelda-Adventure ==="
zeige "WASD=Bewegen, Leertaste=Schwert, Escape=Beenden"

setze win auf fenster_erstelle("moo Zelda", WIN_W, WIN_H)
setze spr auf sprites_laden(win)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # === Bewegung ===
    setze nx auf px
    setze ny auf py
    wenn taste_gedrückt("w"):
        setze ny auf ny - SPEED
        setze sdx auf 0
        setze sdy auf -1
    wenn taste_gedrückt("s"):
        setze ny auf ny + SPEED
        setze sdx auf 0
        setze sdy auf 1
    wenn taste_gedrückt("a"):
        setze nx auf nx - SPEED
        setze sdx auf -1
        setze sdy auf 0
    wenn taste_gedrückt("d"):
        setze nx auf nx + SPEED
        setze sdx auf 1
        setze sdy auf 0

    wenn tile_check(karte, nx, py, SPIELER_SIZE, SPIELER_SIZE) == falsch:
        setze px auf nx
    wenn tile_check(karte, px, ny, SPIELER_SIZE, SPIELER_SIZE) == falsch:
        setze py auf ny

    # === Schwert ===
    wenn taste_gedrückt("leertaste") und schwert == 0:
        setze schwert auf 8
    wenn schwert > 0:
        setze schwert auf schwert - 1

    setze swx auf px + sdx * 24
    setze swy auf py + sdy * 24

    # === Feinde ===
    feinde_update(karte)

    wenn schwert > 0:
        setze i auf 0
        solange i < länge(feinde_x):
            wenn feinde_aktiv[i]:
                wenn aabb(swx, swy, 20, 20, feinde_x[i], feinde_y[i], FEIND_SIZE, FEIND_SIZE):
                    feinde_aktiv[i] = falsch
                    setze score auf score + 10
            setze i auf i + 1

    wenn unverw > 0:
        setze unverw auf unverw - 1
    sonst:
        setze i auf 0
        solange i < länge(feinde_x):
            wenn feinde_aktiv[i]:
                wenn aabb(px, py, SPIELER_SIZE, SPIELER_SIZE, feinde_x[i], feinde_y[i], FEIND_SIZE, FEIND_SIZE):
                    setze leben auf leben - 1
                    setze unverw auf 45
                    wenn leben <= 0:
                        zeige "GAME OVER! Score: " + text(score)
                        stopp
            setze i auf i + 1

    # === Items ===
    setze i auf 0
    solange i < länge(item_x):
        wenn item_aktiv[i]:
            wenn aabb(px, py, SPIELER_SIZE, SPIELER_SIZE, item_x[i], item_y[i], 24, 24):
                wenn item_typ[i] == 0:
                    setze score auf score + 5
                wenn item_typ[i] == 1:
                    setze leben auf min(leben + 1, 5)
                wenn item_typ[i] == 2:
                    setze hat_key auf wahr
                item_aktiv[i] = falsch
        setze i auf i + 1

    # === Tuer ===
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
    karte_zeichnen(win, karte, spr)
    items_zeichnen(win, spr)
    feinde_zeichnen(win, spr)

    # Spieler
    wenn unverw == 0 oder unverw % 4 < 2:
        sprite_zeichnen_skaliert(win, spr["held"], px, py, TILE, TILE)

    # Schwert
    wenn schwert > 0:
        sprite_zeichnen_skaliert(win, spr["schwert"], swx, swy, 24, 24)

    hud(win, spr)
    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Zelda-Adventure beendet. Score: " + text(score)

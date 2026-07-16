# ============================================================
# explorer.moo — 2D Welt-Explorer mit prozeduralem Terrain
#
# Kompilieren: moo-compiler compile beispiele/explorer.moo -o beispiele/explorer
# Starten:     ./beispiele/explorer
#
# Steuerung: WASD=Bewegen, M=Minimap toggle, Escape=Beenden
#
# Features:
#   * 128x128 Tile-Karte (16384 Tiles) mit Noise-Terrain
#   * 6 Biome: Wasser, Strand, Wiese, Wald, Berg, Schnee
#   * Fluesse, Baeume, Steine als Details
#   * Kamera scrollt mit Spieler (zentriert)
#   * Nur sichtbarer Bereich wird gezeichnet (Culling)
#   * Minimap mit Spieler-Position
# ============================================================

konstante TILE auf 16
konstante MAP_SIZE auf 128
konstante VIEW_W auf 40
konstante VIEW_H auf 30
konstante WIN_W auf 640
konstante WIN_H auf 480
konstante SPEED auf 3

# Biom-Typen: 0=Wasser, 1=Strand, 2=Wiese, 3=Wald, 4=Berg, 5=Schnee, 6=Fluss, 7=Weg

# === PRNG / Noise ===
setze noise_seed auf 42

funktion hash2d(ix, iy):
    setze n_val auf ((ix + 1) * 374761 + (iy + 1) * 668265 + noise_seed) % 2147483648
    setze n_val auf (n_val ^ (n_val / 2048)) % 2147483648
    setze n_val auf (n_val * 45673) % 2147483648
    setze n_val auf (n_val ^ (n_val / 32768)) % 2147483648
    gib_zurück n_val

funktion noise2d(x_pos, y_pos):
    setze ix auf boden(x_pos)
    setze iy auf boden(y_pos)
    setze fx auf x_pos - ix
    setze fy auf y_pos - iy
    # Smoothstep
    setze ux auf fx * fx * (3 - 2 * fx)
    setze uy auf fy * fy * (3 - 2 * fy)
    setze a_val auf (hash2d(ix, iy) % 1000) / 500.0 - 1.0
    setze b_val auf (hash2d(ix + 1, iy) % 1000) / 500.0 - 1.0
    setze c_val auf (hash2d(ix, iy + 1) % 1000) / 500.0 - 1.0
    setze d_val auf (hash2d(ix + 1, iy + 1) % 1000) / 500.0 - 1.0
    setze ab auf a_val + (b_val - a_val) * ux
    setze cd auf c_val + (d_val - c_val) * ux
    gib_zurück ab + (cd - ab) * uy

funktion fbm(x_pos, y_pos, oktaven):
    setze wert auf 0.0
    setze amp auf 1.0
    setze freq auf 1.0
    setze max_w auf 0.0
    setze oi auf 0
    solange oi < oktaven:
        setze wert auf wert + noise2d(x_pos * freq, y_pos * freq) * amp
        setze max_w auf max_w + amp
        setze amp auf amp * 0.5
        setze freq auf freq * 2.0
        setze oi auf oi + 1
    gib_zurück wert / max_w

# === Karte generieren ===
setze terrain auf []
setze detail auf []

funktion gen_welt():
    setze terrain auf []
    setze detail auf []
    setze ty auf 0
    solange ty < MAP_SIZE:
        setze tx auf 0
        solange tx < MAP_SIZE:
            # Hoehe aus Noise
            setze hoehe auf fbm(tx * 0.03, ty * 0.03, 4)
            # Feuchtigkeit
            setze feucht auf fbm(tx * 0.02 + 100, ty * 0.02 + 100, 3)

            # Biom bestimmen
            setze biom auf 2
            wenn hoehe < -0.3:
                setze biom auf 0
            sonst wenn hoehe < -0.2:
                setze biom auf 1
            sonst wenn hoehe < 0.1:
                wenn feucht > 0.2:
                    setze biom auf 3
                sonst:
                    setze biom auf 2
            sonst wenn hoehe < 0.35:
                setze biom auf 4
            sonst:
                setze biom auf 5

            # Fluss
            setze fluss_n auf noise2d(tx * 0.025, ty * 0.025)
            wenn abs(fluss_n) < 0.03 und biom != 0 und biom != 5:
                setze biom auf 6

            # Weg
            setze weg_n auf noise2d(tx * 0.04 + 50, ty * 0.04 + 50)
            wenn abs(weg_n) < 0.02 und biom == 2:
                setze biom auf 7

            terrain.hinzufügen(biom)

            # Detail (Baum/Stein)
            setze det auf 0
            setze det_hash auf hash2d(tx * 7 + 333, ty * 13 + 777)
            wenn biom == 3 und det_hash % 100 < 30:
                setze det auf 1
            wenn biom == 2 und det_hash % 100 < 8:
                setze det auf 1
            wenn biom == 4 und det_hash % 100 < 15:
                setze det auf 2
            wenn biom == 2 und det_hash % 100 >= 95:
                setze det auf 2
            detail.hinzufügen(det)

            setze tx auf tx + 1
        setze ty auf ty + 1

# === Biom-Farben ===
funktion biom_farbe(biom):
    wenn biom == 0:
        gib_zurück "#1565C0"
    wenn biom == 1:
        gib_zurück "#FFE082"
    wenn biom == 2:
        gib_zurück "#66BB6A"
    wenn biom == 3:
        gib_zurück "#2E7D32"
    wenn biom == 4:
        gib_zurück "#9E9E9E"
    wenn biom == 5:
        gib_zurück "#ECEFF1"
    wenn biom == 6:
        gib_zurück "#42A5F5"
    wenn biom == 7:
        gib_zurück "#A1887F"
    gib_zurück "#4CAF50"

funktion biom_farbe2(biom):
    wenn biom == 0:
        gib_zurück "#1976D2"
    wenn biom == 1:
        gib_zurück "#FFD54F"
    wenn biom == 2:
        gib_zurück "#81C784"
    wenn biom == 3:
        gib_zurück "#388E3C"
    wenn biom == 4:
        gib_zurück "#BDBDBD"
    wenn biom == 5:
        gib_zurück "#F5F5F5"
    wenn biom == 6:
        gib_zurück "#64B5F6"
    wenn biom == 7:
        gib_zurück "#8D6E63"
    gib_zurück "#66BB6A"

funktion biom_fest(biom):
    gib_zurück biom == 0 oder biom == 4 oder biom == 6

# === Sichtbaren Bereich zeichnen ===
funktion zeichne_welt(win, cam_x, cam_y):
    setze start_tx auf boden(cam_x / TILE)
    setze start_ty auf boden(cam_y / TILE)
    setze off_x auf -(cam_x % TILE)
    setze off_y auf -(cam_y % TILE)

    setze vy auf 0
    solange vy <= VIEW_H:
        setze vx auf 0
        solange vx <= VIEW_W:
            setze tx auf start_tx + vx
            setze ty auf start_ty + vy
            wenn tx >= 0 und tx < MAP_SIZE und ty >= 0 und ty < MAP_SIZE:
                setze idx auf ty * MAP_SIZE + tx
                setze biom auf terrain[idx]
                setze dx auf off_x + vx * TILE
                setze dy auf off_y + vy * TILE

                # Basis-Tile
                zeichne_rechteck(win, dx, dy, TILE, TILE, biom_farbe(biom))

                # Variation
                wenn (tx + ty) % 3 == 0:
                    zeichne_rechteck(win, dx + 2, dy + 2, 4, 4, biom_farbe2(biom))

                # Wasser-Wellen
                wenn biom == 0 oder biom == 6:
                    wenn (tx + ty) % 2 == 0:
                        zeichne_linie(win, dx + 2, dy + TILE / 2, dx + TILE - 2, dy + TILE / 2 - 1, biom_farbe2(biom))

                # Weg-Raender
                wenn biom == 7:
                    zeichne_rechteck(win, dx, dy, TILE, 2, "#795548")
                    zeichne_rechteck(win, dx, dy + TILE - 2, TILE, 2, "#795548")

                # Details
                setze det auf detail[idx]
                wenn det == 1:
                    # Baum
                    zeichne_rechteck(win, dx + 6, dy + 8, 4, 8, "#5D4037")
                    zeichne_kreis(win, dx + 8, dy + 6, 6, "#1B5E20")
                    zeichne_kreis(win, dx + 8, dy + 4, 4, "#2E7D32")
                wenn det == 2:
                    # Stein
                    zeichne_kreis(win, dx + 8, dy + 10, 5, "#757575")
                    zeichne_kreis(win, dx + 8, dy + 9, 3, "#9E9E9E")
            setze vx auf vx + 1
        setze vy auf vy + 1

# === Minimap ===
funktion zeichne_minimap(win, spieler_tx, spieler_ty):
    konstante MM_SIZE auf 100
    konstante MM_X auf WIN_W - MM_SIZE - 8
    konstante MM_Y auf 8
    # Rahmen
    zeichne_rechteck(win, MM_X - 2, MM_Y - 2, MM_SIZE + 4, MM_SIZE + 4, "#000000")
    # Pixel pro Tile
    setze my auf 0
    solange my < MM_SIZE:
        setze mx auf 0
        solange mx < MM_SIZE:
            setze tx auf boden(mx * MAP_SIZE / MM_SIZE)
            setze ty auf boden(my * MAP_SIZE / MM_SIZE)
            wenn tx < MAP_SIZE und ty < MAP_SIZE:
                setze biom auf terrain[ty * MAP_SIZE + tx]
                zeichne_pixel(win, MM_X + mx, MM_Y + my, biom_farbe(biom))
            setze mx auf mx + 2
        setze my auf my + 2
    # Spieler auf Minimap
    setze pmx auf MM_X + spieler_tx * MM_SIZE / MAP_SIZE
    setze pmy auf MM_Y + spieler_ty * MM_SIZE / MAP_SIZE
    zeichne_kreis(win, pmx, pmy, 3, "#F44336")
    # Sichtbereich
    setze vw auf VIEW_W * MM_SIZE / MAP_SIZE
    setze vh auf VIEW_H * MM_SIZE / MAP_SIZE
    zeichne_rechteck(win, pmx - vw / 2, pmy - vh / 2, vw, 1, "#FFFFFF")
    zeichne_rechteck(win, pmx - vw / 2, pmy + vh / 2, vw, 1, "#FFFFFF")
    zeichne_rechteck(win, pmx - vw / 2, pmy - vh / 2, 1, vh, "#FFFFFF")
    zeichne_rechteck(win, pmx + vw / 2, pmy - vh / 2, 1, vh, "#FFFFFF")

# === HUD ===
funktion zeichne_explorer_hud(win, spieler_tx, spieler_ty):
    zeichne_rechteck(win, 0, WIN_H - 24, 200, 24, "#1A1A2E")
    # Position als Punkte
    setze px_dots auf spieler_tx / 4
    setze py_dots auf spieler_ty / 4
    setze di auf 0
    solange di < px_dots und di < 15:
        zeichne_kreis(win, 12 + di * 8, WIN_H - 14, 2, "#42A5F5")
        setze di auf di + 1
    setze di auf 0
    solange di < py_dots und di < 15:
        zeichne_kreis(win, 140 + di * 4, WIN_H - 14, 1, "#FF9800")
        setze di auf di + 1

# === Hauptprogramm ===
zeige "=== moo Welt-Explorer ==="
zeige "Generiere 128x128 Welt..."

setze noise_seed auf zeit_ms() % 99991
gen_welt()

zeige "Welt generiert! WASD=Bewegen, M=Minimap, Escape=Beenden"

setze win auf fenster_erstelle("moo Explorer", WIN_W, WIN_H)

# Spieler in der Mitte starten
setze spieler_x auf MAP_SIZE * TILE / 2
setze spieler_y auf MAP_SIZE * TILE / 2
setze show_minimap auf wahr
setze minimap_cd auf 0

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Bewegung
    setze nx auf spieler_x
    setze ny auf spieler_y
    wenn taste_gedrückt("w") oder taste_gedrückt("oben"):
        setze ny auf ny - SPEED
    wenn taste_gedrückt("s") oder taste_gedrückt("unten"):
        setze ny auf ny + SPEED
    wenn taste_gedrückt("a") oder taste_gedrückt("links"):
        setze nx auf nx - SPEED
    wenn taste_gedrückt("d") oder taste_gedrückt("rechts"):
        setze nx auf nx + SPEED

    # Tile-Kollision
    setze check_tx auf boden(nx / TILE)
    setze check_ty auf boden(ny / TILE)
    wenn check_tx >= 0 und check_tx < MAP_SIZE und check_ty >= 0 und check_ty < MAP_SIZE:
        wenn biom_fest(terrain[check_ty * MAP_SIZE + check_tx]) == falsch:
            setze spieler_x auf nx
            setze spieler_y auf ny
    
    # Welt-Grenzen
    wenn spieler_x < TILE:
        setze spieler_x auf TILE
    wenn spieler_y < TILE:
        setze spieler_y auf TILE
    wenn spieler_x > (MAP_SIZE - 2) * TILE:
        setze spieler_x auf (MAP_SIZE - 2) * TILE
    wenn spieler_y > (MAP_SIZE - 2) * TILE:
        setze spieler_y auf (MAP_SIZE - 2) * TILE

    # Minimap toggle
    wenn minimap_cd > 0:
        setze minimap_cd auf minimap_cd - 1
    wenn taste_gedrückt("m") und minimap_cd == 0:
        setze show_minimap auf show_minimap == falsch
        setze minimap_cd auf 15

    # Kamera (zentriert auf Spieler)
    setze cam_x auf spieler_x - WIN_W / 2
    setze cam_y auf spieler_y - WIN_H / 2
    wenn cam_x < 0:
        setze cam_x auf 0
    wenn cam_y < 0:
        setze cam_y auf 0
    wenn cam_x > MAP_SIZE * TILE - WIN_W:
        setze cam_x auf MAP_SIZE * TILE - WIN_W
    wenn cam_y > MAP_SIZE * TILE - WIN_H:
        setze cam_y auf MAP_SIZE * TILE - WIN_H

    # === Zeichnen ===
    fenster_löschen(win, "#1565C0")
    zeichne_welt(win, cam_x, cam_y)

    # Spieler (relativ zur Kamera)
    setze draw_px auf spieler_x - cam_x
    setze draw_py auf spieler_y - cam_y
    zeichne_kreis(win, draw_px, draw_py, 8, "#F44336")
    zeichne_kreis(win, draw_px, draw_py, 5, "#EF5350")
    # Augen
    zeichne_kreis(win, draw_px - 3, draw_py - 3, 2, "#FFFFFF")
    zeichne_kreis(win, draw_px + 3, draw_py - 3, 2, "#FFFFFF")

    wenn show_minimap:
        setze spieler_tx auf boden(spieler_x / TILE)
        setze spieler_ty auf boden(spieler_y / TILE)
        zeichne_minimap(win, spieler_tx, spieler_ty)

    zeichne_explorer_hud(win, boden(spieler_x / TILE), boden(spieler_y / TILE))
    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Explorer beendet."

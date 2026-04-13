# ============================================================
# moo Welten-Ersteller — Prozedurale 3D-Welt mit Biomen
#
# Kompilieren: moo-compiler run beispiele/welten.moo
# Binary:      moo-compiler compile beispiele/welten.moo -o beispiele/welten
#
# Bedienung:
#   W/S     - Vorwaerts/Rueckwaerts
#   A/D     - Seitwärts (Strafe)
#   Maus    - Umsehen
#   Leertaste - Springen
#   Escape  - Beenden
#
# Features:
#   * Perlin Noise + fBm fuer prozedurales Terrain
#   * 6 Biome: Ozean, Strand, Wiese, Wald, Berg, Schnee
#   * Fluesse via Noise-Schwellwert
#   * Deterministische Baum-Platzierung im Wald
#   * First-Person Kamera mit Gravitation
# ============================================================

# === NOISE-BIBLIOTHEK (Phase 1) ===

setze _prng auf [123456789]
setze _noise_seed auf [0]

funktion seed_setzen(s):
    _prng[0] = s
    setze x auf s
    setze x auf x ^ (x << 13)
    setze x auf x ^ (x >> 7)
    setze x auf x ^ (x << 17)
    _noise_seed[0] = abs(x) & 2147483647

funktion prng_naechste():
    setze x auf _prng[0]
    setze x auf x ^ (x << 13)
    setze x auf x ^ (x >> 7)
    setze x auf x ^ (x << 17)
    _prng[0] = x
    setze r auf abs(x) / 9007199254740992.0
    wenn r > 1.0:
        setze r auf r - boden(r)
    gib_zurück r

funktion hash_2d(ix, iy):
    setze seed auf _noise_seed[0]
    setze n auf ((ix + 1) * 374761 + (iy + 1) * 668265 + seed) & 2147483647
    setze n auf (n ^ (n >> 11)) & 2147483647
    setze n auf (n * 45673) & 2147483647
    setze n auf (n ^ (n >> 15)) & 2147483647
    setze n auf (n * 31337) & 2147483647
    setze n auf (n ^ (n >> 13)) & 2147483647
    gib_zurück n

funktion lerp(a, b, t):
    gib_zurück a + (b - a) * t

funktion fade(t):
    gib_zurück t * t * t * (t * (t * 6 - 15) + 10)

funktion klammer(wert, vmin, vmax):
    gib_zurück max(vmin, min(vmax, wert))

funktion perlin_2d(x, y):
    setze ix auf boden(x)
    setze iy auf boden(y)
    setze fx auf x - ix
    setze fy auf y - iy
    setze u auf fade(fx)
    setze v auf fade(fy)
    setze a auf (hash_2d(ix,     iy    ) % 1000) / 500.0 - 1.0
    setze b auf (hash_2d(ix + 1, iy    ) % 1000) / 500.0 - 1.0
    setze c auf (hash_2d(ix,     iy + 1) % 1000) / 500.0 - 1.0
    setze d auf (hash_2d(ix + 1, iy + 1) % 1000) / 500.0 - 1.0
    gib_zurück lerp(lerp(a, b, u), lerp(c, d, u), v)

funktion fbm(x, y, oktaven):
    setze wert auf 0.0
    setze amplitude auf 1.0
    setze frequenz auf 1.0
    setze max_wert auf 0.0
    setze i auf 0
    solange i < oktaven:
        setze wert auf wert + perlin_2d(x * frequenz, y * frequenz) * amplitude
        setze max_wert auf max_wert + amplitude
        setze amplitude auf amplitude * 0.5
        setze frequenz auf frequenz * 2.0
        setze i auf i + 1
    gib_zurück wert / max_wert

# === BLOCK-KONSTANTEN ===

konstante LUFT auf 0
konstante GRAS auf 1
konstante ERDE auf 2
konstante STEIN auf 3
konstante WASSER auf 4
konstante SAND auf 5
konstante HOLZ auf 6
konstante BLAETTER auf 7
konstante SCHNEE auf 8

konstante MEERESSPIEGEL auf 12
konstante RENDER_DIST auf 16
konstante BAUM_DIST auf 12

# === BLOCK-FARBEN ===

funktion block_farbe(typ):
    wenn typ == GRAS:
        gib_zurück "#4CAF50"
    wenn typ == ERDE:
        gib_zurück "#795548"
    wenn typ == STEIN:
        gib_zurück "#9E9E9E"
    wenn typ == WASSER:
        gib_zurück "#2196F3"
    wenn typ == SAND:
        gib_zurück "#FFE082"
    wenn typ == HOLZ:
        gib_zurück "#5D4037"
    wenn typ == BLAETTER:
        gib_zurück "#2E7D32"
    wenn typ == SCHNEE:
        gib_zurück "#ECEFF1"
    gib_zurück "#FF00FF"

funktion biom_farbe(wx, wz, hoehe):
    setze biom auf biom_bestimmen(wx, wz, hoehe)
    wenn biom == "ozean":
        gib_zurück "#2196F3"
    wenn biom == "strand":
        gib_zurück "#FFE082"
    wenn biom == "wiese":
        gib_zurück "#4CAF50"
    wenn biom == "wald":
        gib_zurück "#2E7D32"
    wenn biom == "berg":
        gib_zurück "#9E9E9E"
    wenn biom == "schnee":
        gib_zurück "#ECEFF1"
    gib_zurück "#4CAF50"

# === TERRAIN-GENERIERUNG (Phase 2) ===

funktion terrain_hoehe(welt_x, welt_z):
    setze kontinent auf fbm(welt_x * 0.005, welt_z * 0.005, 5)
    setze detail auf fbm(welt_x * 0.03, welt_z * 0.03, 3) * 0.3
    setze berg auf fbm(welt_x * 0.015, welt_z * 0.015, 4)
    # Berge nur wo berg-noise hoch ist
    setze berg_faktor auf 0.0
    wenn berg > 0.2:
        setze berg_faktor auf (berg - 0.2) * 60
    setze hoehe auf (kontinent + detail) * 60 + berg_faktor + MEERESSPIEGEL
    gib_zurück boden(klammer(hoehe, 0, 120))

funktion biom_bestimmen(welt_x, welt_z, hoehe):
    wenn hoehe < MEERESSPIEGEL:
        gib_zurück "ozean"
    wenn hoehe <= MEERESSPIEGEL + 1:
        gib_zurück "strand"
    setze feucht auf fbm(welt_x * 0.02 + 100, welt_z * 0.02 + 100, 3)
    wenn hoehe >= 70:
        gib_zurück "schnee"
    wenn hoehe >= 50:
        gib_zurück "berg"
    wenn feucht > 0.1:
        gib_zurück "wald"
    gib_zurück "wiese"

funktion ist_fluss(welt_x, welt_z, hoehe):
    wenn hoehe <= MEERESSPIEGEL oder hoehe >= 45:
        gib_zurück falsch
    setze noise auf perlin_2d(welt_x * 0.025, welt_z * 0.025)
    gib_zurück abs(noise) < 0.04

funktion block_bestimmen(welt_x, y, welt_z):
    setze hoehe auf terrain_hoehe(welt_x, welt_z)
    wenn ist_fluss(welt_x, welt_z, hoehe) und y <= hoehe:
        gib_zurück WASSER
    wenn y > hoehe:
        wenn y <= MEERESSPIEGEL:
            gib_zurück WASSER
        gib_zurück LUFT
    setze biom auf biom_bestimmen(welt_x, welt_z, hoehe)
    wenn y == hoehe:
        wenn biom == "strand":
            gib_zurück SAND
        wenn biom == "schnee":
            gib_zurück SCHNEE
        wenn biom == "berg":
            gib_zurück STEIN
        gib_zurück GRAS
    wenn y >= hoehe - 2:
        gib_zurück ERDE
    gib_zurück STEIN

funktion ist_baum_position(welt_x, welt_z):
    setze hoehe auf terrain_hoehe(welt_x, welt_z)
    setze biom auf biom_bestimmen(welt_x, welt_z, hoehe)
    wenn biom == "wald":
        setze h auf hash_2d(welt_x * 7 + 1000, welt_z * 13 + 2000)
        gib_zurück (h % 100) < 15
    wenn biom == "wiese":
        setze h auf hash_2d(welt_x * 7 + 1000, welt_z * 13 + 2000)
        gib_zurück (h % 100) < 4
    gib_zurück falsch

# === CHUNK-SYSTEM ===

# Chunk-Groesse: 16x16 Bloecke
konstante CHUNK_SIZE auf 16

# Chunk-Cache: Dict mit "cx,cz" → chunk_id
setze chunk_cache auf {}

funktion chunk_key(cx, cz):
    gib_zurück text(cx) + "," + text(cz)

funktion chunk_bauen(win, cx, cz):
    setze chunk auf chunk_erstelle()
    chunk_beginne(chunk)
    setze ox auf cx * CHUNK_SIZE
    setze oz auf cz * CHUNK_SIZE
    # Terrain-Mesh: Grid aus Dreiecken
    setze lx auf 0
    solange lx < CHUNK_SIZE:
        setze lz auf 0
        solange lz < CHUNK_SIZE:
            setze wx auf ox + lx
            setze wz auf oz + lz
            # 4 Ecken mit Hoehe
            setze h00 auf terrain_hoehe(wx, wz)
            setze h10 auf terrain_hoehe(wx + 1, wz)
            setze h01 auf terrain_hoehe(wx, wz + 1)
            setze h11 auf terrain_hoehe(wx + 1, wz + 1)
            # Farbe aus Biom (Durchschnittshoehe)
            setze h_avg auf (h00 + h10 + h01 + h11) / 4
            setze farbe auf biom_farbe(wx, wz, h_avg)
            # Dreieck 1 (CCW von oben: 00 → 01 → 10)
            raum_dreieck(win, wx, h00, wz, wx, h01, wz + 1, wx + 1, h10, wz, farbe)
            # Dreieck 2 (CCW von oben: 10 → 01 → 11)
            raum_dreieck(win, wx + 1, h10, wz, wx, h01, wz + 1, wx + 1, h11, wz + 1, farbe)
            setze lz auf lz + 1
        setze lx auf lx + 1
    # Wasser: Flaches Dreieck-Mesh auf MEERESSPIEGEL
    setze lx auf 0
    solange lx < CHUNK_SIZE:
        setze lz auf 0
        solange lz < CHUNK_SIZE:
            setze wx auf ox + lx
            setze wz auf oz + lz
            setze h auf terrain_hoehe(wx, wz)
            wenn h < MEERESSPIEGEL:
                raum_dreieck(win, wx, MEERESSPIEGEL, wz, wx, MEERESSPIEGEL, wz + 1, wx + 1, MEERESSPIEGEL, wz, "#2196F3")
                raum_dreieck(win, wx + 1, MEERESSPIEGEL, wz, wx, MEERESSPIEGEL, wz + 1, wx + 1, MEERESSPIEGEL, wz + 1, "#2196F3")
            setze lz auf lz + 1
        setze lx auf lx + 1
    # Baeume auf dem Mesh
    setze lx auf 0
    solange lx < CHUNK_SIZE:
        setze lz auf 0
        solange lz < CHUNK_SIZE:
            setze wx auf ox + lx
            setze wz auf oz + lz
            wenn ist_baum_position(wx, wz):
                setze h auf terrain_hoehe(wx, wz)
                setze stamm auf 3 + (hash_2d(wx * 3 + 500, wz * 5 + 700) % 3)
                setze sy auf 1
                solange sy <= stamm:
                    raum_würfel(win, wx, h + sy, wz, 0.4, block_farbe(HOLZ))
                    setze sy auf sy + 1
                setze kronen_r auf 1.2 + (hash_2d(wx * 11, wz * 17) % 5) * 0.15
                raum_kugel(win, wx, h + stamm + 1.5, wz, kronen_r, block_farbe(BLAETTER), 6)
            setze lz auf lz + 1
        setze lx auf lx + 1
    chunk_ende()
    gib_zurück chunk

# Chunks in Render-Distanz holen oder bauen
konstante CHUNK_RENDER auf 4
konstante CHUNK_ENTLADE auf 6

funktion welt_rendern(win, cam_x, cam_z):
    setze pcx auf boden(cam_x / CHUNK_SIZE)
    setze pcz auf boden(cam_z / CHUNK_SIZE)
    setze gebaut auf 0
    # Zeichne und baue Chunks
    setze dx auf 0 - CHUNK_RENDER
    solange dx <= CHUNK_RENDER:
        setze dz auf 0 - CHUNK_RENDER
        solange dz <= CHUNK_RENDER:
            setze cx auf pcx + dx
            setze cz auf pcz + dz
            setze key auf chunk_key(cx, cz)
            wenn chunk_cache[key] == nichts:
                wenn gebaut < 1:
                    chunk_cache[key] = chunk_bauen(win, cx, cz)
                    setze gebaut auf gebaut + 1
            wenn chunk_cache[key] != nichts:
                chunk_zeichne(chunk_cache[key])
            setze dz auf dz + 1
        setze dx auf dx + 1
    # Chunk-Recycling deaktiviert (moo hat keine Dict-Key-Iteration)

# === SPIELER-STATE ===

setze spieler_x auf 0.0
setze spieler_z auf 0.0
setze spieler_y auf 20.0
setze spieler_vy auf 0.0
setze blick_winkel auf 0.0
setze blick_neigung auf 0.0
setze auf_boden auf falsch
konstante GRAVITATION auf 0.01
konstante BEWEGUNG auf 0.15
konstante MAUS_EMPF auf 0.003

# === SPIELER-UPDATE ===

funktion spieler_update(win):
    wenn raum_taste(win, "w"):
        setze spieler_x auf spieler_x + sinus(blick_winkel) * BEWEGUNG
        setze spieler_z auf spieler_z + cosinus(blick_winkel) * BEWEGUNG
    wenn raum_taste(win, "s"):
        setze spieler_x auf spieler_x - sinus(blick_winkel) * BEWEGUNG
        setze spieler_z auf spieler_z - cosinus(blick_winkel) * BEWEGUNG
    wenn raum_taste(win, "a"):
        setze spieler_x auf spieler_x + cosinus(blick_winkel) * BEWEGUNG
        setze spieler_z auf spieler_z - sinus(blick_winkel) * BEWEGUNG
    wenn raum_taste(win, "d"):
        setze spieler_x auf spieler_x - cosinus(blick_winkel) * BEWEGUNG
        setze spieler_z auf spieler_z + sinus(blick_winkel) * BEWEGUNG
    # Maus-Steuerung
    setze mdx auf raum_maus_dx(win)
    setze mdy auf raum_maus_dy(win)
    setze blick_winkel auf blick_winkel - mdx * MAUS_EMPF
    setze blick_neigung auf blick_neigung - mdy * MAUS_EMPF
    wenn blick_neigung > 1.4:
        setze blick_neigung auf 1.4
    wenn blick_neigung < -1.4:
        setze blick_neigung auf -1.4
    wenn raum_taste(win, "leertaste") und auf_boden:
        setze spieler_vy auf 0.2
    setze spieler_vy auf spieler_vy - GRAVITATION
    setze spieler_y auf spieler_y + spieler_vy
    setze gx auf boden(spieler_x)
    setze gz auf boden(spieler_z)
    setze boden_h auf terrain_hoehe(gx, gz) + 1.8
    wenn spieler_y < boden_h:
        setze spieler_y auf boden_h
        setze spieler_vy auf 0.0
        setze auf_boden auf wahr
    sonst:
        setze auf_boden auf falsch

# === HAUPTPROGRAMM ===

zeige "=== moo Welten-Ersteller ==="
zeige "Generiere prozedurale Welt mit Perlin Noise..."
zeige "WASD = Bewegen/Drehen, Leertaste = Springen, Escape = Beenden"

seed_setzen(42)

setze win auf raum_erstelle("moo Welten", 1024, 768)
raum_perspektive(win, 70.0, 0.1, 250.0)
raum_maus_fangen(win)

# Startposition
setze spieler_x auf 0.0
setze spieler_z auf 0.0
setze spieler_y auf terrain_hoehe(0, 0) + 3.0

# FPS-Tracking
setze fps auf 0
setze frame_count auf 0
setze fps_start auf zeit_ms()

solange raum_offen(win):
    wenn raum_taste(win, "escape"):
        stopp

    spieler_update(win)

    setze look_x auf spieler_x + sinus(blick_winkel) * cosinus(blick_neigung) * 5.0
    setze look_y auf spieler_y + sinus(blick_neigung) * 5.0
    setze look_z auf spieler_z + cosinus(blick_winkel) * cosinus(blick_neigung) * 5.0

    raum_löschen(win, 0.53, 0.81, 0.92)
    raum_kamera(win, spieler_x, spieler_y, spieler_z, look_x, look_y, look_z)

    welt_rendern(win, spieler_x, spieler_z)

    # Sonne (folgt Spieler, Richtung passend zu lightDir)
    raum_kugel(win, spieler_x + 700, spieler_y + 400, spieler_z + 500, 20, "#FFD700", 8)

    raum_aktualisieren(win)

    setze frame_count auf frame_count + 1
    setze verstrichen auf zeit_ms() - fps_start
    wenn verstrichen >= 1000:
        setze fps auf boden(frame_count * 1000 / verstrichen)
        zeige "FPS: " + text(fps)
        setze frame_count auf 0
        setze fps_start auf zeit_ms()

    warte(16)

raum_schliessen(win)
zeige "Welten-Ersteller beendet"

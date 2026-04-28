# ============================================================
# City Builder — Schraeg-Iso-View auf die prozedurale moo-Welt
#
# WIEDERVERWENDUNG: Diese Datei nutzt die prozedurale Terrain-Welt
# aus beispiele/domain/game/world/welten_render_test.moo unveraendert
# (Block-Konstanten, block_farbe, terrain_init, terrain_hoehe,
# terrain_block, terrain_rendern). Es gibt KEIN eigenes Grid und
# KEINE Tile-Hash-Logik mehr.
#
# Der Unterschied zum Welt-Renderer-Test ist NUR:
#   - Iso-Kamera (~50 Grad Elevation) statt First-Person
#   - WASD pannt die Kamera, kein Spieler/Gravitation/Springen
#   - Optionaler Welt-Cursor zum spaeteren Bauen
#
# Kompilieren / Starten:
#   MOO_3D_BACKEND=vulkan moo-compiler run beispiele/city_builder/city_builder.moo
#
# Steuerung:
#   W / A / S / D    → Kamera pannen (Norden/West/Sued/Ost) — alternativ: Linke Maustaste ziehen
#   Q / E            → Heran-/Wegzoomen — alternativ: Mausrad scrollen
#   Z / X            → Yaw rotieren — alternativ: Mittlere Maustaste horizontal ziehen
#   R / F            → Tilt (Elevation) — alternativ: Mittlere Maustaste vertikal ziehen
#   Pfeiltasten      → Welt-Cursor verschieben
#   Escape           → Beenden
# ============================================================


# ============================================================
# === BLOCK-DEFINITIONEN (1:1 aus welten_render_test.moo) ===
# ============================================================
konstante LUFT auf 0
konstante GRAS auf 1
konstante ERDE auf 2
konstante STEIN auf 3
konstante WASSER auf 4
konstante SAND auf 5
konstante HOLZ auf 6
konstante BLAETTER auf 7
konstante SCHNEE auf 8

konstante WELT_B auf 200
konstante WELT_T auf 200
konstante WELT_H auf 22
konstante RENDER_DIST auf 200

# Farben pro Block-Typ (identisch zum Welt-Test)
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


# ============================================================
# === TERRAIN-GENERIERUNG (1:1 aus welten_render_test.moo) ===
# ============================================================
setze terrain_hoehen auf []

# Multi-Oktav-Noise via aufaddierte Sinus/Cosinus-Wellen mit
# unterschiedlichen Frequenzen und Phasen — keine sichtbare
# Periodizitaet (keine Streifen wie bei einfachem sinus(x*0.4)).
funktion welt_noise(gx, gz):
    # Niederfrequente Hauptberge
    setze a auf sinus(gx * 0.05) * cosinus(gz * 0.04) * 4.5
    # Mittlere Huegel mit Diagonal-Versatz
    setze b auf sinus((gx + gz * 0.7) * 0.11 + 1.3) * 2.8
    # Mittlere Huegel mit anderer Diagonale
    setze c auf cosinus((gx * 0.9 - gz) * 0.13 + 2.7) * 2.2
    # Hochfrequente Aufrauhung
    setze d auf sinus(gx * 0.27 + gz * 0.31 + 4.1) * 1.4
    # Sehr feine Rauschstruktur
    setze e auf cosinus(gx * 0.43 - gz * 0.39 + 5.2) * 0.8
    # Globaler langsamer Trend (eine grosse Geste)
    setze f auf sinus((gx - gz) * 0.018) * 2.5
    gib_zurück 7.5 + a + b + c + d + e + f

funktion terrain_init():
    # KEIN `setze terrain_hoehen auf []` hier (Scoping-Trap, siehe oben)
    setze z auf 0
    solange z < WELT_T:
        setze x auf 0
        solange x < WELT_B:
            setze h auf boden(welt_noise(x, z))
            wenn h < 1:
                setze h auf 1
            wenn h > WELT_H - 2:
                setze h auf WELT_H - 2
            terrain_hoehen.hinzufügen(h)
            setze x auf x + 1
        setze z auf z + 1

funktion terrain_hoehe(x, z):
    wenn x < 0 oder x >= WELT_B oder z < 0 oder z >= WELT_T:
        gib_zurück 5
    gib_zurück terrain_hoehen[z * WELT_B + x]

funktion terrain_block(x, y, z):
    setze h auf terrain_hoehe(x, z)
    wenn y > h:
        gib_zurück LUFT
    wenn y == h:
        wenn h <= 6:
            gib_zurück SAND
        wenn h >= 13:
            gib_zurück SCHNEE
        gib_zurück GRAS
    wenn y >= h - 2:
        gib_zurück ERDE
    gib_zurück STEIN

# Liefert den Block-Typ fuer eine Tile-Hoehe (entscheidet Farbe).
funktion oberflaechen_typ(h):
    wenn h <= 6:
        gib_zurück SAND
    wenn h >= 13:
        gib_zurück SCHNEE
    gib_zurück GRAS

# Heightmesh-Renderer: pro Tile ein Quad aus 2 Dreiecken, Eck-Hoehen
# aus terrain_hoehe -> sanfte Haenge statt eckige Wuerfelstapel.
# Wasser bei Hoehe < 6 wird als flaches Quad-Paar darueber gelegt.
# Statisches Boden-Mesh in einem Chunk: Heightmesh aus 2 Dreiecken pro
# Tile + Wasser-Quad in Senken. Wird einmalig nach terrain_init() gebaut
# und pro Frame mit chunk_zeichne() in EINEM Draw-Call gerendert. Bei
# 200x200 Welt = 80000 Boden-Dreiecke + Wasser, kein Performance-Problem
# fuer Vulkan/GL3.3 (statisches VBO).
setze boden_chunk auf -1

funktion boden_chunk_baue(win):
    setze boden_chunk auf chunk_erstelle()
    chunk_beginne(boden_chunk)
    setze x auf 0
    solange x < WELT_B - 1:
        setze z auf 0
        solange z < WELT_T - 1:
            setze h_nw auf terrain_hoehe(x, z)
            setze h_ne auf terrain_hoehe(x + 1, z)
            setze h_sw auf terrain_hoehe(x, z + 1)
            setze h_se auf terrain_hoehe(x + 1, z + 1)
            setze h_mid auf (h_nw + h_ne + h_sw + h_se) / 4.0
            setze farbe auf block_farbe(oberflaechen_typ(h_mid))
            # Konstante Triangulierung wie in welten.moo:
            #   Tri 1: nw -> sw -> ne (CCW von oben)
            #   Tri 2: ne -> sw -> se (CCW von oben)
            raum_dreieck(win, x, h_nw, z, x, h_sw, z + 1, x + 1, h_ne, z, farbe)
            raum_dreieck(win, x + 1, h_ne, z, x, h_sw, z + 1, x + 1, h_se, z + 1, farbe)
            wenn h_mid < 5.5:
                raum_dreieck(win, x, 6.0, z, x, 6.0, z + 1, x + 1, 6.0, z, "#2196F3")
                raum_dreieck(win, x + 1, 6.0, z, x, 6.0, z + 1, x + 1, 6.0, z + 1, "#2196F3")
            setze z auf z + 1
        setze x auf x + 1
    chunk_ende()


# ============================================================
# === ISO-KAMERA (Schraeg-von-oben, City-Builder-Look) ======
# ============================================================
# Elevations-Winkel ~50 Grad ueber Horizont, lookAt etwas ueber Boden
# fuer Volumen-Eindruck.
# Default-Elevation 40° — nun aber dynamisch (R/F kippen),
# zwischen 15° (fast horizontal) und 80° (fast top-down) clamping.
setze cam_elevation auf 0.6981317   # 40° in Radiant

setze cam_x auf WELT_B / 2.0
setze cam_z auf WELT_T / 2.0
setze cam_zoom auf 130.0
setze cam_azimuth auf 0.0

# Iso-Kamera mit horizontalem Drehwinkel (azimuth). Auge bewegt sich
# auf einem Kreis um (fokus_x, fokus_z) mit Radius cos(elev)*zoom.
# Orbit-Kamera mit dynamischer Elevation + Azimuth.
funktion camera_iso(win, fokus_x, fokus_z, zoom, azimuth, elevation):
    setze auge_y auf zoom * sinus(elevation)
    setze radius auf zoom * cosinus(elevation)
    setze auge_x auf fokus_x + radius * sinus(azimuth)
    setze auge_z auf fokus_z + radius * cosinus(azimuth)
    raum_kamera(win, auge_x, auge_y, auge_z, fokus_x, 5.0, fokus_z)


# ============================================================
# === WELT-CURSOR (auf Terrain-Oberflaeche) =================
# ============================================================
setze cursor_x auf boden(WELT_B / 2)
setze cursor_z auf boden(WELT_T / 2)

# Pulsierende Marker-Saeule auf der Cursor-Position. Hoehe richtet
# sich nach dem Terrain, sodass der Cursor immer auf der Oberflaeche
# steht.
funktion cursor_render(win, cx, cz, puls):
    setze h auf terrain_hoehe(cx, cz)
    # Hauptmarker: leuchtend gelber Wuerfel
    raum_würfel(win, cx, h + 1.2 + puls * 0.3, cz, 0.5, "#FFEB3B")
    # Kleinere weisse Spitze obendrauf
    raum_würfel(win, cx, h + 1.9 + puls * 0.4, cz, 0.25, "#FFFFFF")
    # Boden-Highlight (transparent-wirkender flacher Wuerfel)
    raum_würfel(win, cx, h + 0.55, cz, 0.95, "#FFEB3B")


# ============================================================
# === HAUPT-PROGRAMM ========================================
# ============================================================
zeige "=== moo City Builder (Iso-View auf prozedurale Welt) ==="
zeige "Wiederverwendet: welten_render_test.moo Terrain-Logik"

terrain_init()
zeige "Terrain generiert (" + text(WELT_B) + "x" + text(WELT_T) + ")"

setze win auf raum_erstelle("moo City Builder", 1280, 800)
raum_perspektive(win, 50.0, 0.1, 800.0)
# Kein raum_maus_fangen — wir nutzen absolute Maus-Positionen + Buttons
# (raum_maus_x/y, raum_maus_taste, raum_maus_rad).

# Maus-State fuer Click-Drag-Tracking. Initialisiere auf aktuelle Position
# damit der erste Frame keinen Riesen-Delta hat.
setze last_maus_x auf raum_maus_x(win)
setze last_maus_y auf raum_maus_y(win)

# Boden-Mesh einmalig in Chunk packen — spart pro Frame ~80000 Draw-Calls
zeige "Baue Boden-Chunk (" + text((WELT_B - 1) * (WELT_T - 1) * 2) + " Dreiecke)..."
boden_chunk_baue(win)
zeige "Boden-Chunk fertig."

setze taste_cooldown auf 0
setze pulsphase auf 0.0

# FPS-Tracking
setze fps auf 0
setze frame_count auf 0
setze fps_start auf zeit_ms()

solange raum_offen(win):
    wenn raum_taste(win, "escape"):
        stopp

    # === Kamera-Pan mit WASD — entlang der aktuellen Drehung ===
    # "vorne" zeigt vom Auge weg in Welt-Mitte; rotation per cam_azimuth.
    setze pan_speed auf 0.7
    setze fwd_x auf 0 - sinus(cam_azimuth) * pan_speed
    setze fwd_z auf 0 - cosinus(cam_azimuth) * pan_speed
    setze rt_x  auf cosinus(cam_azimuth) * pan_speed
    setze rt_z  auf 0 - sinus(cam_azimuth) * pan_speed
    wenn raum_taste(win, "w"):
        setze cam_x auf cam_x + fwd_x
        setze cam_z auf cam_z + fwd_z
    wenn raum_taste(win, "s"):
        setze cam_x auf cam_x - fwd_x
        setze cam_z auf cam_z - fwd_z
    wenn raum_taste(win, "d"):
        setze cam_x auf cam_x + rt_x
        setze cam_z auf cam_z + rt_z
    wenn raum_taste(win, "a"):
        setze cam_x auf cam_x - rt_x
        setze cam_z auf cam_z - rt_z

    # === Maus-Steuerung (Task D) ===
    # Absolute Position pro Frame -> Delta gegenueber letztem Frame.
    setze mx auf raum_maus_x(win)
    setze my auf raum_maus_y(win)
    setze mdx auf mx - last_maus_x
    setze mdy auf my - last_maus_y
    setze last_maus_x auf mx
    setze last_maus_y auf my

    # Linke Maustaste gedrueckt + Bewegung -> Karte panen.
    # Unit-Vektoren forward/right (cam_azimuth-rotated), Pan-Faktor
    # skaliert mit Zoom (entferntere Kamera = mehr Pan pro Pixel).
    wenn raum_maus_taste(win, "links"):
        setze ufwd_x auf 0 - sinus(cam_azimuth)
        setze ufwd_z auf 0 - cosinus(cam_azimuth)
        setze urt_x auf cosinus(cam_azimuth)
        setze urt_z auf 0 - sinus(cam_azimuth)
        setze pan_factor auf cam_zoom * 0.0015
        # Maus rechts -> Kamera links (Welt scheint mit Cursor zu gehen)
        setze cam_x auf cam_x - urt_x * mdx * pan_factor
        setze cam_z auf cam_z - urt_z * mdx * pan_factor
        # Maus runter -> Kamera zurueck (Welt schiebt sich nach unten)
        setze cam_x auf cam_x - ufwd_x * mdy * pan_factor
        setze cam_z auf cam_z - ufwd_z * mdy * pan_factor

    # Mittlere Maustaste + Bewegung -> Yaw (mdx) + Tilt (mdy).
    wenn raum_maus_taste(win, "mitte"):
        setze cam_azimuth auf cam_azimuth + mdx * 0.008
        setze cam_elevation auf cam_elevation - mdy * 0.005
        wenn cam_elevation < 0.25:
            setze cam_elevation auf 0.25
        wenn cam_elevation > 1.4:
            setze cam_elevation auf 1.4

    # Mausrad -> Zoom. raum_maus_rad ist consume-on-read: liefert Delta
    # seit letztem Aufruf und resetet auf 0.
    setze rad_dy auf raum_maus_rad(win)
    wenn rad_dy != 0:
        setze cam_zoom auf cam_zoom - rad_dy * 4.0
        wenn cam_zoom < 20.0:
            setze cam_zoom auf 20.0
        wenn cam_zoom > 400.0:
            setze cam_zoom auf 400.0

    # Tasten-Fallbacks fuer Yaw (Z/X)
    wenn raum_taste(win, "z"):
        setze cam_azimuth auf cam_azimuth - 0.03
    wenn raum_taste(win, "x"):
        setze cam_azimuth auf cam_azimuth + 0.03

    # === Tilt (Elevation) per R / F ===
    wenn raum_taste(win, "r"):
        setze cam_elevation auf cam_elevation + 0.02
        wenn cam_elevation > 1.4:
            setze cam_elevation auf 1.4
    wenn raum_taste(win, "f"):
        setze cam_elevation auf cam_elevation - 0.02
        wenn cam_elevation < 0.25:
            setze cam_elevation auf 0.25

    # Zoom
    wenn raum_taste(win, "q"):
        setze cam_zoom auf cam_zoom - 1.2
        wenn cam_zoom < 20.0:
            setze cam_zoom auf 20.0
    wenn raum_taste(win, "e"):
        setze cam_zoom auf cam_zoom + 1.2
        wenn cam_zoom > 400.0:
            setze cam_zoom auf 400.0

    # === Welt-Cursor mit Pfeiltasten ===
    wenn taste_cooldown > 0:
        setze taste_cooldown auf taste_cooldown - 1

    wenn taste_cooldown == 0:
        wenn raum_taste(win, "links") und cursor_x > 0:
            setze cursor_x auf cursor_x - 1
            setze taste_cooldown auf 6
        wenn raum_taste(win, "rechts") und cursor_x < WELT_B - 1:
            setze cursor_x auf cursor_x + 1
            setze taste_cooldown auf 6
        wenn raum_taste(win, "oben") und cursor_z > 0:
            setze cursor_z auf cursor_z - 1
            setze taste_cooldown auf 6
        wenn raum_taste(win, "unten") und cursor_z < WELT_T - 1:
            setze cursor_z auf cursor_z + 1
            setze taste_cooldown auf 6
        # R fuer Welt-Neuladen aktuell deaktiviert (terrain_init darf
        # nur einmal laufen, sonst wuerden Hoehen doppelt angehaengt).

    # === Render ===
    raum_löschen(win, 0.53, 0.81, 0.92)
    camera_iso(win, cam_x, cam_z, cam_zoom, cam_azimuth, cam_elevation)

    # Statisches Boden-Mesh in einem Draw-Call.
    chunk_zeichne(boden_chunk)

    setze pulsphase auf pulsphase + 0.08
    setze puls auf (sinus(pulsphase) + 1.0) / 2.0
    cursor_render(win, cursor_x, cursor_z, puls)

    raum_aktualisieren(win)

    # FPS
    setze frame_count auf frame_count + 1
    setze verstrichen auf zeit_ms() - fps_start
    wenn verstrichen >= 2000:
        setze fps auf boden(frame_count * 1000 / verstrichen)
        zeige "FPS: " + text(fps) + " (Cursor=" + text(cursor_x) + "," + text(cursor_z) + ")"
        setze frame_count auf 0
        setze fps_start auf zeit_ms()

    warte(16)

raum_schliessen(win)
zeige "=== City Builder beendet ==="

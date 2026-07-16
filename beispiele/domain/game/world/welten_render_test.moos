# ============================================================
# moo Welt-Renderer Test — 3D-Terrain mit WASD-Steuerung
#
# Kompilieren: moo-compiler run beispiele/domain/game/world/welten_render_test.moo
#
# Bedienung:
#   W/S     → Vorwaerts/Rueckwaerts
#   A/D     → Links/Rechts drehen
#   Leertaste → Springen
#   Escape  → Beenden
#
# Features:
#   * Dummy-Terrain (Sinus/Cosinus-Wellen)
#   * Block-Typen: Gras, Erde, Stein, Sand, Schnee, Wasser
#   * First-Person Kamera mit Gravitation
#   * Sichtbarkeits-Optimierung (nur Oberflaechen-Bloecke)
# ============================================================

# --- Block-Konstanten ---
konstante LUFT auf 0
konstante GRAS auf 1
konstante ERDE auf 2
konstante STEIN auf 3
konstante WASSER auf 4
konstante SAND auf 5
konstante HOLZ auf 6
konstante BLAETTER auf 7
konstante SCHNEE auf 8

# --- Welt-Groesse ---
konstante WELT_B auf 32
konstante WELT_T auf 32
konstante WELT_H auf 16
konstante RENDER_DIST auf 16

# --- Block-Farben ---
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

# --- Dummy-Terrain ---
setze terrain_hoehen auf []

funktion terrain_init():
    setze z auf 0
    solange z < WELT_T:
        setze x auf 0
        solange x < WELT_B:
            setze h auf boden(6 + sinus(x * 0.4) * 3 + cosinus(z * 0.3) * 2)
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

# --- Terrain-Renderer ---
funktion terrain_rendern(win, cam_x, cam_z):
    setze cx_min auf max(0, boden(cam_x) - RENDER_DIST)
    setze cx_max auf min(WELT_B - 1, boden(cam_x) + RENDER_DIST)
    setze cz_min auf max(0, boden(cam_z) - RENDER_DIST)
    setze cz_max auf min(WELT_T - 1, boden(cam_z) + RENDER_DIST)

    setze x auf cx_min
    solange x <= cx_max:
        setze z auf cz_min
        solange z <= cz_max:
            setze h auf terrain_hoehe(x, z)
            # Oberflaechenblock rendern
            setze typ auf terrain_block(x, h, z)
            wenn typ != LUFT:
                raum_würfel(win, x, h, z, 1.0, block_farbe(typ))
            # Wasser auf Meeresspiegel (Hoehe 6)
            wenn h < 6:
                raum_würfel(win, x, 6, z, 1.0, "#2196F3")
            setze z auf z + 1
        setze x auf x + 1

# --- Spieler-State ---
setze spieler_x auf 16.0
setze spieler_z auf 16.0
setze spieler_y auf 12.0
setze spieler_vy auf 0.0
setze blick_winkel auf 0.0
setze auf_boden auf falsch
konstante GRAVITATION auf 0.01
konstante BEWEGUNG auf 0.15

# --- Spieler-Update ---
funktion spieler_update(win):
    # Vorwaerts/Rueckwaerts
    wenn raum_taste(win, "w"):
        setze spieler_x auf spieler_x + sinus(blick_winkel) * BEWEGUNG
        setze spieler_z auf spieler_z + cosinus(blick_winkel) * BEWEGUNG
    wenn raum_taste(win, "s"):
        setze spieler_x auf spieler_x - sinus(blick_winkel) * BEWEGUNG
        setze spieler_z auf spieler_z - cosinus(blick_winkel) * BEWEGUNG

    # Links/Rechts drehen
    wenn raum_taste(win, "a"):
        setze blick_winkel auf blick_winkel - 0.04
    wenn raum_taste(win, "d"):
        setze blick_winkel auf blick_winkel + 0.04

    # Springen
    wenn raum_taste(win, "leertaste") und auf_boden:
        setze spieler_vy auf 0.2

    # Gravitation
    setze spieler_vy auf spieler_vy - GRAVITATION
    setze spieler_y auf spieler_y + spieler_vy

    # Boden-Kollision
    setze gx auf boden(spieler_x)
    setze gz auf boden(spieler_z)
    setze boden_h auf terrain_hoehe(gx, gz) + 1.8
    wenn spieler_y < boden_h:
        setze spieler_y auf boden_h
        setze spieler_vy auf 0.0
        setze auf_boden auf wahr
    sonst:
        setze auf_boden auf falsch

# --- Haupt-Spielschleife ---
zeige "=== moo Welt-Renderer Test ==="
zeige "WASD = Bewegen/Drehen, Leertaste = Springen, Escape = Beenden"

terrain_init()
zeige "Terrain generiert (" + text(WELT_B) + "x" + text(WELT_T) + ")"

setze win auf raum_erstelle("moo Welt-Test", 1024, 768)
raum_perspektive(win, 60.0, 0.1, 150.0)

# Startposition: Mitte der Welt, ueber Terrain
setze spieler_x auf 16.0
setze spieler_z auf 16.0
setze spieler_y auf terrain_hoehe(16, 16) + 3.0

# FPS-Tracking
setze fps auf 0
setze frame_count auf 0
setze fps_start auf zeit_ms()

solange raum_offen(win):
    wenn raum_taste(win, "escape"):
        stopp

    spieler_update(win)

    # Kamera (First-Person)
    setze look_x auf spieler_x + sinus(blick_winkel) * 5.0
    setze look_z auf spieler_z + cosinus(blick_winkel) * 5.0

    raum_löschen(win, 0.53, 0.81, 0.92)
    raum_kamera(win, spieler_x, spieler_y, spieler_z, look_x, spieler_y, look_z)

    terrain_rendern(win, spieler_x, spieler_z)

    raum_aktualisieren(win)

    # FPS messen
    setze frame_count auf frame_count + 1
    setze verstrichen auf zeit_ms() - fps_start
    wenn verstrichen >= 1000:
        setze fps auf boden(frame_count * 1000 / verstrichen)
        zeige "FPS: " + text(fps)
        setze frame_count auf 0
        setze fps_start auf zeit_ms()

    warte(16)

raum_schliessen(win)
zeige "Welt-Renderer beendet"

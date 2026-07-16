# ============================================================
# moo Strategie — Mini-Civilization
#
# Kompilieren: moo-compiler compile beispiele/strategie.moo -o beispiele/strategie
# Starten:     ./beispiele/strategie
#
# Maus = Einheit waehlen + Ziel setzen
# WASD = Karte scrollen
# Leertaste = Runde beenden
# 1 = Krieger bauen, 2 = Arbeiter bauen
# Escape = Beenden
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600
setze TILE auf 24
setze MAP_W auf 60
setze MAP_H auf 40
setze MAX_EINHEITEN auf 50

# Kamera
setze cam_x auf 10
setze cam_y auf 5

# === PROZEDURALE KARTE ===
# 0=Wasser, 1=Gras, 2=Wald, 3=Berg, 4=Wueste, 5=Stadt
setze karte auf []
setze ressourcen auf []

# Einfacher Noise: Hash + Glaettung
funktion pseudo_noise(px, py, seed):
    setze n auf (px * 374761 + py * 668265 + seed * 31337) % 1000
    wenn n < 0:
        setze n auf 0 - n
    gib_zurück n / 1000.0

funktion terrain_gen(px, py):
    setze hoehe auf pseudo_noise(px, py, 42)
    setze feuchte auf pseudo_noise(px, py, 137)
    setze temp auf pseudo_noise(px, py, 999)
    # Glaettung: Nachbar-Durchschnitt
    setze h2 auf pseudo_noise(px + 1, py, 42)
    setze h3 auf pseudo_noise(px, py + 1, 42)
    setze h4 auf pseudo_noise(px - 1, py, 42)
    setze h5 auf pseudo_noise(px, py - 1, 42)
    setze hoehe auf (hoehe * 2 + h2 + h3 + h4 + h5) / 6.0
    wenn hoehe < 0.3:
        gib_zurück 0
    wenn hoehe > 0.75:
        gib_zurück 3
    wenn feuchte > 0.6 und hoehe > 0.4:
        gib_zurück 2
    wenn temp > 0.7 und feuchte < 0.35:
        gib_zurück 4
    gib_zurück 1

# Karte generieren
setze my auf 0
solange my < MAP_H:
    setze mx auf 0
    solange mx < MAP_W:
        setze terrain auf terrain_gen(mx, my)
        karte.hinzufügen(terrain)
        # Ressourcen: Wald=Holz, Berg=Stein
        wenn terrain == 2:
            ressourcen.hinzufügen(3)
        sonst:
            wenn terrain == 3:
                ressourcen.hinzufügen(2)
            sonst:
                ressourcen.hinzufügen(0)
        setze mx auf mx + 1
    setze my auf my + 1

# === EINHEITEN ===
# Typ: 0=Krieger, 1=Arbeiter
setze ein_x auf []
setze ein_y auf []
setze ein_typ auf []
setze ein_hp auf []
setze ein_team auf []
setze ein_aktiv auf []
setze ein_bewegt auf []

funktion einheit_erstellen(ex, ey, etyp, eteam):
    ein_x.hinzufügen(ex)
    ein_y.hinzufügen(ey)
    ein_typ.hinzufügen(etyp)
    wenn etyp == 0:
        ein_hp.hinzufügen(10)
    sonst:
        ein_hp.hinzufügen(5)
    ein_team.hinzufügen(eteam)
    ein_aktiv.hinzufügen(wahr)
    ein_bewegt.hinzufügen(falsch)

# Start-Einheiten Spieler (Team 0)
# Finde gutes Startfeld (Gras)
setze start_x auf 10
setze start_y auf 10
setze sy auf 5
solange sy < MAP_H - 5:
    setze sx auf 5
    solange sx < MAP_W - 5:
        wenn karte[sy * MAP_W + sx] == 1:
            setze start_x auf sx
            setze start_y auf sy
            setze sx auf MAP_W
            setze sy auf MAP_H
        setze sx auf sx + 1
    setze sy auf sy + 1

einheit_erstellen(start_x, start_y, 0, 0)
einheit_erstellen(start_x + 1, start_y, 0, 0)
einheit_erstellen(start_x, start_y + 1, 1, 0)

# Feind-Einheiten (Team 1)
setze feind_start_x auf MAP_W - 15
setze feind_start_y auf MAP_H - 15
setze fy auf MAP_H - 10
solange fy > 5:
    setze fx auf MAP_W - 10
    solange fx > 5:
        wenn karte[fy * MAP_W + fx] == 1:
            setze feind_start_x auf fx
            setze feind_start_y auf fy
            setze fx auf 0
            setze fy auf 0
        setze fx auf fx - 1
    setze fy auf fy - 1

einheit_erstellen(feind_start_x, feind_start_y, 0, 1)
einheit_erstellen(feind_start_x - 1, feind_start_y, 0, 1)
einheit_erstellen(feind_start_x, feind_start_y - 1, 1, 1)

setze einheit_anzahl auf länge(ein_x)

# Stadt auf Start-Position setzen
setze karte[start_y * MAP_W + start_x] auf 5

# Spieler-Ressourcen
setze holz auf 10
setze stein auf 5
setze gold auf 0
setze runde auf 1

# Selection
setze auswahl auf -1
setze cursor_mx auf 0
setze cursor_my auf 0
setze eingabe_cd auf 0

funktion tile_farbe(typ):
    wenn typ == 0:
        gib_zurück "#1565C0"
    wenn typ == 1:
        gib_zurück "#4CAF50"
    wenn typ == 2:
        gib_zurück "#2E7D32"
    wenn typ == 3:
        gib_zurück "#78909C"
    wenn typ == 4:
        gib_zurück "#FFD54F"
    wenn typ == 5:
        gib_zurück "#FF9800"
    gib_zurück "#000000"

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Strategie", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # === KAMERA SCROLLEN ===
    wenn eingabe_cd <= 0:
        wenn taste_gedrückt("w"):
            setze cam_y auf cam_y - 1
            setze eingabe_cd auf 4
        wenn taste_gedrückt("s"):
            setze cam_y auf cam_y + 1
            setze eingabe_cd auf 4
        wenn taste_gedrückt("a"):
            setze cam_x auf cam_x - 1
            setze eingabe_cd auf 4
        wenn taste_gedrückt("d"):
            setze cam_x auf cam_x + 1
            setze eingabe_cd auf 4

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    wenn cam_x < 0:
        setze cam_x auf 0
    wenn cam_y < 0:
        setze cam_y auf 0
    wenn cam_x > MAP_W - BREITE / TILE:
        setze cam_x auf MAP_W - BREITE / TILE
    wenn cam_y > MAP_H - (HOEHE - 80) / TILE:
        setze cam_y auf MAP_H - (HOEHE - 80) / TILE

    # === MAUS-INTERAKTION ===
    setze mx auf maus_x(win)
    setze my auf maus_y(win)
    setze cursor_mx auf cam_x + mx / TILE
    setze cursor_my auf cam_y + my / TILE

    wenn maus_gedrückt(win):
        wenn my < HOEHE - 80:
            # Einheit auswaehlen oder bewegen
            setze gefunden auf -1
            setze ei auf 0
            solange ei < einheit_anzahl:
                wenn ein_aktiv[ei] und ein_team[ei] == 0:
                    wenn ein_x[ei] == cursor_mx und ein_y[ei] == cursor_my:
                        setze gefunden auf ei
                setze ei auf ei + 1

            wenn gefunden >= 0:
                setze auswahl auf gefunden
            sonst:
                # Ausgewaehlte Einheit bewegen
                wenn auswahl >= 0 und nicht ein_bewegt[auswahl]:
                    setze ziel_terrain auf karte[cursor_my * MAP_W + cursor_mx]
                    wenn ziel_terrain != 0 und ziel_terrain != 3:
                        # Distanz-Check (max 3 Felder)
                        setze ddx auf cursor_mx - ein_x[auswahl]
                        setze ddy auf cursor_my - ein_y[auswahl]
                        wenn ddx < 0:
                            setze ddx auf 0 - ddx
                        wenn ddy < 0:
                            setze ddy auf 0 - ddy
                        wenn ddx + ddy <= 3:
                            # Kampf?
                            setze kampf auf -1
                            setze ei auf 0
                            solange ei < einheit_anzahl:
                                wenn ein_aktiv[ei] und ein_team[ei] == 1:
                                    wenn ein_x[ei] == cursor_mx und ein_y[ei] == cursor_my:
                                        setze kampf auf ei
                                setze ei auf ei + 1

                            wenn kampf >= 0 und ein_typ[auswahl] == 0:
                                # Angriff!
                                setze ein_hp[kampf] auf ein_hp[kampf] - 4
                                setze ein_hp[auswahl] auf ein_hp[auswahl] - 2
                                wenn ein_hp[kampf] <= 0:
                                    setze ein_aktiv[kampf] auf falsch
                                    setze gold auf gold + 5
                                wenn ein_hp[auswahl] <= 0:
                                    setze ein_aktiv[auswahl] auf falsch
                                    setze auswahl auf -1
                                setze ein_bewegt[auswahl] auf wahr
                            sonst:
                                wenn kampf < 0:
                                    setze ein_x[auswahl] auf cursor_mx
                                    setze ein_y[auswahl] auf cursor_my
                                    setze ein_bewegt[auswahl] auf wahr
                                    # Arbeiter sammelt Ressourcen
                                    wenn ein_typ[auswahl] == 1:
                                        setze ridx auf cursor_my * MAP_W + cursor_mx
                                        wenn ressourcen[ridx] > 0:
                                            wenn karte[ridx] == 2:
                                                setze holz auf holz + 2
                                            wenn karte[ridx] == 3:
                                                setze stein auf stein + 2
                                            setze ressourcen[ridx] auf ressourcen[ridx] - 1

    # === RUNDE BEENDEN ===
    wenn taste_gedrückt("leertaste") und eingabe_cd <= 0:
        setze runde auf runde + 1
        setze gold auf gold + 1
        setze eingabe_cd auf 20
        # Alle Einheiten wieder bereit
        setze ei auf 0
        solange ei < einheit_anzahl:
            setze ein_bewegt[ei] auf falsch
            setze ei auf ei + 1
        # Feind-KI: bewege Feinde Richtung Spieler-Stadt
        setze ei auf 0
        solange ei < einheit_anzahl:
            wenn ein_aktiv[ei] und ein_team[ei] == 1:
                wenn ein_x[ei] > start_x:
                    setze ein_x[ei] auf ein_x[ei] - 1
                wenn ein_x[ei] < start_x:
                    setze ein_x[ei] auf ein_x[ei] + 1
                wenn ein_y[ei] > start_y:
                    setze ein_y[ei] auf ein_y[ei] - 1
                wenn ein_y[ei] < start_y:
                    setze ein_y[ei] auf ein_y[ei] + 1
            setze ei auf ei + 1
        setze auswahl auf -1

    # Einheiten bauen
    wenn taste_gedrückt("1") und holz >= 5 und eingabe_cd <= 0:
        einheit_erstellen(start_x + 1, start_y + 1, 0, 0)
        setze einheit_anzahl auf länge(ein_x)
        setze holz auf holz - 5
        setze eingabe_cd auf 15
    wenn taste_gedrückt("2") und holz >= 3 und eingabe_cd <= 0:
        einheit_erstellen(start_x - 1, start_y + 1, 1, 0)
        setze einheit_anzahl auf länge(ein_x)
        setze holz auf holz - 3
        setze eingabe_cd auf 15

    # === ZEICHNEN ===
    fenster_löschen(win, "#1A237E")

    # Karte
    setze draw_h auf (HOEHE - 80) / TILE
    setze draw_w auf BREITE / TILE
    setze ty auf 0
    solange ty < draw_h:
        setze tx auf 0
        solange tx < draw_w:
            setze map_x auf cam_x + tx
            setze map_y auf cam_y + ty
            wenn map_x >= 0 und map_x < MAP_W und map_y >= 0 und map_y < MAP_H:
                setze terrain auf karte[map_y * MAP_W + map_x]
                setze tf auf tile_farbe(terrain)
                zeichne_rechteck(win, tx * TILE, ty * TILE, TILE - 1, TILE - 1, tf)
                # Ressourcen-Marker
                wenn ressourcen[map_y * MAP_W + map_x] > 0:
                    zeichne_kreis(win, tx * TILE + TILE / 2, ty * TILE + TILE / 2, 3, "#FFD700")
            setze tx auf tx + 1
        setze ty auf ty + 1

    # Cursor
    setze cx auf (cursor_mx - cam_x) * TILE
    setze cy auf (cursor_my - cam_y) * TILE
    zeichne_rechteck(win, cx, cy, TILE - 1, 2, "#FFFFFF")
    zeichne_rechteck(win, cx, cy + TILE - 3, TILE - 1, 2, "#FFFFFF")
    zeichne_rechteck(win, cx, cy, 2, TILE - 1, "#FFFFFF")
    zeichne_rechteck(win, cx + TILE - 3, cy, 2, TILE - 1, "#FFFFFF")

    # Einheiten
    setze ei auf 0
    solange ei < einheit_anzahl:
        wenn ein_aktiv[ei]:
            setze ux auf (ein_x[ei] - cam_x) * TILE + TILE / 2
            setze uy auf (ein_y[ei] - cam_y) * TILE + TILE / 2
            wenn ux > -TILE und ux < BREITE + TILE und uy > -TILE und uy < HOEHE - 80:
                wenn ein_team[ei] == 0:
                    wenn ein_typ[ei] == 0:
                        zeichne_kreis(win, ux, uy, 8, "#2196F3")
                        zeichne_rechteck(win, ux + 5, uy - 8, 3, 12, "#90CAF9")
                    sonst:
                        zeichne_kreis(win, ux, uy, 7, "#4CAF50")
                        zeichne_rechteck(win, ux - 1, uy + 5, 4, 6, "#795548")
                sonst:
                    wenn ein_typ[ei] == 0:
                        zeichne_kreis(win, ux, uy, 8, "#F44336")
                        zeichne_rechteck(win, ux + 5, uy - 8, 3, 12, "#EF9A9A")
                    sonst:
                        zeichne_kreis(win, ux, uy, 7, "#FF9800")
                # HP-Balken
                setze hp_w auf ein_hp[ei] * 2
                zeichne_rechteck(win, ux - 10, uy - 14, hp_w, 3, "#4CAF50")
                # Auswahl-Ring
                wenn ei == auswahl:
                    zeichne_kreis(win, ux, uy, 12, "#FFFFFF")
        setze ei auf ei + 1

    # HUD (unten)
    zeichne_rechteck(win, 0, HOEHE - 80, BREITE, 80, "#263238")

    # Ressourcen
    zeichne_kreis(win, 20, HOEHE - 60, 6, "#8D6E63")
    setze hi auf 0
    solange hi < holz und hi < 20:
        zeichne_rechteck(win, 35 + hi * 5, HOEHE - 63, 3, 6, "#A1887F")
        setze hi auf hi + 1

    zeichne_kreis(win, 20, HOEHE - 40, 6, "#78909C")
    setze si auf 0
    solange si < stein und si < 20:
        zeichne_rechteck(win, 35 + si * 5, HOEHE - 43, 3, 6, "#90A4AE")
        setze si auf si + 1

    zeichne_kreis(win, 20, HOEHE - 20, 6, "#FFD700")
    setze gi auf 0
    solange gi < gold und gi < 20:
        zeichne_rechteck(win, 35 + gi * 5, HOEHE - 23, 3, 6, "#FFF176")
        setze gi auf gi + 1

    # Runde
    setze ri auf 0
    solange ri < runde und ri < 30:
        zeichne_pixel(win, BREITE - 20 - ri * 4, HOEHE - 20, "#FFFFFF")
        zeichne_pixel(win, BREITE - 20 - ri * 4, HOEHE - 21, "#FFFFFF")
        setze ri auf ri + 1

    # Bau-Hinweise (rechts)
    zeichne_rechteck(win, BREITE - 200, HOEHE - 75, 90, 30, "#37474F")
    zeichne_kreis(win, BREITE - 185, HOEHE - 60, 6, "#2196F3")
    # "1: Krieger (5H)"

    zeichne_rechteck(win, BREITE - 200, HOEHE - 40, 90, 30, "#37474F")
    zeichne_kreis(win, BREITE - 185, HOEHE - 25, 6, "#4CAF50")
    # "2: Arbeiter (3H)"

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Strategie beendet! Runde: " + text(runde) + " Gold: " + text(gold)

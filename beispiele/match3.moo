# ============================================================
# moo Match-3 — Bejeweled/Candy Crush Style Puzzle
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/match3.moo -o beispiele/match3
#   ./beispiele/match3
#
# Bedienung:
#   Maus-Klick    - Stein auswaehlen / tauschen
#   R             - Neustart
#   Escape        - Beenden
#
# Features:
#   * 8x8 Grid mit 6 Farben
#   * Klick-Swap (2 benachbarte Steine tauschen)
#   * 3er-Ketten horizontal + vertikal erkennen
#   * Steine loeschen + Gravity-Drop
#   * Neue Steine von oben nachfuellen
#   * Ketten-Combo (mehrere Matches = Bonus)
#   * Score + Level
# ============================================================

setze BREITE auf 640
setze HOEHE auf 700
setze GRID auf 8
setze TILE auf 64
setze OFFSET_X auf 64
setze OFFSET_Y auf 80
setze FARBEN auf 6

# Farb-Palette
funktion stein_farbe(typ):
    wenn typ == 0:
        gib_zurück "#F44336"
    wenn typ == 1:
        gib_zurück "#2196F3"
    wenn typ == 2:
        gib_zurück "#4CAF50"
    wenn typ == 3:
        gib_zurück "#FFEB3B"
    wenn typ == 4:
        gib_zurück "#9C27B0"
    wenn typ == 5:
        gib_zurück "#FF9800"
    gib_zurück "#FFFFFF"

funktion stein_farbe_hell(typ):
    wenn typ == 0:
        gib_zurück "#EF9A9A"
    wenn typ == 1:
        gib_zurück "#90CAF9"
    wenn typ == 2:
        gib_zurück "#A5D6A7"
    wenn typ == 3:
        gib_zurück "#FFF9C4"
    wenn typ == 4:
        gib_zurück "#CE93D8"
    wenn typ == 5:
        gib_zurück "#FFCC80"
    gib_zurück "#EEEEEE"

# === SPIELFELD ===
# Grid als 1D-Liste (y * GRID + x)
setze feld auf []
setze markiert auf []

# Pseudo-Random basierend auf Position
setze rng_state auf [12345]

funktion rng():
    setze x auf rng_state[0]
    setze x auf x ^ (x << 13)
    setze x auf x ^ (x >> 7)
    setze x auf x ^ (x << 17)
    wenn x < 0:
        setze x auf 0 - x
    rng_state[0] = x
    gib_zurück x

funktion feld_idx(x, y):
    gib_zurück y * GRID + x

funktion feld_get(x, y):
    wenn x < 0 oder x >= GRID oder y < 0 oder y >= GRID:
        gib_zurück -1
    gib_zurück feld[y * GRID + x]

funktion feld_set(x, y, val):
    feld[y * GRID + x] = val

# Feld fuellen (ohne initiale 3er-Ketten)
funktion feld_fuellen():
    setze fi auf 0
    solange fi < GRID * GRID:
        feld.hinzufügen(rng() % FARBEN)
        markiert.hinzufügen(falsch)
        setze fi auf fi + 1
    # Initiale Matches entfernen
    setze changed auf wahr
    solange changed:
        setze changed auf falsch
        setze fy auf 0
        solange fy < GRID:
            setze fx auf 0
            solange fx < GRID:
                setze c auf feld_get(fx, fy)
                # Horizontal 3er
                wenn fx >= 2:
                    wenn feld_get(fx - 1, fy) == c und feld_get(fx - 2, fy) == c:
                        feld_set(fx, fy, (c + 1 + rng() % (FARBEN - 1)) % FARBEN)
                        setze changed auf wahr
                # Vertikal 3er
                wenn fy >= 2:
                    wenn feld_get(fx, fy - 1) == c und feld_get(fx, fy - 2) == c:
                        feld_set(fx, fy, (c + 1 + rng() % (FARBEN - 1)) % FARBEN)
                        setze changed auf wahr
                setze fx auf fx + 1
            setze fy auf fy + 1

# === MATCH-ERKENNUNG ===
funktion matches_finden():
    # Reset markierungen
    setze fi auf 0
    solange fi < GRID * GRID:
        markiert[fi] = falsch
        setze fi auf fi + 1
    setze gefunden auf 0
    # Horizontal
    setze fy auf 0
    solange fy < GRID:
        setze fx auf 0
        solange fx < GRID - 2:
            setze c auf feld_get(fx, fy)
            wenn c >= 0:
                wenn feld_get(fx + 1, fy) == c und feld_get(fx + 2, fy) == c:
                    markiert[feld_idx(fx, fy)] = wahr
                    markiert[feld_idx(fx + 1, fy)] = wahr
                    markiert[feld_idx(fx + 2, fy)] = wahr
                    setze gefunden auf gefunden + 1
                    # Erweiterte Kette pruefen
                    setze ex auf fx + 3
                    solange ex < GRID und feld_get(ex, fy) == c:
                        markiert[feld_idx(ex, fy)] = wahr
                        setze ex auf ex + 1
            setze fx auf fx + 1
        setze fy auf fy + 1
    # Vertikal
    setze fx auf 0
    solange fx < GRID:
        setze fy auf 0
        solange fy < GRID - 2:
            setze c auf feld_get(fx, fy)
            wenn c >= 0:
                wenn feld_get(fx, fy + 1) == c und feld_get(fx, fy + 2) == c:
                    markiert[feld_idx(fx, fy)] = wahr
                    markiert[feld_idx(fx, fy + 1)] = wahr
                    markiert[feld_idx(fx, fy + 2)] = wahr
                    setze gefunden auf gefunden + 1
                    setze ey auf fy + 3
                    solange ey < GRID und feld_get(fx, ey) == c:
                        markiert[feld_idx(fx, ey)] = wahr
                        setze ey auf ey + 1
            setze fy auf fy + 1
        setze fx auf fx + 1
    gib_zurück gefunden

# Markierte Steine entfernen
funktion markierte_entfernen():
    setze entfernt auf 0
    setze fi auf 0
    solange fi < GRID * GRID:
        wenn markiert[fi]:
            feld[fi] = -1
            setze entfernt auf entfernt + 1
        setze fi auf fi + 1
    gib_zurück entfernt

# Gravity: Steine fallen runter
funktion gravity():
    setze fx auf 0
    solange fx < GRID:
        # Von unten nach oben durchgehen
        setze leer auf GRID - 1
        setze fy auf GRID - 1
        solange fy >= 0:
            wenn feld_get(fx, fy) >= 0:
                wenn fy != leer:
                    feld_set(fx, leer, feld_get(fx, fy))
                    feld_set(fx, fy, -1)
                setze leer auf leer - 1
            setze fy auf fy - 1
        # Leere oben auffuellen
        solange leer >= 0:
            feld_set(fx, leer, rng() % FARBEN)
            setze leer auf leer - 1
        setze fx auf fx + 1

# === SPIELZUSTAND ===
setze punkte auf 0
setze combo auf 0
setze auswahl_x auf -1
setze auswahl_y auf -1
setze anim_timer auf 0
setze klick_cooldown auf 0

feld_fuellen()

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Match-3", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_gedrückt("r"):
        setze fi auf 0
        solange fi < GRID * GRID:
            feld[fi] = rng() % FARBEN
            setze fi auf fi + 1
        setze punkte auf 0
        setze auswahl_x auf -1
        setze auswahl_y auf -1

    # === ANIMATION: Match-Cycle ===
    wenn anim_timer > 0:
        setze anim_timer auf anim_timer - 1
        wenn anim_timer == 0:
            setze entfernt auf markierte_entfernen()
            setze punkte auf punkte + entfernt * 10 * (combo + 1)
            gravity()
            # Neue Matches nach Gravity?
            setze neue_matches auf matches_finden()
            wenn neue_matches > 0:
                setze combo auf combo + 1
                setze anim_timer auf 15
            sonst:
                setze combo auf 0
    sonst:
        # === MAUS-KLICK ===
        wenn klick_cooldown > 0:
            setze klick_cooldown auf klick_cooldown - 1
        wenn maus_gedrückt(win) und klick_cooldown == 0:
            setze mx auf maus_x(win)
            setze my auf maus_y(win)
            setze gx auf boden((mx - OFFSET_X) / TILE)
            setze gy auf boden((my - OFFSET_Y) / TILE)
            wenn gx >= 0 und gx < GRID und gy >= 0 und gy < GRID:
                wenn auswahl_x < 0:
                    # Erste Auswahl
                    setze auswahl_x auf gx
                    setze auswahl_y auf gy
                sonst:
                    # Zweite Auswahl — benachbart?
                    setze dx auf gx - auswahl_x
                    setze dy auf gy - auswahl_y
                    wenn dx < 0:
                        setze dx auf 0 - dx
                    wenn dy < 0:
                        setze dy auf 0 - dy
                    wenn (dx == 1 und dy == 0) oder (dx == 0 und dy == 1):
                        # Tauschen
                        setze tmp auf feld_get(auswahl_x, auswahl_y)
                        feld_set(auswahl_x, auswahl_y, feld_get(gx, gy))
                        feld_set(gx, gy, tmp)
                        # Match pruefen
                        setze neue_matches auf matches_finden()
                        wenn neue_matches > 0:
                            setze combo auf 0
                            setze anim_timer auf 15
                        sonst:
                            # Zurueck tauschen
                            setze tmp auf feld_get(auswahl_x, auswahl_y)
                            feld_set(auswahl_x, auswahl_y, feld_get(gx, gy))
                            feld_set(gx, gy, tmp)
                    setze auswahl_x auf -1
                    setze auswahl_y auf -1
                setze klick_cooldown auf 10

    # === ZEICHNEN ===
    fenster_löschen(win, "#263238")

    # Grid-Hintergrund
    zeichne_rechteck(win, OFFSET_X - 4, OFFSET_Y - 4, GRID * TILE + 8, GRID * TILE + 8, "#37474F")

    # Steine
    setze fy auf 0
    solange fy < GRID:
        setze fx auf 0
        solange fx < GRID:
            setze typ auf feld_get(fx, fy)
            wenn typ >= 0:
                setze dx auf OFFSET_X + fx * TILE
                setze dy auf OFFSET_Y + fy * TILE
                setze ist_markiert auf markiert[feld_idx(fx, fy)]
                # Schachbrett-Hintergrund
                wenn (fx + fy) % 2 == 0:
                    zeichne_rechteck(win, dx, dy, TILE, TILE, "#455A64")
                sonst:
                    zeichne_rechteck(win, dx, dy, TILE, TILE, "#37474F")
                # Stein
                wenn ist_markiert und anim_timer > 0:
                    # Blinken wenn markiert
                    wenn (anim_timer / 3) % 2 == 0:
                        zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 24, stein_farbe(typ))
                sonst:
                    zeichne_kreis(win, dx + TILE / 2, dy + TILE / 2, 24, stein_farbe(typ))
                    zeichne_kreis(win, dx + TILE / 2 - 4, dy + TILE / 2 - 6, 8, stein_farbe_hell(typ))
                # Auswahlrahmen
                wenn fx == auswahl_x und fy == auswahl_y:
                    zeichne_rechteck(win, dx + 2, dy + 2, TILE - 4, 3, "#FFFFFF")
                    zeichne_rechteck(win, dx + 2, dy + TILE - 5, TILE - 4, 3, "#FFFFFF")
                    zeichne_rechteck(win, dx + 2, dy + 2, 3, TILE - 4, "#FFFFFF")
                    zeichne_rechteck(win, dx + TILE - 5, dy + 2, 3, TILE - 4, "#FFFFFF")
            setze fx auf fx + 1
        setze fy auf fy + 1

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 70, "#1B2631")
    # Score als goldene Punkte
    setze score_dots auf punkte / 30
    wenn score_dots > 40:
        setze score_dots auf 40
    setze si auf 0
    solange si < score_dots:
        zeichne_kreis(win, 15 + si * 15, 25, 5, "#FFD700")
        setze si auf si + 1
    # Combo
    wenn combo > 0:
        setze ci auf 0
        solange ci < combo und ci < 10:
            zeichne_kreis(win, 15 + ci * 20, 50, 7, "#FF9800")
            setze ci auf ci + 1

    # Farb-Legende unten
    setze fi auf 0
    solange fi < FARBEN:
        zeichne_kreis(win, OFFSET_X + 30 + fi * 90, HOEHE - 30, 15, stein_farbe(fi))
        setze fi auf fi + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Match-3 beendet! Punkte: " + text(punkte)

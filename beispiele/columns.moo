# ============================================================
# columns.moo — Columns (Sega-Style Match-3 Puzzle)
#
# Kompilieren: moo-compiler compile beispiele/columns.moo -o beispiele/columns
# Starten:     ./beispiele/columns
#
# Steuerung:
#   Links/Rechts - Spalte waehlen
#   Unten        - Schnell fallen
#   Leertaste    - Farben rotieren
#   Escape       - Beenden
#
# Regeln: 3er Saeulen fallen, 3+ gleiche vertikal/horizontal/diagonal = Match
# ============================================================

konstante COLS auf 6
konstante ROWS auf 13
konstante CELL auf 32
konstante BOARD_X auf 100
konstante BOARD_Y auf 40
konstante WIN_W auf 400
konstante WIN_H auf 500
konstante FARBEN auf 6

# Farb-Palette
funktion gem_farbe(typ):
    wenn typ == 1:
        gib_zurück "#F44336"
    wenn typ == 2:
        gib_zurück "#2196F3"
    wenn typ == 3:
        gib_zurück "#4CAF50"
    wenn typ == 4:
        gib_zurück "#FF9800"
    wenn typ == 5:
        gib_zurück "#9C27B0"
    wenn typ == 6:
        gib_zurück "#FFEB3B"
    gib_zurück "#424242"

funktion gem_farbe2(typ):
    wenn typ == 1:
        gib_zurück "#EF9A9A"
    wenn typ == 2:
        gib_zurück "#90CAF9"
    wenn typ == 3:
        gib_zurück "#A5D6A7"
    wenn typ == 4:
        gib_zurück "#FFCC80"
    wenn typ == 5:
        gib_zurück "#CE93D8"
    wenn typ == 6:
        gib_zurück "#FFF59D"
    gib_zurück "#616161"

# === PRNG ===
setze col_seed auf 12345

funktion col_rand(max_val):
    setze col_seed auf (col_seed * 1103515245 + 12345) % 2147483648
    gib_zurück (col_seed % max_val) + 1

# === Spielfeld (0 = leer, 1-6 = Farbe) ===
setze board auf []

funktion init_board():
    setze board auf []
    setze bi auf 0
    solange bi < ROWS * COLS:
        board.hinzufügen(0)
        setze bi auf bi + 1

funktion get_cell(row, col):
    wenn row < 0 oder row >= ROWS oder col < 0 oder col >= COLS:
        gib_zurück 0
    gib_zurück board[row * COLS + col]

funktion set_cell(row, col, val):
    wenn row >= 0 und row < ROWS und col >= 0 und col < COLS:
        board[row * COLS + col] = val

# === Fallende Säule (3 Gems) ===
setze saule auf [0, 0, 0]
setze saule_col auf 3
setze saule_row auf -2
setze fall_timer auf 0
setze fall_speed auf 30

funktion neue_saule():
    saule[0] = col_rand(FARBEN)
    saule[1] = col_rand(FARBEN)
    saule[2] = col_rand(FARBEN)
    setze saule_col auf COLS / 2
    setze saule_row auf -2
    setze fall_timer auf 0

funktion rotiere_saule():
    setze temp auf saule[2]
    saule[2] = saule[1]
    saule[1] = saule[0]
    saule[0] = temp

# === Platzieren ===
funktion platziere_saule():
    setze ri auf 0
    solange ri < 3:
        setze row auf saule_row + ri
        wenn row >= 0 und row < ROWS:
            set_cell(row, saule_col, saule[ri])
        setze ri auf ri + 1

# === Matches finden + entfernen ===
funktion finde_matches():
    setze matched auf []
    setze mi auf 0
    solange mi < ROWS * COLS:
        matched.hinzufügen(falsch)
        setze mi auf mi + 1

    setze gefunden auf 0

    # Horizontal
    setze row auf 0
    solange row < ROWS:
        setze col auf 0
        solange col < COLS - 2:
            setze val auf get_cell(row, col)
            wenn val > 0 und get_cell(row, col + 1) == val und get_cell(row, col + 2) == val:
                matched[row * COLS + col] = wahr
                matched[row * COLS + col + 1] = wahr
                matched[row * COLS + col + 2] = wahr
                setze gefunden auf gefunden + 1
            setze col auf col + 1
        setze row auf row + 1

    # Vertikal
    setze col auf 0
    solange col < COLS:
        setze row auf 0
        solange row < ROWS - 2:
            setze val auf get_cell(row, col)
            wenn val > 0 und get_cell(row + 1, col) == val und get_cell(row + 2, col) == val:
                matched[row * COLS + col] = wahr
                matched[(row + 1) * COLS + col] = wahr
                matched[(row + 2) * COLS + col] = wahr
                setze gefunden auf gefunden + 1
            setze row auf row + 1
        setze col auf col + 1

    # Diagonal (\)
    setze row auf 0
    solange row < ROWS - 2:
        setze col auf 0
        solange col < COLS - 2:
            setze val auf get_cell(row, col)
            wenn val > 0 und get_cell(row + 1, col + 1) == val und get_cell(row + 2, col + 2) == val:
                matched[row * COLS + col] = wahr
                matched[(row + 1) * COLS + col + 1] = wahr
                matched[(row + 2) * COLS + col + 2] = wahr
                setze gefunden auf gefunden + 1
            setze col auf col + 1
        setze row auf row + 1

    # Diagonal (/)
    setze row auf 0
    solange row < ROWS - 2:
        setze col auf 2
        solange col < COLS:
            setze val auf get_cell(row, col)
            wenn val > 0 und get_cell(row + 1, col - 1) == val und get_cell(row + 2, col - 2) == val:
                matched[row * COLS + col] = wahr
                matched[(row + 1) * COLS + col - 1] = wahr
                matched[(row + 2) * COLS + col - 2] = wahr
                setze gefunden auf gefunden + 1
            setze col auf col + 1
        setze row auf row + 1

    # Matches entfernen
    wenn gefunden > 0:
        setze mi auf 0
        solange mi < ROWS * COLS:
            wenn matched[mi]:
                board[mi] = 0
            setze mi auf mi + 1

    gib_zurück gefunden

# === Schwerkraft (Lücken füllen) ===
funktion schwerkraft():
    setze bewegt auf falsch
    setze col auf 0
    solange col < COLS:
        setze row auf ROWS - 2
        solange row >= 0:
            wenn get_cell(row, col) > 0 und get_cell(row + 1, col) == 0:
                set_cell(row + 1, col, get_cell(row, col))
                set_cell(row, col, 0)
                setze bewegt auf wahr
            setze row auf row - 1
        setze col auf col + 1
    gib_zurück bewegt

# === Zeichnen ===
funktion zeichne_board(win):
    # Rahmen
    zeichne_rechteck(win, BOARD_X - 4, BOARD_Y - 4, COLS * CELL + 8, ROWS * CELL + 8, "#37474F")
    zeichne_rechteck(win, BOARD_X - 2, BOARD_Y - 2, COLS * CELL + 4, ROWS * CELL + 4, "#263238")

    setze row auf 0
    solange row < ROWS:
        setze col auf 0
        solange col < COLS:
            setze dx auf BOARD_X + col * CELL
            setze dy auf BOARD_Y + row * CELL
            # Hintergrund
            zeichne_rechteck(win, dx, dy, CELL, CELL, "#1A1A2E")
            zeichne_rechteck(win, dx + 1, dy + 1, CELL - 2, CELL - 2, "#212121")

            setze val auf get_cell(row, col)
            wenn val > 0:
                # Gem zeichnen
                zeichne_rechteck(win, dx + 3, dy + 3, CELL - 6, CELL - 6, gem_farbe(val))
                zeichne_rechteck(win, dx + 6, dy + 6, CELL - 12, CELL - 12, gem_farbe2(val))
                # Glanz
                zeichne_rechteck(win, dx + 8, dy + 5, 6, 3, "#FFFFFF")
            setze col auf col + 1
        setze row auf row + 1

funktion zeichne_saule_fall(win):
    setze ri auf 0
    solange ri < 3:
        setze row auf saule_row + ri
        wenn row >= 0:
            setze dx auf BOARD_X + saule_col * CELL
            setze dy auf BOARD_Y + row * CELL
            setze val auf saule[ri]
            zeichne_rechteck(win, dx + 3, dy + 3, CELL - 6, CELL - 6, gem_farbe(val))
            zeichne_rechteck(win, dx + 6, dy + 6, CELL - 12, CELL - 12, gem_farbe2(val))
            zeichne_rechteck(win, dx + 8, dy + 5, 6, 3, "#FFFFFF")
        setze ri auf ri + 1

funktion zeichne_vorschau(win):
    zeichne_rechteck(win, 20, BOARD_Y, 60, 110, "#263238")
    zeichne_rechteck(win, 22, BOARD_Y + 2, 56, 106, "#1A1A2E")
    setze ri auf 0
    solange ri < 3:
        setze dx auf 30
        setze dy auf BOARD_Y + 10 + ri * 34
        setze val auf saule[ri]
        zeichne_kreis(win, dx + 12, dy + 12, 12, gem_farbe(val))
        zeichne_kreis(win, dx + 12, dy + 10, 6, gem_farbe2(val))
        setze ri auf ri + 1

funktion zeichne_col_hud(win, score_val, level_val):
    zeichne_rechteck(win, 0, WIN_H - 32, WIN_W, 32, "#1A1A2E")
    # Score
    setze si auf 0
    solange si < score_val und si < 50:
        zeichne_kreis(win, 12 + si * 6, WIN_H - 16, 2, "#FFD700")
        setze si auf si + 1
    # Level
    setze li auf 0
    solange li < level_val:
        zeichne_kreis(win, WIN_W - 16 - li * 14, WIN_H - 16, 5, "#42A5F5")
        setze li auf li + 1

# === Hauptprogramm ===
zeige "=== moo Columns ==="
zeige "Links/Rechts=Bewegen, Unten=Schnell, Leertaste=Rotieren"

setze win auf fenster_erstelle("moo Columns", WIN_W, WIN_H)
setze col_seed auf zeit_ms() % 99991
init_board()
neue_saule()

setze score auf 0
setze level auf 1
setze game_over auf falsch
setze taste_cd auf 0
setze match_phase auf falsch
setze match_timer auf 0
setze combo auf 0

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_cd > 0:
        setze taste_cd auf taste_cd - 1

    wenn game_over == falsch und match_phase == falsch:
        # Steuerung
        wenn taste_cd == 0:
            wenn taste_gedrückt("links") und saule_col > 0:
                setze saule_col auf saule_col - 1
                setze taste_cd auf 6
            wenn taste_gedrückt("rechts") und saule_col < COLS - 1:
                setze saule_col auf saule_col + 1
                setze taste_cd auf 6
            wenn taste_gedrückt("leertaste"):
                rotiere_saule()
                setze taste_cd auf 8

        # Schnell fallen
        setze speed auf fall_speed
        wenn taste_gedrückt("unten"):
            setze speed auf 2

        # Fallen
        setze fall_timer auf fall_timer + 1
        wenn fall_timer >= speed:
            setze fall_timer auf 0
            # Kann fallen?
            setze kann auf wahr
            wenn saule_row + 3 >= ROWS:
                setze kann auf falsch
            sonst wenn get_cell(saule_row + 3, saule_col) > 0:
                setze kann auf falsch

            wenn kann:
                setze saule_row auf saule_row + 1
            sonst:
                # Platzieren
                platziere_saule()
                # Game Over?
                wenn saule_row < 0:
                    setze game_over auf wahr
                sonst:
                    setze match_phase auf wahr
                    setze match_timer auf 10
                    setze combo auf 0

    # Match-Phase
    wenn match_phase:
        setze match_timer auf match_timer - 1
        wenn match_timer <= 0:
            setze matches auf finde_matches()
            wenn matches > 0:
                setze combo auf combo + 1
                setze score auf score + matches * combo * 10
                setze match_timer auf 15
                # Schwerkraft
                setze gravity auf wahr
                solange gravity:
                    setze gravity auf schwerkraft()
            sonst:
                setze match_phase auf falsch
                neue_saule()
                # Level-Up
                wenn score > level * 100:
                    setze level auf level + 1
                    wenn fall_speed > 8:
                        setze fall_speed auf fall_speed - 3

    # === Zeichnen ===
    fenster_löschen(win, "#0D1117")
    zeichne_board(win)

    wenn match_phase == falsch und game_over == falsch:
        zeichne_saule_fall(win)

    zeichne_vorschau(win)
    zeichne_col_hud(win, score, level)

    wenn game_over:
        zeichne_rechteck(win, WIN_W / 2 - 80, WIN_H / 2 - 25, 160, 50, "#D32F2F")
        zeichne_rechteck(win, WIN_W / 2 - 78, WIN_H / 2 - 23, 156, 46, "#F44336")
        zeichne_linie(win, WIN_W / 2 - 10, WIN_H / 2 - 10, WIN_W / 2 + 10, WIN_H / 2 + 10, "#FFFFFF")
        zeichne_linie(win, WIN_W / 2 + 10, WIN_H / 2 - 10, WIN_W / 2 - 10, WIN_H / 2 + 10, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Columns beendet. Score: " + text(score) + " Level: " + text(level)

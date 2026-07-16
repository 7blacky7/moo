# ============================================================
# pipe_puzzle.moo — Pipe-Mania Puzzle
#
# Kompilieren: moo-compiler compile beispiele/pipe_puzzle.moo -o beispiele/pipe_puzzle
# Starten:     ./beispiele/pipe_puzzle
#
# Steuerung: Mausklick = Pipe rotieren, N = Neue Karte, Escape = Beenden
# Ziel: Verbinde Start (gruen) mit Ziel (rot) durch Rotation der Rohre
# ============================================================

konstante COLS auf 8
konstante ROWS auf 6
konstante CELL auf 64
konstante BOARD_X auf 32
konstante BOARD_Y auf 60
konstante WIN_W auf 544
konstante WIN_H auf 480

# Pipe-Typen:
# 0 = leer
# 1 = horizontal (Ost-West)
# 2 = vertikal (Nord-Sued)
# 3 = L (Nord-Ost)
# 4 = L (Ost-Sued)
# 5 = L (Sued-West)
# 6 = L (West-Nord)
# 7 = Start (offen nach Osten)
# 8 = Ziel (offen nach Westen)

# Verbindungen: [Nord, Ost, Sued, West] (wahr/falsch)
funktion pipe_connections(typ):
    wenn typ == 1: gib_zurück [falsch, wahr, falsch, wahr]
    wenn typ == 2: gib_zurück [wahr, falsch, wahr, falsch]
    wenn typ == 3: gib_zurück [wahr, wahr, falsch, falsch]
    wenn typ == 4: gib_zurück [falsch, wahr, wahr, falsch]
    wenn typ == 5: gib_zurück [falsch, falsch, wahr, wahr]
    wenn typ == 6: gib_zurück [wahr, falsch, falsch, wahr]
    wenn typ == 7: gib_zurück [falsch, wahr, falsch, falsch]
    wenn typ == 8: gib_zurück [falsch, falsch, falsch, wahr]
    gib_zurück [falsch, falsch, falsch, falsch]

# Rotation: 1→2→1, 3→4→5→6→3
funktion rotate_pipe(typ):
    wenn typ == 1: gib_zurück 2
    wenn typ == 2: gib_zurück 1
    wenn typ == 3: gib_zurück 4
    wenn typ == 4: gib_zurück 5
    wenn typ == 5: gib_zurück 6
    wenn typ == 6: gib_zurück 3
    gib_zurück typ

# === PRNG ===
setze pp_seed auf 42

funktion pp_rand(max_val):
    setze pp_seed auf (pp_seed * 1103515245 + 12345) % 2147483648
    gib_zurück pp_seed % max_val

# === Board ===
setze board auf []

funktion init_board():
    setze board auf []
    setze bi auf 0
    solange bi < ROWS * COLS:
        setze typ auf pp_rand(6) + 1
        board.hinzufügen(typ)
        setze bi auf bi + 1
    # Start links, Ziel rechts in der Mitte
    setze mid_row auf ROWS / 2
    board[mid_row * COLS + 0] = 7
    board[mid_row * COLS + COLS - 1] = 8

# === Flood-Fill fuer Verbindungspruefung ===
funktion ist_verbunden():
    setze besucht auf []
    setze bi auf 0
    solange bi < ROWS * COLS:
        besucht.hinzufügen(falsch)
        setze bi auf bi + 1

    # Start finden
    setze mid_row auf ROWS / 2
    setze start_idx auf mid_row * COLS + 0

    # BFS
    setze queue auf [start_idx]
    besucht[start_idx] = wahr

    solange länge(queue) > 0:
        setze cur auf queue[0]
        # Queue shift
        setze neue_queue auf []
        setze qi auf 1
        solange qi < länge(queue):
            neue_queue.hinzufügen(queue[qi])
            setze qi auf qi + 1
        setze queue auf neue_queue

        setze cur_row auf cur / COLS
        setze cur_col auf cur - cur_row * COLS
        setze cur_typ auf board[cur]
        setze conn auf pipe_connections(cur_typ)

        # 4 Richtungen
        # Nord
        wenn conn[0] und cur_row > 0:
            setze n_idx auf (cur_row - 1) * COLS + cur_col
            wenn besucht[n_idx] == falsch:
                setze n_conn auf pipe_connections(board[n_idx])
                wenn n_conn[2]:
                    besucht[n_idx] = wahr
                    queue.hinzufügen(n_idx)
        # Ost
        wenn conn[1] und cur_col < COLS - 1:
            setze n_idx auf cur_row * COLS + cur_col + 1
            wenn besucht[n_idx] == falsch:
                setze n_conn auf pipe_connections(board[n_idx])
                wenn n_conn[3]:
                    besucht[n_idx] = wahr
                    queue.hinzufügen(n_idx)
        # Sued
        wenn conn[2] und cur_row < ROWS - 1:
            setze n_idx auf (cur_row + 1) * COLS + cur_col
            wenn besucht[n_idx] == falsch:
                setze n_conn auf pipe_connections(board[n_idx])
                wenn n_conn[0]:
                    besucht[n_idx] = wahr
                    queue.hinzufügen(n_idx)
        # West
        wenn conn[3] und cur_col > 0:
            setze n_idx auf cur_row * COLS + cur_col - 1
            wenn besucht[n_idx] == falsch:
                setze n_conn auf pipe_connections(board[n_idx])
                wenn n_conn[1]:
                    besucht[n_idx] = wahr
                    queue.hinzufügen(n_idx)

    # Ziel erreicht?
    setze ziel_idx auf mid_row * COLS + COLS - 1
    gib_zurück [besucht[ziel_idx], besucht]

# === Zeichnen ===
funktion zeichne_pipe(win, dx, dy, typ, aktiv):
    # Hintergrund
    zeichne_rechteck(win, dx, dy, CELL, CELL, "#263238")
    zeichne_rechteck(win, dx + 2, dy + 2, CELL - 4, CELL - 4, "#37474F")

    wenn typ == 0:
        gib_zurück nichts

    setze pipe_c auf "#90A4AE"
    wenn aktiv:
        setze pipe_c auf "#29B6F6"

    wenn typ == 7:
        # Start — gruen
        zeichne_rechteck(win, dx + CELL / 2, dy + CELL / 2 - 8, CELL / 2 + 4, 16, "#4CAF50")
        zeichne_rechteck(win, dx + CELL / 2, dy + CELL / 2 - 6, CELL / 2 + 4, 12, "#66BB6A")
        zeichne_kreis(win, dx + CELL / 2 - 4, dy + CELL / 2, 16, "#388E3C")
        zeichne_kreis(win, dx + CELL / 2 - 4, dy + CELL / 2, 10, "#4CAF50")
        gib_zurück nichts

    wenn typ == 8:
        # Ziel — rot
        zeichne_rechteck(win, dx - 4, dy + CELL / 2 - 8, CELL / 2 + 4, 16, "#F44336")
        zeichne_rechteck(win, dx - 4, dy + CELL / 2 - 6, CELL / 2 + 4, 12, "#EF5350")
        zeichne_kreis(win, dx + CELL / 2 + 4, dy + CELL / 2, 16, "#D32F2F")
        zeichne_kreis(win, dx + CELL / 2 + 4, dy + CELL / 2, 10, "#F44336")
        gib_zurück nichts

    setze conn auf pipe_connections(typ)
    setze cx_pos auf dx + CELL / 2
    setze cy_pos auf dy + CELL / 2

    # Rohr-Segmente
    wenn conn[0]:
        zeichne_rechteck(win, cx_pos - 8, dy, 16, CELL / 2, pipe_c)
        zeichne_rechteck(win, cx_pos - 6, dy, 12, CELL / 2, "#CFD8DC")
    wenn conn[1]:
        zeichne_rechteck(win, cx_pos, cy_pos - 8, CELL / 2, 16, pipe_c)
        zeichne_rechteck(win, cx_pos, cy_pos - 6, CELL / 2, 12, "#CFD8DC")
    wenn conn[2]:
        zeichne_rechteck(win, cx_pos - 8, cy_pos, 16, CELL / 2, pipe_c)
        zeichne_rechteck(win, cx_pos - 6, cy_pos, 12, CELL / 2, "#CFD8DC")
    wenn conn[3]:
        zeichne_rechteck(win, dx, cy_pos - 8, CELL / 2, 16, pipe_c)
        zeichne_rechteck(win, dx, cy_pos - 6, CELL / 2, 12, "#CFD8DC")

    # Zentrum
    zeichne_kreis(win, cx_pos, cy_pos, 10, pipe_c)
    zeichne_kreis(win, cx_pos, cy_pos, 6, "#B0BEC5")

# === State ===
setze klick_cd auf 0
setze geloest auf falsch
setze level auf 1
setze zuege auf 0

# === Hauptprogramm ===
zeige "=== moo Pipe Puzzle ==="
zeige "Klick = Rohr rotieren, N = Neue Karte"

setze win auf fenster_erstelle("moo Pipe Puzzle", WIN_W, WIN_H)
setze pp_seed auf zeit_ms() % 99991
init_board()

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn klick_cd > 0:
        setze klick_cd auf klick_cd - 1

    wenn taste_gedrückt("n") und klick_cd == 0:
        setze pp_seed auf zeit_ms() % 99991
        init_board()
        setze geloest auf falsch
        setze zuege auf 0
        wenn level < 10:
            setze level auf level + 1
        setze klick_cd auf 15

    # Mausklick
    wenn maus_gedrückt(win) und klick_cd == 0 und geloest == falsch:
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        setze bcol auf boden((mx - BOARD_X) / CELL)
        setze brow auf boden((my - BOARD_Y) / CELL)
        wenn bcol >= 0 und bcol < COLS und brow >= 0 und brow < ROWS:
            setze bidx auf brow * COLS + bcol
            setze cur_typ auf board[bidx]
            wenn cur_typ >= 1 und cur_typ <= 6:
                board[bidx] = rotate_pipe(cur_typ)
                setze zuege auf zuege + 1
        setze klick_cd auf 8

    # Pruefen ob verbunden
    setze result auf ist_verbunden()
    setze verbunden auf result[0]
    setze besucht auf result[1]
    wenn verbunden und geloest == falsch:
        setze geloest auf wahr

    # === Zeichnen ===
    fenster_löschen(win, "#0D1117")
    # HUD
    zeichne_rechteck(win, 0, 0, WIN_W, BOARD_Y - 10, "#1A1A2E")
    # Level als Kreise
    setze li auf 0
    solange li < level:
        zeichne_kreis(win, 12 + li * 18, 24, 6, "#42A5F5")
        setze li auf li + 1
    # Zuege
    setze zi auf 0
    solange zi < zuege und zi < 30:
        zeichne_kreis(win, WIN_W - 12 - zi * 10, 24, 3, "#90A4AE")
        setze zi auf zi + 1

    # Board
    setze row_idx auf 0
    solange row_idx < ROWS:
        setze col_idx auf 0
        solange col_idx < COLS:
            setze idx auf row_idx * COLS + col_idx
            setze dx auf BOARD_X + col_idx * CELL
            setze dy auf BOARD_Y + row_idx * CELL
            zeichne_pipe(win, dx, dy, board[idx], besucht[idx])
            setze col_idx auf col_idx + 1
        setze row_idx auf row_idx + 1

    # Gewonnen
    wenn geloest:
        zeichne_rechteck(win, WIN_W / 2 - 100, WIN_H / 2 - 30, 200, 60, "#4CAF50")
        zeichne_rechteck(win, WIN_W / 2 - 98, WIN_H / 2 - 28, 196, 56, "#66BB6A")
        # Haken
        zeichne_linie(win, WIN_W / 2 - 15, WIN_H / 2, WIN_W / 2 - 5, WIN_H / 2 + 10, "#FFFFFF")
        zeichne_linie(win, WIN_W / 2 - 5, WIN_H / 2 + 10, WIN_W / 2 + 15, WIN_H / 2 - 10, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Pipe Puzzle beendet. Level: " + text(level) + " Zuege: " + text(zuege)

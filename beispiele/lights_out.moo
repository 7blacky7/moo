# ============================================================
# lights_out.moo — Lights Out Puzzle
#
# Kompilieren: moo-compiler compile beispiele/lights_out.moo -o beispiele/lights_out
# Starten:     ./beispiele/lights_out
#
# Steuerung: Mausklick = Feld umschalten (auch 4 Nachbarn), R = Neu
# Ziel: Alle Lichter ausschalten!
# ============================================================

konstante SIZE auf 5
konstante CELL auf 70
konstante BOARD_X auf 25
konstante BOARD_Y auf 60
konstante WIN_W auf 400
konstante WIN_H auf 460

setze board auf []
setze zuege auf 0
setze geloest auf falsch
setze klick_cd auf 0
setze level auf 1

# PRNG
setze lo_seed auf 42

funktion lo_rand(max_val):
    setze lo_seed auf (lo_seed * 1103515245 + 12345) % 2147483648
    gib_zurück lo_seed % max_val

funktion init_board():
    setze board auf []
    setze bi auf 0
    solange bi < SIZE * SIZE:
        board.hinzufügen(0)
        setze bi auf bi + 1
    # Zufaellig Klicks anwenden um loesbares Puzzle zu erzeugen
    setze ki auf 0
    solange ki < 5 + level:
        setze pos auf lo_rand(SIZE * SIZE)
        setze pr auf pos / SIZE
        setze pc auf pos - pr * SIZE
        toggle(pr, pc)
        setze ki auf ki + 1
    setze zuege auf 0
    setze geloest auf falsch

funktion toggle(r, c):
    wenn r >= 0 und r < SIZE und c >= 0 und c < SIZE:
        setze idx auf r * SIZE + c
        wenn board[idx] == 0:
            board[idx] = 1
        sonst:
            board[idx] = 0

funktion klick(r, c):
    toggle(r, c)
    toggle(r - 1, c)
    toggle(r + 1, c)
    toggle(r, c - 1)
    toggle(r, c + 1)

funktion check_geloest():
    setze bi auf 0
    solange bi < SIZE * SIZE:
        wenn board[bi] == 1:
            gib_zurück falsch
        setze bi auf bi + 1
    gib_zurück wahr

# === Hauptprogramm ===
zeige "=== moo Lights Out ==="
zeige "Schalte alle Lichter aus!"

setze win auf fenster_erstelle("moo Lights Out", WIN_W, WIN_H)
setze lo_seed auf zeit_ms() % 99991
init_board()

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn klick_cd > 0:
        setze klick_cd auf klick_cd - 1

    wenn taste_gedrückt("r") und klick_cd == 0:
        setze lo_seed auf zeit_ms() % 99991
        init_board()
        setze klick_cd auf 15

    wenn taste_gedrückt("n") und geloest und klick_cd == 0:
        setze level auf level + 1
        init_board()
        setze klick_cd auf 15

    wenn maus_gedrückt(win) und klick_cd == 0 und geloest == falsch:
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        setze c auf boden((mx - BOARD_X) / CELL)
        setze r auf boden((my - BOARD_Y) / CELL)
        wenn r >= 0 und r < SIZE und c >= 0 und c < SIZE:
            klick(r, c)
            setze zuege auf zuege + 1
            wenn check_geloest():
                setze geloest auf wahr
        setze klick_cd auf 10

    # === Zeichnen ===
    fenster_löschen(win, "#0D1117")
    # HUD
    zeichne_rechteck(win, 0, 0, WIN_W, 50, "#1A1A2E")
    # Level-Kreise
    setze li auf 0
    solange li < level:
        zeichne_kreis(win, 14 + li * 20, 25, 8, "#42A5F5")
        setze li auf li + 1
    # Zuege
    setze zi auf 0
    solange zi < zuege und zi < 20:
        zeichne_kreis(win, WIN_W - 12 - zi * 12, 25, 3, "#90A4AE")
        setze zi auf zi + 1

    # Board
    setze r auf 0
    solange r < SIZE:
        setze c auf 0
        solange c < SIZE:
            setze dx auf BOARD_X + c * CELL
            setze dy auf BOARD_Y + r * CELL
            setze val auf board[r * SIZE + c]
            wenn val == 1:
                # Licht AN
                zeichne_rechteck(win, dx + 2, dy + 2, CELL - 4, CELL - 4, "#FFC107")
                zeichne_rechteck(win, dx + 6, dy + 6, CELL - 12, CELL - 12, "#FFEB3B")
                zeichne_kreis(win, dx + CELL / 2, dy + CELL / 2, 12, "#FFFFFF")
                zeichne_kreis(win, dx + CELL / 2, dy + CELL / 2, 6, "#FFF176")
            sonst:
                # Licht AUS
                zeichne_rechteck(win, dx + 2, dy + 2, CELL - 4, CELL - 4, "#37474F")
                zeichne_rechteck(win, dx + 6, dy + 6, CELL - 12, CELL - 12, "#263238")
                zeichne_kreis(win, dx + CELL / 2, dy + CELL / 2, 8, "#424242")
            setze c auf c + 1
        setze r auf r + 1

    # Gewonnen
    wenn geloest:
        zeichne_rechteck(win, WIN_W / 2 - 80, WIN_H - 60, 160, 40, "#4CAF50")
        zeichne_rechteck(win, WIN_W / 2 - 78, WIN_H - 58, 156, 36, "#66BB6A")
        # Haken
        zeichne_linie(win, WIN_W / 2 - 10, WIN_H - 40, WIN_W / 2, WIN_H - 30, "#FFFFFF")
        zeichne_linie(win, WIN_W / 2, WIN_H - 30, WIN_W / 2 + 15, WIN_H - 48, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Lights Out beendet. Level: " + text(level) + " Zuege: " + text(zuege)

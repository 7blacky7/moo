# ============================================================
# tic_tac_toe.moo — Tic Tac Toe mit KI
#
# Kompilieren: moo-compiler compile beispiele/tic_tac_toe.moo -o beispiele/tic_tac_toe
# Starten:     ./beispiele/tic_tac_toe
#
# Steuerung: Mausklick auf Feld, R = Neu, Escape = Beenden
# Gegner: Minimax-KI (unbesiegbar)
# ============================================================

konstante CELL auf 120
konstante BOARD_X auf 60
konstante BOARD_Y auf 80
konstante WIN_W auf 480
konstante WIN_H auf 520

# Board: 0=leer, 1=X (Spieler), 2=O (KI)
setze board auf []
setze spieler_dran auf wahr
setze game_over auf falsch
setze winner auf 0
setze klick_cd auf 0
setze x_wins auf 0
setze o_wins auf 0

funktion init_board():
    setze board auf []
    setze bi auf 0
    solange bi < 9:
        board.hinzufügen(0)
        setze bi auf bi + 1
    setze spieler_dran auf wahr
    setze game_over auf falsch
    setze winner auf 0

# Check Gewinn
funktion check_gewinn(b, spieler):
    # Reihen
    setze ri auf 0
    solange ri < 3:
        wenn b[ri * 3] == spieler und b[ri * 3 + 1] == spieler und b[ri * 3 + 2] == spieler:
            gib_zurück wahr
        setze ri auf ri + 1
    # Spalten
    setze ci auf 0
    solange ci < 3:
        wenn b[ci] == spieler und b[ci + 3] == spieler und b[ci + 6] == spieler:
            gib_zurück wahr
        setze ci auf ci + 1
    # Diagonalen
    wenn b[0] == spieler und b[4] == spieler und b[8] == spieler:
        gib_zurück wahr
    wenn b[2] == spieler und b[4] == spieler und b[6] == spieler:
        gib_zurück wahr
    gib_zurück falsch

funktion board_voll(b):
    setze bi auf 0
    solange bi < 9:
        wenn b[bi] == 0:
            gib_zurück falsch
        setze bi auf bi + 1
    gib_zurück wahr

# Minimax
funktion minimax(b, spieler, tief):
    wenn check_gewinn(b, 2):
        gib_zurück 10 - tief
    wenn check_gewinn(b, 1):
        gib_zurück tief - 10
    wenn board_voll(b):
        gib_zurück 0

    wenn spieler == 2:
        # Max (KI)
        setze best auf -100
        setze bi auf 0
        solange bi < 9:
            wenn b[bi] == 0:
                b[bi] = 2
                setze score auf minimax(b, 1, tief + 1)
                b[bi] = 0
                wenn score > best:
                    setze best auf score
            setze bi auf bi + 1
        gib_zurück best
    sonst:
        # Min (Spieler)
        setze best auf 100
        setze bi auf 0
        solange bi < 9:
            wenn b[bi] == 0:
                b[bi] = 1
                setze score auf minimax(b, 2, tief + 1)
                b[bi] = 0
                wenn score < best:
                    setze best auf score
            setze bi auf bi + 1
        gib_zurück best

funktion ki_zug():
    setze best_pos auf -1
    setze best_score auf -100
    setze bi auf 0
    solange bi < 9:
        wenn board[bi] == 0:
            board[bi] = 2
            setze score auf minimax(board, 1, 0)
            board[bi] = 0
            wenn score > best_score:
                setze best_score auf score
                setze best_pos auf bi
        setze bi auf bi + 1
    wenn best_pos >= 0:
        board[best_pos] = 2

# === Hauptprogramm ===
zeige "=== moo Tic Tac Toe ==="
zeige "Klick auf Feld, du bist X, KI ist O"

setze win auf fenster_erstelle("moo Tic Tac Toe", WIN_W, WIN_H)
init_board()

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn klick_cd > 0:
        setze klick_cd auf klick_cd - 1

    wenn taste_gedrückt("r") und klick_cd == 0:
        init_board()
        setze klick_cd auf 15

    # Spieler-Zug
    wenn spieler_dran und game_over == falsch und maus_gedrückt(win) und klick_cd == 0:
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        setze bcol auf boden((mx - BOARD_X) / CELL)
        setze brow auf boden((my - BOARD_Y) / CELL)
        wenn bcol >= 0 und bcol < 3 und brow >= 0 und brow < 3:
            setze bidx auf brow * 3 + bcol
            wenn board[bidx] == 0:
                board[bidx] = 1
                setze klick_cd auf 15
                # Prüfen
                wenn check_gewinn(board, 1):
                    setze game_over auf wahr
                    setze winner auf 1
                    setze x_wins auf x_wins + 1
                sonst wenn board_voll(board):
                    setze game_over auf wahr
                    setze winner auf 0
                sonst:
                    setze spieler_dran auf falsch

    # KI-Zug
    wenn spieler_dran == falsch und game_over == falsch:
        ki_zug()
        wenn check_gewinn(board, 2):
            setze game_over auf wahr
            setze winner auf 2
            setze o_wins auf o_wins + 1
        sonst wenn board_voll(board):
            setze game_over auf wahr
            setze winner auf 0
        sonst:
            setze spieler_dran auf wahr

    # === Zeichnen ===
    fenster_löschen(win, "#263238")
    # HUD
    zeichne_rechteck(win, 0, 0, WIN_W, 60, "#1A1A2E")
    # Spieler-Wins (X, blau)
    setze xi auf 0
    solange xi < x_wins:
        zeichne_rechteck(win, 14 + xi * 24, 20, 16, 16, "#2196F3")
        zeichne_linie(win, 18 + xi * 24, 24, 26 + xi * 24, 32, "#FFFFFF")
        zeichne_linie(win, 26 + xi * 24, 24, 18 + xi * 24, 32, "#FFFFFF")
        setze xi auf xi + 1
    # KI-Wins (O, rot)
    setze oi auf 0
    solange oi < o_wins:
        zeichne_kreis(win, WIN_W - 22 - oi * 24, 28, 8, "#F44336")
        zeichne_kreis(win, WIN_W - 22 - oi * 24, 28, 4, "#263238")
        setze oi auf oi + 1

    # Board-Grid
    setze bx auf BOARD_X
    setze by auf BOARD_Y
    # Hintergrund
    zeichne_rechteck(win, bx, by, 3 * CELL, 3 * CELL, "#37474F")
    # Zellen
    setze r auf 0
    solange r < 3:
        setze c auf 0
        solange c < 3:
            setze dx auf bx + c * CELL
            setze dy auf by + r * CELL
            zeichne_rechteck(win, dx + 2, dy + 2, CELL - 4, CELL - 4, "#ECEFF1")

            setze val auf board[r * 3 + c]
            wenn val == 1:
                # X
                setze pad auf 20
                zeichne_linie(win, dx + pad, dy + pad, dx + CELL - pad, dy + CELL - pad, "#2196F3")
                zeichne_linie(win, dx + pad + 1, dy + pad, dx + CELL - pad + 1, dy + CELL - pad, "#2196F3")
                zeichne_linie(win, dx + CELL - pad, dy + pad, dx + pad, dy + CELL - pad, "#2196F3")
                zeichne_linie(win, dx + CELL - pad + 1, dy + pad, dx + pad + 1, dy + CELL - pad, "#2196F3")
            wenn val == 2:
                # O
                zeichne_kreis(win, dx + CELL / 2, dy + CELL / 2, CELL / 2 - 20, "#F44336")
                zeichne_kreis(win, dx + CELL / 2, dy + CELL / 2, CELL / 2 - 28, "#ECEFF1")
            setze c auf c + 1
        setze r auf r + 1

    # Game Over
    wenn game_over:
        setze gfarbe auf "#FFC107"
        wenn winner == 1: setze gfarbe auf "#2196F3"
        wenn winner == 2: setze gfarbe auf "#F44336"
        zeichne_rechteck(win, WIN_W / 2 - 100, WIN_H - 60, 200, 40, gfarbe)
        wenn winner == 1:
            # X-Symbol
            zeichne_linie(win, WIN_W / 2 - 15, WIN_H - 50, WIN_W / 2 + 15, WIN_H - 30, "#FFFFFF")
            zeichne_linie(win, WIN_W / 2 + 15, WIN_H - 50, WIN_W / 2 - 15, WIN_H - 30, "#FFFFFF")
        sonst wenn winner == 2:
            zeichne_kreis(win, WIN_W / 2, WIN_H - 40, 14, "#FFFFFF")
            zeichne_kreis(win, WIN_W / 2, WIN_H - 40, 8, gfarbe)
        sonst:
            # Unentschieden: Strich
            zeichne_rechteck(win, WIN_W / 2 - 15, WIN_H - 42, 30, 4, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Tic Tac Toe beendet. X: " + text(x_wins) + " O: " + text(o_wins)

# ============================================================
# game_2048.moo — 2048 Puzzle
#
# Kompilieren: moo-compiler compile beispiele/game_2048.moo -o beispiele/game_2048
# Starten:     ./beispiele/game_2048
#
# Steuerung: WASD oder Pfeiltasten = Verschieben, R = Neu, Escape = Beenden
# Ziel: Kombiniere gleiche Zahlen bis 2048!
# ============================================================

konstante SIZE auf 4
konstante CELL auf 90
konstante GAP auf 10
konstante BOARD_X auf 20
konstante BOARD_Y auf 60
konstante WIN_W auf 420
konstante WIN_H auf 480

# Farbe je Zahl (als Anzahl Punkte: 2=1, 4=2, 8=3, 16=4, ...)
funktion tile_farbe(val):
    wenn val == 2: gib_zurück "#EEE4DA"
    wenn val == 4: gib_zurück "#EDE0C8"
    wenn val == 8: gib_zurück "#F2B179"
    wenn val == 16: gib_zurück "#F59563"
    wenn val == 32: gib_zurück "#F67C5F"
    wenn val == 64: gib_zurück "#F65E3B"
    wenn val == 128: gib_zurück "#EDCF72"
    wenn val == 256: gib_zurück "#EDCC61"
    wenn val == 512: gib_zurück "#EDC850"
    wenn val == 1024: gib_zurück "#EDC53F"
    wenn val == 2048: gib_zurück "#EDC22E"
    gib_zurück "#3C3A32"

# === PRNG ===
setze g2_seed auf 777

funktion g2_rand(max_val):
    setze g2_seed auf (g2_seed * 1103515245 + 12345) % 2147483648
    gib_zurück g2_seed % max_val

# Board (Zeile-basiert)
setze board auf []

funktion init_board():
    setze board auf []
    setze bi auf 0
    solange bi < SIZE * SIZE:
        board.hinzufügen(0)
        setze bi auf bi + 1
    spawn_tile()
    spawn_tile()

funktion spawn_tile():
    setze leer auf []
    setze bi auf 0
    solange bi < SIZE * SIZE:
        wenn board[bi] == 0:
            leer.hinzufügen(bi)
        setze bi auf bi + 1
    wenn länge(leer) > 0:
        setze pos auf leer[g2_rand(länge(leer))]
        setze val auf 2
        wenn g2_rand(10) == 0:
            setze val auf 4
        board[pos] = val

# Verschiebe + merge eine Zeile nach links
funktion merge_row(row):
    # Filter: alle != 0
    setze gefiltert auf []
    setze ri auf 0
    solange ri < SIZE:
        wenn row[ri] != 0:
            gefiltert.hinzufügen(row[ri])
        setze ri auf ri + 1
    # Merge aufeinanderfolgende gleiche
    setze gemergt auf []
    setze score_add auf 0
    setze gi auf 0
    solange gi < länge(gefiltert):
        wenn gi + 1 < länge(gefiltert) und gefiltert[gi] == gefiltert[gi + 1]:
            gemergt.hinzufügen(gefiltert[gi] * 2)
            setze score_add auf score_add + gefiltert[gi] * 2
            setze gi auf gi + 2
        sonst:
            gemergt.hinzufügen(gefiltert[gi])
            setze gi auf gi + 1
    # Mit Nullen auffuellen
    solange länge(gemergt) < SIZE:
        gemergt.hinzufügen(0)
    setze result auf {}
    result["row"] = gemergt
    result["score"] = score_add
    gib_zurück result

funktion move_links():
    setze changed auf falsch
    setze total_score auf 0
    setze r auf 0
    solange r < SIZE:
        setze zeile auf []
        setze c auf 0
        solange c < SIZE:
            zeile.hinzufügen(board[r * SIZE + c])
            setze c auf c + 1
        setze merged auf merge_row(zeile)
        setze neu auf merged["row"]
        setze total_score auf total_score + merged["score"]
        setze c auf 0
        solange c < SIZE:
            wenn board[r * SIZE + c] != neu[c]:
                setze changed auf wahr
            board[r * SIZE + c] = neu[c]
            setze c auf c + 1
        setze r auf r + 1
    setze result auf {}
    result["changed"] = changed
    result["score"] = total_score
    gib_zurück result

funktion move_rechts():
    # Reverse, move_links, reverse zurueck
    setze r auf 0
    solange r < SIZE:
        setze links auf 0
        setze rechts auf SIZE - 1
        solange links < rechts:
            setze tmp auf board[r * SIZE + links]
            board[r * SIZE + links] = board[r * SIZE + rechts]
            board[r * SIZE + rechts] = tmp
            setze links auf links + 1
            setze rechts auf rechts - 1
        setze r auf r + 1
    setze result auf move_links()
    # Reverse zurueck
    setze r auf 0
    solange r < SIZE:
        setze links auf 0
        setze rechts auf SIZE - 1
        solange links < rechts:
            setze tmp auf board[r * SIZE + links]
            board[r * SIZE + links] = board[r * SIZE + rechts]
            board[r * SIZE + rechts] = tmp
            setze links auf links + 1
            setze rechts auf rechts - 1
        setze r auf r + 1
    gib_zurück result

funktion move_oben():
    # Transponiere, move_links, transponiere zurueck
    setze neu_board auf []
    setze bi auf 0
    solange bi < SIZE * SIZE:
        neu_board.hinzufügen(0)
        setze bi auf bi + 1
    setze r auf 0
    solange r < SIZE:
        setze c auf 0
        solange c < SIZE:
            neu_board[c * SIZE + r] = board[r * SIZE + c]
            setze c auf c + 1
        setze r auf r + 1
    setze bi auf 0
    solange bi < SIZE * SIZE:
        board[bi] = neu_board[bi]
        setze bi auf bi + 1
    setze result auf move_links()
    # Zurueck
    setze neu_board2 auf []
    setze bi auf 0
    solange bi < SIZE * SIZE:
        neu_board2.hinzufügen(0)
        setze bi auf bi + 1
    setze r auf 0
    solange r < SIZE:
        setze c auf 0
        solange c < SIZE:
            neu_board2[c * SIZE + r] = board[r * SIZE + c]
            setze c auf c + 1
        setze r auf r + 1
    setze bi auf 0
    solange bi < SIZE * SIZE:
        board[bi] = neu_board2[bi]
        setze bi auf bi + 1
    gib_zurück result

funktion move_unten():
    # Transponiere, move_rechts, transponiere zurueck
    setze neu_board auf []
    setze bi auf 0
    solange bi < SIZE * SIZE:
        neu_board.hinzufügen(0)
        setze bi auf bi + 1
    setze r auf 0
    solange r < SIZE:
        setze c auf 0
        solange c < SIZE:
            neu_board[c * SIZE + r] = board[r * SIZE + c]
            setze c auf c + 1
        setze r auf r + 1
    setze bi auf 0
    solange bi < SIZE * SIZE:
        board[bi] = neu_board[bi]
        setze bi auf bi + 1
    setze result auf move_rechts()
    # Zurueck
    setze neu_board2 auf []
    setze bi auf 0
    solange bi < SIZE * SIZE:
        neu_board2.hinzufügen(0)
        setze bi auf bi + 1
    setze r auf 0
    solange r < SIZE:
        setze c auf 0
        solange c < SIZE:
            neu_board2[c * SIZE + r] = board[r * SIZE + c]
            setze c auf c + 1
        setze r auf r + 1
    setze bi auf 0
    solange bi < SIZE * SIZE:
        board[bi] = neu_board2[bi]
        setze bi auf bi + 1
    gib_zurück result

# State
setze score auf 0
setze best auf 0
setze game_over auf falsch
setze gewonnen auf falsch
setze taste_cd auf 0

# === Hauptprogramm ===
zeige "=== moo 2048 ==="
zeige "WASD=Verschieben, R=Neu, Escape=Beenden"

setze win auf fenster_erstelle("moo 2048", WIN_W, WIN_H)
setze g2_seed auf zeit_ms() % 99991
init_board()

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_cd > 0:
        setze taste_cd auf taste_cd - 1

    wenn taste_gedrückt("r") und taste_cd == 0:
        setze g2_seed auf zeit_ms() % 99991
        init_board()
        setze score auf 0
        setze game_over auf falsch
        setze gewonnen auf falsch
        setze taste_cd auf 15

    wenn taste_cd == 0 und game_over == falsch:
        setze moved auf falsch
        setze add_score auf 0
        wenn taste_gedrückt("a") oder taste_gedrückt("links"):
            setze res auf move_links()
            setze moved auf res["changed"]
            setze add_score auf res["score"]
        sonst wenn taste_gedrückt("d") oder taste_gedrückt("rechts"):
            setze res auf move_rechts()
            setze moved auf res["changed"]
            setze add_score auf res["score"]
        sonst wenn taste_gedrückt("w") oder taste_gedrückt("oben"):
            setze res auf move_oben()
            setze moved auf res["changed"]
            setze add_score auf res["score"]
        sonst wenn taste_gedrückt("s") oder taste_gedrückt("unten"):
            setze res auf move_unten()
            setze moved auf res["changed"]
            setze add_score auf res["score"]

        wenn moved:
            setze score auf score + add_score
            wenn score > best:
                setze best auf score
            spawn_tile()
            setze taste_cd auf 8
            # 2048 erreicht?
            setze bi auf 0
            solange bi < SIZE * SIZE:
                wenn board[bi] == 2048:
                    setze gewonnen auf wahr
                setze bi auf bi + 1
            # Game Over? (keine Moves mehr)
            setze hat_leer auf falsch
            setze bi auf 0
            solange bi < SIZE * SIZE:
                wenn board[bi] == 0:
                    setze hat_leer auf wahr
                setze bi auf bi + 1
            wenn hat_leer == falsch:
                setze game_over auf wahr

    # === Zeichnen ===
    fenster_löschen(win, "#FAF8EF")
    # HUD
    zeichne_rechteck(win, 0, 0, WIN_W, 50, "#BBADA0")
    # Score als Punkte
    setze si auf 0
    solange si < score / 4 und si < 50:
        zeichne_kreis(win, 12 + si * 7, 25, 3, "#FFD700")
        setze si auf si + 1

    # Board-Hintergrund
    zeichne_rechteck(win, BOARD_X - 5, BOARD_Y - 5, SIZE * (CELL + GAP) + 10, SIZE * (CELL + GAP) + 10, "#BBADA0")

    # Tiles
    setze r auf 0
    solange r < SIZE:
        setze c auf 0
        solange c < SIZE:
            setze dx auf BOARD_X + c * (CELL + GAP)
            setze dy auf BOARD_Y + r * (CELL + GAP)
            setze val auf board[r * SIZE + c]
            setze farbe auf tile_farbe(val)
            wenn val == 0:
                setze farbe auf "#CDC1B4"
            zeichne_rechteck(win, dx, dy, CELL, CELL, farbe)
            # Wert als Punkte (log2)
            wenn val > 0:
                setze dots auf 1
                setze v auf val
                solange v > 2:
                    setze v auf v / 2
                    setze dots auf dots + 1
                # Als Kreise im Quadrant-Muster
                setze di auf 0
                solange di < dots und di < 16:
                    setze dr_row auf di / 4
                    setze dc auf di - dr_row * 4
                    setze cx auf dx + 20 + dc * 16
                    setze cy auf dy + 20 + dr_row * 16
                    zeichne_kreis(win, cx, cy, 5, "#5D4037")
                    setze di auf di + 1
            setze c auf c + 1
        setze r auf r + 1

    # Game Over
    wenn game_over:
        zeichne_rechteck(win, WIN_W / 2 - 80, WIN_H / 2 - 25, 160, 50, "#D32F2F")
        zeichne_rechteck(win, WIN_W / 2 - 78, WIN_H / 2 - 23, 156, 46, "#F44336")

    wenn gewonnen:
        zeichne_rechteck(win, WIN_W / 2 - 80, WIN_H / 2 - 25, 160, 50, "#4CAF50")
        zeichne_rechteck(win, WIN_W / 2 - 78, WIN_H / 2 - 23, 156, 46, "#66BB6A")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "2048 beendet. Score: " + text(score) + " Best: " + text(best)

# ============================================================
# snake_plus.moo — Snake mit Power-Ups und Hindernissen
#
# Kompilieren: moo-compiler compile beispiele/snake_plus.moo -o beispiele/snake_plus
# Starten:     ./beispiele/snake_plus
#
# Steuerung: WASD/Pfeile=Richtung, Escape=Beenden
# Features: 3 Futter-Typen, Hindernisse, Speed-Up, Wände
# ============================================================

konstante COLS auf 30
konstante ROWS auf 22
konstante CELL auf 20
konstante WIN_W auf 600
konstante WIN_H auf 480
konstante HUD_H auf 40

# Snake-Körper (Liste von [x, y])
setze snake auf []
setze dir_x auf 1
setze dir_y auf 0
setze next_dir_x auf 1
setze next_dir_y auf 0

# Futter: 0=normal(rot), 1=gold(gelb, +3), 2=bonus(lila, +5+speedup)
setze food_x auf []
setze food_y auf []
setze food_typ auf []

# Hindernisse
setze wall_x auf []
setze wall_y auf []

# PRNG
setze sp_seed auf 12345

funktion sp_rand(max_val):
    setze sp_seed auf (sp_seed * 1103515245 + 12345) % 2147483648
    gib_zurück sp_seed % max_val

# State
setze score auf 0
setze best auf 0
setze game_over auf falsch
setze move_timer auf 0
setze speed auf 8
setze level auf 1

funktion snake_enthält(x_pos, y_pos):
    setze si auf 0
    solange si < länge(snake):
        setze teil auf snake[si]
        wenn teil[0] == x_pos und teil[1] == y_pos:
            gib_zurück wahr
        setze si auf si + 1
    gib_zurück falsch

funktion wall_enthält(x_pos, y_pos):
    setze wi auf 0
    solange wi < länge(wall_x):
        wenn wall_x[wi] == x_pos und wall_y[wi] == y_pos:
            gib_zurück wahr
        setze wi auf wi + 1
    gib_zurück falsch

funktion spawn_food(typ):
    setze versuche auf 0
    solange versuche < 100:
        setze fx auf sp_rand(COLS)
        setze fy auf sp_rand(ROWS)
        wenn snake_enthält(fx, fy) == falsch und wall_enthält(fx, fy) == falsch:
            food_x.hinzufügen(fx)
            food_y.hinzufügen(fy)
            food_typ.hinzufügen(typ)
            gib_zurück nichts
        setze versuche auf versuche + 1

funktion spawn_wall():
    setze versuche auf 0
    solange versuche < 50:
        setze wx auf sp_rand(COLS)
        setze wy auf sp_rand(ROWS)
        wenn snake_enthält(wx, wy) == falsch und wall_enthält(wx, wy) == falsch:
            # Nicht zu nah am Kopf
            setze kopf auf snake[0]
            setze ddx auf wx - kopf[0]
            setze ddy auf wy - kopf[1]
            wenn ddx * ddx + ddy * ddy > 25:
                wall_x.hinzufügen(wx)
                wall_y.hinzufügen(wy)
                gib_zurück nichts
        setze versuche auf versuche + 1

funktion neu_starten():
    setze snake auf []
    snake.hinzufügen([15, 11])
    snake.hinzufügen([14, 11])
    snake.hinzufügen([13, 11])
    setze dir_x auf 1
    setze dir_y auf 0
    setze next_dir_x auf 1
    setze next_dir_y auf 0
    setze food_x auf []
    setze food_y auf []
    setze food_typ auf []
    setze wall_x auf []
    setze wall_y auf []
    setze score auf 0
    setze game_over auf falsch
    setze speed auf 8
    setze level auf 1
    setze move_timer auf 0
    spawn_food(0)
    spawn_food(0)
    spawn_food(1)

# === Hauptprogramm ===
zeige "=== moo Snake Plus ==="
zeige "WASD=Richtung, Gold=+3, Lila=+5 & Speed"

setze win auf fenster_erstelle("moo Snake Plus", WIN_W, WIN_H)
setze sp_seed auf zeit_ms() % 99991
neu_starten()

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_gedrückt("r"):
        setze sp_seed auf zeit_ms() % 99991
        neu_starten()

    # Richtung (nicht umkehren)
    wenn taste_gedrückt("w") oder taste_gedrückt("oben"):
        wenn dir_y != 1:
            setze next_dir_x auf 0
            setze next_dir_y auf -1
    wenn taste_gedrückt("s") oder taste_gedrückt("unten"):
        wenn dir_y != -1:
            setze next_dir_x auf 0
            setze next_dir_y auf 1
    wenn taste_gedrückt("a") oder taste_gedrückt("links"):
        wenn dir_x != 1:
            setze next_dir_x auf -1
            setze next_dir_y auf 0
    wenn taste_gedrückt("d") oder taste_gedrückt("rechts"):
        wenn dir_x != -1:
            setze next_dir_x auf 1
            setze next_dir_y auf 0

    # Bewegung
    wenn game_over == falsch:
        setze move_timer auf move_timer + 1
        wenn move_timer >= speed:
            setze move_timer auf 0
            setze dir_x auf next_dir_x
            setze dir_y auf next_dir_y
            setze kopf auf snake[0]
            setze nx auf kopf[0] + dir_x
            setze ny auf kopf[1] + dir_y

            # Wand-Kollision
            wenn nx < 0 oder nx >= COLS oder ny < 0 oder ny >= ROWS:
                setze game_over auf wahr
            sonst wenn wall_enthält(nx, ny):
                setze game_over auf wahr
            sonst wenn snake_enthält(nx, ny):
                setze game_over auf wahr
            sonst:
                # Kopf hinzufuegen
                setze neue_snake auf [[nx, ny]]
                setze si auf 0
                solange si < länge(snake):
                    neue_snake.hinzufügen(snake[si])
                    setze si auf si + 1
                setze snake auf neue_snake

                # Futter?
                setze gegessen auf -1
                setze fi auf 0
                solange fi < länge(food_x):
                    wenn food_x[fi] == nx und food_y[fi] == ny:
                        setze gegessen auf fi
                    setze fi auf fi + 1

                wenn gegessen >= 0:
                    setze typ auf food_typ[gegessen]
                    wenn typ == 0:
                        setze score auf score + 1
                    wenn typ == 1:
                        setze score auf score + 3
                    wenn typ == 2:
                        setze score auf score + 5
                        wenn speed > 3:
                            setze speed auf speed - 1
                    # Food entfernen (shift)
                    setze neu_fx auf []
                    setze neu_fy auf []
                    setze neu_ft auf []
                    setze fi auf 0
                    solange fi < länge(food_x):
                        wenn fi != gegessen:
                            neu_fx.hinzufügen(food_x[fi])
                            neu_fy.hinzufügen(food_y[fi])
                            neu_ft.hinzufügen(food_typ[fi])
                        setze fi auf fi + 1
                    setze food_x auf neu_fx
                    setze food_y auf neu_fy
                    setze food_typ auf neu_ft
                    # Neues Futter
                    setze neu_typ auf sp_rand(10)
                    wenn neu_typ < 6: setze neu_typ auf 0
                    sonst wenn neu_typ < 9: setze neu_typ auf 1
                    sonst: setze neu_typ auf 2
                    spawn_food(neu_typ)

                    # Level-Up
                    wenn score > level * 10:
                        setze level auf level + 1
                        spawn_wall()
                        wenn speed > 3:
                            setze speed auf speed - 1
                sonst:
                    # Schwanz entfernen
                    setze neu_snake auf []
                    setze si auf 0
                    solange si < länge(snake) - 1:
                        neu_snake.hinzufügen(snake[si])
                        setze si auf si + 1
                    setze snake auf neu_snake

                wenn score > best:
                    setze best auf score

    # === Zeichnen ===
    fenster_löschen(win, "#1A1A2E")
    # HUD
    zeichne_rechteck(win, 0, 0, WIN_W, HUD_H, "#0D1117")
    # Score
    setze si auf 0
    solange si < score und si < 50:
        zeichne_kreis(win, 12 + si * 8, 20, 3, "#FFD700")
        setze si auf si + 1
    # Level
    setze li auf 0
    solange li < level:
        zeichne_kreis(win, WIN_W - 16 - li * 14, 20, 6, "#42A5F5")
        setze li auf li + 1

    # Spielfeld-Hintergrund
    zeichne_rechteck(win, 0, HUD_H, WIN_W, WIN_H - HUD_H, "#212121")

    # Hindernisse
    setze wi auf 0
    solange wi < länge(wall_x):
        setze dx auf wall_x[wi] * CELL
        setze dy auf HUD_H + wall_y[wi] * CELL
        zeichne_rechteck(win, dx, dy, CELL, CELL, "#5D4037")
        zeichne_rechteck(win, dx + 2, dy + 2, CELL - 4, CELL - 4, "#795548")
        setze wi auf wi + 1

    # Futter
    setze fi auf 0
    solange fi < länge(food_x):
        setze dx auf food_x[fi] * CELL + CELL / 2
        setze dy auf HUD_H + food_y[fi] * CELL + CELL / 2
        setze farbe auf "#F44336"
        wenn food_typ[fi] == 1: setze farbe auf "#FFD700"
        wenn food_typ[fi] == 2: setze farbe auf "#9C27B0"
        zeichne_kreis(win, dx, dy, CELL / 2 - 2, farbe)
        zeichne_kreis(win, dx - 2, dy - 2, CELL / 3, "#FFFFFF")
        setze fi auf fi + 1

    # Snake
    setze si auf 0
    solange si < länge(snake):
        setze teil auf snake[si]
        setze dx auf teil[0] * CELL
        setze dy auf HUD_H + teil[1] * CELL
        wenn si == 0:
            # Kopf
            zeichne_rechteck(win, dx, dy, CELL, CELL, "#66BB6A")
            zeichne_rechteck(win, dx + 2, dy + 2, CELL - 4, CELL - 4, "#81C784")
            # Augen
            zeichne_kreis(win, dx + 6, dy + 6, 3, "#FFFFFF")
            zeichne_kreis(win, dx + CELL - 6, dy + 6, 3, "#FFFFFF")
            zeichne_kreis(win, dx + 6, dy + 6, 1, "#000000")
            zeichne_kreis(win, dx + CELL - 6, dy + 6, 1, "#000000")
        sonst:
            zeichne_rechteck(win, dx + 1, dy + 1, CELL - 2, CELL - 2, "#4CAF50")
            zeichne_rechteck(win, dx + 3, dy + 3, CELL - 6, CELL - 6, "#66BB6A")
        setze si auf si + 1

    # Game Over
    wenn game_over:
        zeichne_rechteck(win, WIN_W / 2 - 80, WIN_H / 2 - 25, 160, 50, "#D32F2F")
        zeichne_rechteck(win, WIN_W / 2 - 78, WIN_H / 2 - 23, 156, 46, "#F44336")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Snake Plus beendet. Score: " + text(score) + " Level: " + text(level)

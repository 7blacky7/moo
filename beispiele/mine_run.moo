# ============================================================
# mine_run.moo — Mine Run (Dodge & Collect)
#
# Kompilieren: moo-compiler compile beispiele/mine_run.moo -o beispiele/mine_run
# Starten:     ./beispiele/mine_run
#
# Steuerung: Links/Rechts = Bewegen, Escape = Beenden
# Ziel: Gold sammeln, Minen ausweichen
# ============================================================

konstante WIN_W auf 400
konstante WIN_H auf 600
konstante LANES auf 5
konstante LANE_W auf 80
konstante OBJ_SIZE auf 30

# PRNG
setze mr_seed auf 42

funktion mr_rand(max_val):
    setze mr_seed auf (mr_seed * 1103515245 + 12345) % 2147483648
    gib_zurück mr_seed % max_val

# Spieler in Lane 2 (Mitte)
setze player_lane auf 2
setze score auf 0
setze best auf 0
setze leben auf 3
setze game_over auf falsch
setze taste_cd auf 0
setze spawn_cd auf 0
setze speed auf 4.0
setze level auf 1

# Objekte (Gold=0, Mine=1)
setze obj_lane auf []
setze obj_y auf []
setze obj_typ auf []
setze obj_aktiv auf []
setze obj_count auf 0

funktion spawn_obj():
    setze lane auf mr_rand(LANES)
    setze typ auf mr_rand(10)
    wenn typ < 4: setze typ auf 0
    sonst: setze typ auf 1
    obj_lane.hinzufügen(lane)
    obj_y.hinzufügen(-50.0)
    obj_typ.hinzufügen(typ)
    obj_aktiv.hinzufügen(wahr)
    setze obj_count auf obj_count + 1

funktion neu_starten():
    setze player_lane auf 2
    setze score auf 0
    setze leben auf 3
    setze game_over auf falsch
    setze speed auf 4.0
    setze level auf 1
    setze obj_count auf 0
    setze obj_lane auf []
    setze obj_y auf []
    setze obj_typ auf []
    setze obj_aktiv auf []

# === Hauptprogramm ===
zeige "=== moo Mine Run ==="
zeige "Links/Rechts = Lane wechseln, Gold sammeln, Minen meiden"

setze win auf fenster_erstelle("moo Mine Run", WIN_W, WIN_H)
setze mr_seed auf zeit_ms() % 99991

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_cd > 0:
        setze taste_cd auf taste_cd - 1

    wenn taste_gedrückt("r") und game_over:
        neu_starten()

    wenn game_over == falsch:
        # Bewegung
        wenn taste_cd == 0:
            wenn taste_gedrückt("a") oder taste_gedrückt("links"):
                wenn player_lane > 0:
                    setze player_lane auf player_lane - 1
                    setze taste_cd auf 8
            wenn taste_gedrückt("d") oder taste_gedrückt("rechts"):
                wenn player_lane < LANES - 1:
                    setze player_lane auf player_lane + 1
                    setze taste_cd auf 8

        # Spawnen
        setze spawn_cd auf spawn_cd + 1
        wenn spawn_cd >= 30:
            setze spawn_cd auf 0
            spawn_obj()

        # Objekte bewegen
        setze oi auf 0
        solange oi < obj_count:
            wenn obj_aktiv[oi]:
                obj_y[oi] = obj_y[oi] + speed
                # Kollision mit Spieler?
                wenn obj_lane[oi] == player_lane und obj_y[oi] > WIN_H - 100 und obj_y[oi] < WIN_H - 40:
                    wenn obj_typ[oi] == 0:
                        setze score auf score + 1
                    sonst:
                        setze leben auf leben - 1
                        wenn leben <= 0:
                            setze game_over auf wahr
                            wenn score > best:
                                setze best auf score
                    obj_aktiv[oi] = falsch
                # Unten raus
                wenn obj_y[oi] > WIN_H:
                    obj_aktiv[oi] = falsch
            setze oi auf oi + 1

        # Level-Up
        wenn score > level * 10:
            setze level auf level + 1
            setze speed auf speed + 0.5

    # === Zeichnen ===
    # Hintergrund: Tunnel
    fenster_löschen(win, "#37474F")
    # Lane-Linien
    setze li_idx auf 0
    solange li_idx <= LANES:
        setze lx auf li_idx * LANE_W
        zeichne_rechteck(win, lx - 1, 0, 2, WIN_H, "#263238")
        setze li_idx auf li_idx + 1

    # Geschwindigkeitslinien
    setze sl auf 0
    solange sl < WIN_H:
        setze y_off auf (zeit_ms() / 16) % 40
        zeichne_rechteck(win, 0, sl + y_off, 4, 10, "#455A64")
        zeichne_rechteck(win, WIN_W - 4, sl + y_off, 4, 10, "#455A64")
        setze sl auf sl + 40

    # Objekte
    setze oi auf 0
    solange oi < obj_count:
        wenn obj_aktiv[oi]:
            setze cx auf obj_lane[oi] * LANE_W + LANE_W / 2
            setze cy auf obj_y[oi]
            wenn obj_typ[oi] == 0:
                # Gold
                zeichne_kreis(win, cx, cy, 18, "#FFC107")
                zeichne_kreis(win, cx, cy, 12, "#FFD700")
                zeichne_kreis(win, cx - 5, cy - 5, 4, "#FFEB3B")
            sonst:
                # Mine
                zeichne_kreis(win, cx, cy, 18, "#B71C1C")
                zeichne_kreis(win, cx, cy, 12, "#263238")
                # Strahlen
                zeichne_linie(win, cx - 20, cy, cx + 20, cy, "#D32F2F")
                zeichne_linie(win, cx, cy - 20, cx, cy + 20, "#D32F2F")
                zeichne_linie(win, cx - 14, cy - 14, cx + 14, cy + 14, "#D32F2F")
                zeichne_linie(win, cx + 14, cy - 14, cx - 14, cy + 14, "#D32F2F")
        setze oi auf oi + 1

    # Spieler
    wenn game_over == falsch:
        setze pcx auf player_lane * LANE_W + LANE_W / 2
        setze pcy auf WIN_H - 70
        # Mine-Cart
        zeichne_rechteck(win, pcx - 20, pcy - 15, 40, 30, "#5D4037")
        zeichne_rechteck(win, pcx - 18, pcy - 13, 36, 26, "#795548")
        # Raeder
        zeichne_kreis(win, pcx - 14, pcy + 15, 6, "#424242")
        zeichne_kreis(win, pcx + 14, pcy + 15, 6, "#424242")
        # Bergmann im Cart
        zeichne_kreis(win, pcx, pcy - 22, 10, "#FFC107")
        zeichne_kreis(win, pcx, pcy - 22, 6, "#FFE0B2")

    # HUD
    zeichne_rechteck(win, 0, 0, WIN_W, 36, "#1A1A2E")
    # Score
    setze si auf 0
    solange si < score und si < 30:
        zeichne_kreis(win, 12 + si * 8, 18, 3, "#FFD700")
        setze si auf si + 1
    # Leben
    setze hi auf 0
    solange hi < leben:
        zeichne_kreis(win, WIN_W - 16 - hi * 22, 18, 8, "#E91E63")
        setze hi auf hi + 1

    # Game Over
    wenn game_over:
        zeichne_rechteck(win, WIN_W / 2 - 80, WIN_H / 2 - 25, 160, 50, "#D32F2F")
        zeichne_rechteck(win, WIN_W / 2 - 78, WIN_H / 2 - 23, 156, 46, "#F44336")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Mine Run beendet. Score: " + text(score) + " Best: " + text(best)

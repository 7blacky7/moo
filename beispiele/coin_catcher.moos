# ============================================================
# coin_catcher.moo — Coin Catcher
#
# Kompilieren: moo-compiler compile beispiele/coin_catcher.moo -o beispiele/coin_catcher
# Starten:     ./beispiele/coin_catcher
#
# Steuerung: Maus bewegt Korb, Escape=Beenden
# Ziel: Fangende Muenzen, vermeide Steine, 60s Zeit
# ============================================================

konstante WIN_W auf 500
konstante WIN_H auf 600
konstante KORB_W auf 80
konstante KORB_H auf 30
konstante MAX_OBJ auf 20
konstante SPIEL_DAUER auf 3600

setze korb_x auf 250.0
setze score auf 0
setze best auf 0
setze misses auf 0
setze frames auf 0
setze game_over auf falsch
setze spawn_cd auf 0

# PRNG
setze cc_seed auf 42

funktion cc_rand(max_val):
    setze cc_seed auf (cc_seed * 1103515245 + 12345) % 2147483648
    gib_zurück cc_seed % max_val

# Objekte: 0=muenze, 1=stein, 2=herz
setze obj_x auf []
setze obj_y auf []
setze obj_vy auf []
setze obj_typ auf []
setze obj_aktiv auf []
setze obj_count auf 0

funktion spawn_obj():
    wenn obj_count < MAX_OBJ:
        setze ox auf 20 + cc_rand(WIN_W - 40)
        setze typ auf cc_rand(100)
        wenn typ < 65: setze typ auf 0
        sonst wenn typ < 90: setze typ auf 1
        sonst: setze typ auf 2
        obj_x.hinzufügen(ox * 1.0)
        obj_y.hinzufügen(-20.0)
        obj_vy.hinzufügen(3.0 + cc_rand(40) / 10.0)
        obj_typ.hinzufügen(typ)
        obj_aktiv.hinzufügen(wahr)
        setze obj_count auf obj_count + 1

funktion neu_starten():
    setze korb_x auf 250.0
    setze score auf 0
    setze misses auf 0
    setze frames auf 0
    setze game_over auf falsch
    setze obj_count auf 0
    setze obj_x auf []
    setze obj_y auf []
    setze obj_vy auf []
    setze obj_typ auf []
    setze obj_aktiv auf []

# === Hauptprogramm ===
zeige "=== moo Coin Catcher ==="
zeige "Maus bewegt Korb, fange Muenzen, 60 Sekunden"

setze win auf fenster_erstelle("moo Coin Catcher", WIN_W, WIN_H)
setze cc_seed auf zeit_ms() % 99991

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_gedrückt("r") und game_over:
        neu_starten()

    wenn game_over == falsch:
        setze frames auf frames + 1
        wenn frames >= SPIEL_DAUER:
            setze game_over auf wahr
            wenn score > best:
                setze best auf score

        # Maus → Korb
        setze korb_x auf maus_x(win) - KORB_W / 2
        wenn korb_x < 0: setze korb_x auf 0.0
        wenn korb_x > WIN_W - KORB_W: setze korb_x auf WIN_W - KORB_W * 1.0

        # Spawn
        setze spawn_cd auf spawn_cd + 1
        setze spawn_rate auf 25
        wenn frames > 1200: setze spawn_rate auf 18
        wenn frames > 2400: setze spawn_rate auf 12
        wenn spawn_cd >= spawn_rate:
            setze spawn_cd auf 0
            spawn_obj()

        # Objekte bewegen
        setze oi auf 0
        solange oi < obj_count:
            wenn obj_aktiv[oi]:
                obj_y[oi] = obj_y[oi] + obj_vy[oi]
                # Korb-Kollision?
                wenn obj_y[oi] > WIN_H - 80 und obj_y[oi] < WIN_H - 40:
                    wenn obj_x[oi] > korb_x und obj_x[oi] < korb_x + KORB_W:
                        wenn obj_typ[oi] == 0:
                            setze score auf score + 1
                        wenn obj_typ[oi] == 1:
                            setze misses auf misses + 1
                        wenn obj_typ[oi] == 2:
                            setze score auf score + 5
                        obj_aktiv[oi] = falsch
                # Verpasst
                wenn obj_y[oi] > WIN_H:
                    wenn obj_aktiv[oi] und obj_typ[oi] == 0:
                        setze misses auf misses + 1
                    obj_aktiv[oi] = falsch
            setze oi auf oi + 1

    # === Zeichnen ===
    # Himmel
    fenster_löschen(win, "#81D4FA")
    # Wolken
    zeichne_kreis(win, 80, 80, 20, "#FFFFFF")
    zeichne_kreis(win, 100, 75, 18, "#FFFFFF")
    zeichne_kreis(win, 350, 120, 22, "#FFFFFF")
    zeichne_kreis(win, 370, 115, 16, "#FFFFFF")
    # Boden
    zeichne_rechteck(win, 0, WIN_H - 40, WIN_W, 40, "#8D6E63")
    zeichne_rechteck(win, 0, WIN_H - 40, WIN_W, 6, "#4CAF50")

    # Objekte
    setze oi auf 0
    solange oi < obj_count:
        wenn obj_aktiv[oi]:
            setze cx auf obj_x[oi]
            setze cy auf obj_y[oi]
            wenn obj_typ[oi] == 0:
                zeichne_kreis(win, cx, cy, 12, "#FFC107")
                zeichne_kreis(win, cx, cy, 8, "#FFD700")
                zeichne_kreis(win, cx - 3, cy - 3, 3, "#FFEB3B")
            wenn obj_typ[oi] == 1:
                zeichne_kreis(win, cx, cy, 14, "#5D4037")
                zeichne_kreis(win, cx - 2, cy - 2, 8, "#795548")
            wenn obj_typ[oi] == 2:
                zeichne_kreis(win, cx - 6, cy, 8, "#E91E63")
                zeichne_kreis(win, cx + 6, cy, 8, "#E91E63")
                zeichne_rechteck(win, cx - 12, cy, 24, 12, "#E91E63")
        setze oi auf oi + 1

    # Korb
    zeichne_rechteck(win, korb_x, WIN_H - 60, KORB_W, KORB_H, "#6D4C41")
    zeichne_rechteck(win, korb_x + 2, WIN_H - 58, KORB_W - 4, KORB_H - 4, "#8D6E63")
    # Korb-Streifen
    zeichne_linie(win, korb_x + 15, WIN_H - 58, korb_x + 15, WIN_H - 34, "#5D4037")
    zeichne_linie(win, korb_x + 35, WIN_H - 58, korb_x + 35, WIN_H - 34, "#5D4037")
    zeichne_linie(win, korb_x + 55, WIN_H - 58, korb_x + 55, WIN_H - 34, "#5D4037")

    # HUD
    zeichne_rechteck(win, 0, 0, WIN_W, 40, "#1A1A2E")
    # Score
    setze si auf 0
    solange si < score und si < 40:
        zeichne_kreis(win, 12 + si * 8, 20, 3, "#FFD700")
        setze si auf si + 1
    # Timer
    setze rest auf SPIEL_DAUER - frames
    wenn rest < 0: setze rest auf 0
    setze bw auf rest * 120 / SPIEL_DAUER
    zeichne_rechteck(win, WIN_W / 2 - 60, 16, 120, 8, "#424242")
    setze tc auf "#4CAF50"
    wenn rest < SPIEL_DAUER / 3: setze tc auf "#F44336"
    zeichne_rechteck(win, WIN_W / 2 - 60, 16, bw, 8, tc)
    # Misses
    setze mi auf 0
    solange mi < misses und mi < 15:
        zeichne_kreis(win, WIN_W - 12 - mi * 10, 20, 3, "#F44336")
        setze mi auf mi + 1

    # Game Over
    wenn game_over:
        zeichne_rechteck(win, WIN_W / 2 - 80, WIN_H / 2 - 25, 160, 50, "#1565C0")
        zeichne_rechteck(win, WIN_W / 2 - 78, WIN_H / 2 - 23, 156, 46, "#1976D2")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Coin Catcher beendet. Score: " + text(score)

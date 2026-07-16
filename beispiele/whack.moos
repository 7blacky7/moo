# ============================================================
# whack.moo — Whack-a-Mole
#
# Kompilieren: moo-compiler compile beispiele/whack.moo -o beispiele/whack
# Starten:     ./beispiele/whack
#
# Steuerung: Mausklick = Maulwurf treffen, Escape = Beenden
# Ziel: Triff so viele Maulwuerfe wie moeglich in 30 Sekunden
# ============================================================

konstante WIN_W auf 480
konstante WIN_H auf 480
konstante GRID_COLS auf 3
konstante GRID_ROWS auf 3
konstante LOCH_R auf 40
konstante MOLE_R auf 30
konstante SPIEL_DAUER auf 1800

# === Farben ===
konstante C_GRAS auf "#4CAF50"
konstante C_GRAS_HELL auf "#66BB6A"
konstante C_LOCH auf "#5D4037"
konstante C_LOCH_INNEN auf "#3E2723"
konstante C_MOLE auf "#8D6E63"
konstante C_MOLE_BAUCH auf "#A1887F"
konstante C_MOLE_NASE auf "#FF7043"
konstante C_GOLD auf "#FFD700"

# === Loecher ===
setze loch_x auf []
setze loch_y auf []
setze mole_aktiv auf []
setze mole_timer auf []
setze mole_getroffen auf []

# === State ===
setze score auf 0
setze misses auf 0
setze frames auf 0
setze game_over auf falsch
setze klick_cd auf 0
setze spawn_timer auf 0
setze schwierigkeit auf 40

# PRNG
setze wh_seed auf 77777

funktion wh_zufall(max_val):
    setze wh_seed auf (wh_seed * 1103515245 + 12345) % 2147483648
    gib_zurück wh_seed % max_val

# === Init ===
funktion init_loecher():
    setze loch_x auf []
    setze loch_y auf []
    setze mole_aktiv auf []
    setze mole_timer auf []
    setze mole_getroffen auf []
    setze row_idx auf 0
    solange row_idx < GRID_ROWS:
        setze col_idx auf 0
        solange col_idx < GRID_COLS:
            setze cx_pos auf 80 + col_idx * 160
            setze cy_pos auf 120 + row_idx * 130
            loch_x.hinzufügen(cx_pos)
            loch_y.hinzufügen(cy_pos)
            mole_aktiv.hinzufügen(falsch)
            mole_timer.hinzufügen(0)
            mole_getroffen.hinzufügen(falsch)
            setze col_idx auf col_idx + 1
        setze row_idx auf row_idx + 1

funktion neues_spiel():
    setze score auf 0
    setze misses auf 0
    setze frames auf 0
    setze game_over auf falsch
    setze spawn_timer auf 0
    setze schwierigkeit auf 40
    init_loecher()

# === Maulwurf zeichnen ===
funktion zeichne_mole(win, cx_pos, cy_pos, getroffen):
    wenn getroffen:
        # Getroffen — Sterne
        zeichne_kreis(win, cx_pos - 12, cy_pos - 16, 4, C_GOLD)
        zeichne_kreis(win, cx_pos + 12, cy_pos - 20, 3, C_GOLD)
        zeichne_kreis(win, cx_pos + 4, cy_pos - 24, 5, C_GOLD)
    sonst:
        # Kopf
        zeichne_kreis(win, cx_pos, cy_pos - 10, MOLE_R, C_MOLE)
        # Bauch/Gesicht
        zeichne_kreis(win, cx_pos, cy_pos - 6, 22, C_MOLE_BAUCH)
        # Augen
        zeichne_kreis(win, cx_pos - 10, cy_pos - 18, 6, "#FFFFFF")
        zeichne_kreis(win, cx_pos + 10, cy_pos - 18, 6, "#FFFFFF")
        zeichne_kreis(win, cx_pos - 10, cy_pos - 18, 3, "#000000")
        zeichne_kreis(win, cx_pos + 10, cy_pos - 18, 3, "#000000")
        # Nase
        zeichne_kreis(win, cx_pos, cy_pos - 8, 6, C_MOLE_NASE)
        # Zaehne
        zeichne_rechteck(win, cx_pos - 4, cy_pos - 2, 3, 5, "#FFFFFF")
        zeichne_rechteck(win, cx_pos + 1, cy_pos - 2, 3, 5, "#FFFFFF")

# === Loch zeichnen ===
funktion zeichne_loch(win, cx_pos, cy_pos):
    # Schatten-Ellipse (als flaches Rechteck + Kreise)
    zeichne_kreis(win, cx_pos, cy_pos + 10, LOCH_R, C_LOCH)
    zeichne_rechteck(win, cx_pos - LOCH_R, cy_pos + 2, LOCH_R * 2, 16, C_LOCH)
    zeichne_kreis(win, cx_pos, cy_pos + 6, LOCH_R - 6, C_LOCH_INNEN)

# === HUD ===
funktion zeichne_whack_hud(win):
    zeichne_rechteck(win, 0, 0, WIN_W, 50, "#1A1A2E")
    # Score
    setze si auf 0
    solange si < score und si < 30:
        zeichne_kreis(win, 16 + si * 12, 20, 4, C_GOLD)
        setze si auf si + 1
    # Timer-Balken
    setze rest auf SPIEL_DAUER - frames
    wenn rest < 0:
        setze rest auf 0
    setze balken_w auf (rest * 200) / SPIEL_DAUER
    zeichne_rechteck(win, WIN_W / 2 - 100, 35, 200, 8, "#424242")
    wenn balken_w > 0:
        setze timer_farbe auf "#4CAF50"
        wenn rest < SPIEL_DAUER / 4:
            setze timer_farbe auf "#F44336"
        sonst wenn rest < SPIEL_DAUER / 2:
            setze timer_farbe auf "#FF9800"
        zeichne_rechteck(win, WIN_W / 2 - 100, 35, balken_w, 8, timer_farbe)
    # Misses
    setze mi auf 0
    solange mi < misses und mi < 10:
        zeichne_kreis(win, WIN_W - 16 - mi * 14, 20, 4, "#F44336")
        setze mi auf mi + 1

# === Hauptprogramm ===
zeige "=== moo Whack-a-Mole ==="
zeige "Klick auf Maulwuerfe! 30 Sekunden."

setze win auf fenster_erstelle("moo Whack-a-Mole", WIN_W, WIN_H)
setze wh_seed auf zeit_ms() % 99991
neues_spiel()

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_gedrückt("r"):
        setze wh_seed auf zeit_ms() % 99991
        neues_spiel()

    wenn game_over == falsch:
        setze frames auf frames + 1

        # Timer abgelaufen?
        wenn frames >= SPIEL_DAUER:
            setze game_over auf wahr

        # Schwierigkeit erhoehen
        wenn frames % 300 == 0 und schwierigkeit > 15:
            setze schwierigkeit auf schwierigkeit - 3

        # Maulwuerfe spawnen
        setze spawn_timer auf spawn_timer + 1
        wenn spawn_timer >= schwierigkeit:
            setze spawn_timer auf 0
            setze slot auf wh_zufall(9)
            wenn mole_aktiv[slot] == falsch:
                mole_aktiv[slot] = wahr
                mole_timer[slot] = 30 + wh_zufall(30)
                mole_getroffen[slot] = falsch

        # Maulwurf-Timer
        setze mi auf 0
        solange mi < 9:
            wenn mole_aktiv[mi]:
                mole_timer[mi] = mole_timer[mi] - 1
                wenn mole_timer[mi] <= 0:
                    wenn mole_getroffen[mi] == falsch:
                        setze misses auf misses + 1
                    mole_aktiv[mi] = falsch
            setze mi auf mi + 1

    # Klick
    wenn klick_cd > 0:
        setze klick_cd auf klick_cd - 1

    wenn maus_gedrückt(win) und klick_cd == 0 und game_over == falsch:
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        setze treffer auf falsch
        setze mi auf 0
        solange mi < 9:
            wenn mole_aktiv[mi] und mole_getroffen[mi] == falsch:
                setze dist_x auf mx - loch_x[mi]
                setze dist_y auf my - loch_y[mi]
                setze dist_sq auf dist_x * dist_x + dist_y * dist_y
                wenn dist_sq < MOLE_R * MOLE_R * 4:
                    mole_getroffen[mi] = wahr
                    mole_timer[mi] = 10
                    setze score auf score + 1
                    setze treffer auf wahr
            setze mi auf mi + 1
        setze klick_cd auf 6

    # === Zeichnen ===
    # Gras-Hintergrund
    fenster_löschen(win, C_GRAS)
    # Gras-Streifen
    setze stripe auf 0
    solange stripe < WIN_H:
        zeichne_rechteck(win, 0, stripe, WIN_W, 4, C_GRAS_HELL)
        setze stripe auf stripe + 20

    # Loecher + Maulwuerfe
    setze mi auf 0
    solange mi < 9:
        zeichne_loch(win, loch_x[mi], loch_y[mi])
        wenn mole_aktiv[mi]:
            zeichne_mole(win, loch_x[mi], loch_y[mi], mole_getroffen[mi])
        setze mi auf mi + 1

    zeichne_whack_hud(win)

    # Game Over
    wenn game_over:
        zeichne_rechteck(win, WIN_W / 2 - 100, WIN_H / 2 - 40, 200, 80, "#1A1A2E")
        zeichne_rechteck(win, WIN_W / 2 - 98, WIN_H / 2 - 38, 196, 76, "#263238")
        # Score-Kreise im Game-Over
        setze gi auf 0
        solange gi < score und gi < 20:
            zeichne_kreis(win, WIN_W / 2 - 90 + gi * 9, WIN_H / 2, 3, C_GOLD)
            setze gi auf gi + 1
        # R = Restart Hinweis
        zeichne_rechteck(win, WIN_W / 2 - 20, WIN_H / 2 + 14, 40, 16, "#2196F3")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Whack-a-Mole beendet. Score: " + text(score) + " Misses: " + text(misses)

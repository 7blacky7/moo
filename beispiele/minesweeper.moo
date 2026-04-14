# ============================================================
# minesweeper.moo — Klassisches Minesweeper
#
# Kompilieren: moo-compiler compile beispiele/minesweeper.moo -o beispiele/minesweeper
# Starten:     ./beispiele/minesweeper
#
# Steuerung:
#   Mausklick      - Feld aufdecken
#   Rechtsklick    - Flagge setzen/entfernen (TODO: braucht rechte Maustaste)
#   F              - Flagge auf Feld unter Maus
#   R              - Neues Spiel
#   Escape         - Beenden
#
# Schwierigkeit: 10x10 Grid, 15 Minen
# ============================================================

konstante COLS auf 10
konstante ROWS auf 10
konstante MINEN auf 15
konstante CELL auf 40
konstante HUD_H auf 48
konstante WIN_W auf 400
konstante WIN_H auf 448

# Zell-Status: 0=verdeckt, 1=aufgedeckt, 2=flagge
# Zell-Inhalt: -1=Mine, 0-8=Anzahl benachbarter Minen

# Farben fuer Zahlen 1-8
funktion zahl_farbe(n):
    wenn n == 1:
        gib_zurück "#1565C0"
    wenn n == 2:
        gib_zurück "#2E7D32"
    wenn n == 3:
        gib_zurück "#C62828"
    wenn n == 4:
        gib_zurück "#6A1B9A"
    wenn n == 5:
        gib_zurück "#E65100"
    wenn n == 6:
        gib_zurück "#00838F"
    wenn n == 7:
        gib_zurück "#37474F"
    wenn n == 8:
        gib_zurück "#78909C"
    gib_zurück "#000000"

# === Spielfeld erstellen ===
funktion neues_spiel():
    setze spiel auf {}

    # Inhalt: flache Liste ROWS*COLS
    setze inhalt auf []
    setze status auf []
    setze i auf 0
    solange i < ROWS * COLS:
        inhalt.hinzufügen(0)
        status.hinzufügen(0)
        setze i auf i + 1

    # Minen zufaellig platzieren
    setze gesetzt auf 0
    setze seed auf zeit_ms() % 99991
    solange gesetzt < MINEN:
        setze seed auf (seed * 1103515245 + 12345) % 2147483648
        setze pos auf (seed / 2147483648.0 * ROWS * COLS)
        setze pos auf boden(pos) % (ROWS * COLS)
        wenn inhalt[pos] != -1:
            inhalt[pos] = -1
            setze gesetzt auf gesetzt + 1

    # Nachbar-Zahlen berechnen
    setze row auf 0
    solange row < ROWS:
        setze col auf 0
        solange col < COLS:
            setze idx auf row * COLS + col
            wenn inhalt[idx] != -1:
                setze count auf 0
                setze dr auf -1
                solange dr <= 1:
                    setze dc auf -1
                    solange dc <= 1:
                        wenn dr != 0 oder dc != 0:
                            setze nr auf row + dr
                            setze nc auf col + dc
                            wenn nr >= 0 und nr < ROWS und nc >= 0 und nc < COLS:
                                wenn inhalt[nr * COLS + nc] == -1:
                                    setze count auf count + 1
                        setze dc auf dc + 1
                    setze dr auf dr + 1
                inhalt[idx] = count
            setze col auf col + 1
        setze row auf row + 1

    spiel["inhalt"] = inhalt
    spiel["status"] = status
    spiel["game_over"] = falsch
    spiel["gewonnen"] = falsch
    spiel["aufgedeckt"] = 0
    spiel["flaggen"] = 0
    gib_zurück spiel

# === Feld aufdecken (rekursiv fuer 0er) ===
funktion aufdecken(spiel, row, col):
    wenn row < 0 oder row >= ROWS oder col < 0 oder col >= COLS:
        gib_zurück nichts
    setze idx auf row * COLS + col
    setze status_arr auf spiel["status"]
    wenn status_arr[idx] != 0:
        gib_zurück nichts

    setze status_arr[idx] auf 1
    spiel["aufgedeckt"] = spiel["aufgedeckt"] + 1

    setze inh auf spiel["inhalt"]
    wenn inh[idx] == -1:
        spiel["game_over"] = wahr
        gib_zurück nichts

    # Wenn 0 → Nachbarn aufdecken
    wenn inh[idx] == 0:
        setze dr auf -1
        solange dr <= 1:
            setze dc auf -1
            solange dc <= 1:
                wenn dr != 0 oder dc != 0:
                    aufdecken(spiel, row + dr, col + dc)
                setze dc auf dc + 1
            setze dr auf dr + 1

    # Gewonnen?
    wenn spiel["aufgedeckt"] == ROWS * COLS - MINEN:
        spiel["gewonnen"] = wahr

# === Flagge setzen/entfernen ===
funktion flagge_toggle(spiel, row, col):
    wenn row < 0 oder row >= ROWS oder col < 0 oder col >= COLS:
        gib_zurück nichts
    setze idx auf row * COLS + col
    setze status_arr auf spiel["status"]
    wenn status_arr[idx] == 0:
        setze status_arr[idx] auf 2
        spiel["flaggen"] = spiel["flaggen"] + 1
    sonst wenn status_arr[idx] == 2:
        setze status_arr[idx] auf 0
        spiel["flaggen"] = spiel["flaggen"] - 1

# === Zeichnen ===
funktion zeichne_zelle(win, spiel, row, col):
    setze idx auf row * COLS + col
    setze cx auf col * CELL
    setze cy auf HUD_H + row * CELL
    setze status_arr auf spiel["status"]
    setze inh auf spiel["inhalt"]

    wenn status_arr[idx] == 0:
        # Verdeckt
        zeichne_rechteck(win, cx, cy, CELL, CELL, "#A5D6A7")
        zeichne_rechteck(win, cx + 1, cy + 1, CELL - 2, CELL - 2, "#81C784")
        zeichne_linie(win, cx, cy, cx + CELL, cy, "#C8E6C9")
        zeichne_linie(win, cx, cy, cx, cy + CELL, "#C8E6C9")
    sonst wenn status_arr[idx] == 2:
        # Flagge
        zeichne_rechteck(win, cx, cy, CELL, CELL, "#A5D6A7")
        zeichne_rechteck(win, cx + 1, cy + 1, CELL - 2, CELL - 2, "#81C784")
        # Flaggen-Symbol
        zeichne_rechteck(win, cx + 16, cy + 8, 3, 22, "#5D4037")
        zeichne_rechteck(win, cx + 19, cy + 8, 12, 10, "#F44336")
    sonst:
        # Aufgedeckt
        zeichne_rechteck(win, cx, cy, CELL, CELL, "#E8E0D0")
        zeichne_rechteck(win, cx + 1, cy + 1, CELL - 2, CELL - 2, "#D7CFC0")

        wenn inh[idx] == -1:
            # Mine
            zeichne_kreis(win, cx + CELL / 2, cy + CELL / 2, 12, "#37474F")
            zeichne_kreis(win, cx + CELL / 2, cy + CELL / 2, 6, "#263238")
            # Strahlen
            zeichne_linie(win, cx + 8, cy + CELL / 2, cx + CELL - 8, cy + CELL / 2, "#37474F")
            zeichne_linie(win, cx + CELL / 2, cy + 8, cx + CELL / 2, cy + CELL - 8, "#37474F")
        sonst wenn inh[idx] > 0:
            # Zahl als farbiger Kreis mit Punkten
            setze farbe auf zahl_farbe(inh[idx])
            zeichne_kreis(win, cx + CELL / 2, cy + CELL / 2, 12, farbe)
            # Anzahl Punkte im Kreis
            setze dots auf inh[idx]
            wenn dots <= 4:
                setze di auf 0
                solange di < dots:
                    setze angle auf di * 6.28 / dots
                    setze dpx auf cx + CELL / 2 + sinus(angle) * 6
                    setze dpy auf cy + CELL / 2 + cosinus(angle) * 6
                    zeichne_kreis(win, dpx, dpy, 2, "#FFFFFF")
                    setze di auf di + 1
            sonst:
                zeichne_kreis(win, cx + CELL / 2, cy + CELL / 2, 8, farbe)
                zeichne_kreis(win, cx + CELL / 2, cy + CELL / 2, 4, "#FFFFFF")

funktion zeichne_spielfeld(win, spiel):
    setze row auf 0
    solange row < ROWS:
        setze col auf 0
        solange col < COLS:
            zeichne_zelle(win, spiel, row, col)
            setze col auf col + 1
        setze row auf row + 1

funktion zeichne_hud(win, spiel):
    zeichne_rechteck(win, 0, 0, WIN_W, HUD_H, "#1A1A2E")
    # Minen-Zaehler (links)
    setze rest auf MINEN - spiel["flaggen"]
    setze mi auf 0
    solange mi < rest und mi < 20:
        zeichne_kreis(win, 16 + mi * 16, 24, 5, "#F44336")
        setze mi auf mi + 1
    # Smiley (Mitte)
    zeichne_kreis(win, WIN_W / 2, 24, 14, "#FFD700")
    wenn spiel["game_over"]:
        # Traurig
        zeichne_kreis(win, WIN_W / 2 - 5, 20, 2, "#000000")
        zeichne_kreis(win, WIN_W / 2 + 5, 20, 2, "#000000")
        zeichne_linie(win, WIN_W / 2 - 6, 30, WIN_W / 2 + 6, 30, "#000000")
    sonst wenn spiel["gewonnen"]:
        # Sonnenbrille
        zeichne_rechteck(win, WIN_W / 2 - 9, 18, 8, 5, "#000000")
        zeichne_rechteck(win, WIN_W / 2 + 1, 18, 8, 5, "#000000")
        zeichne_linie(win, WIN_W / 2 - 6, 30, WIN_W / 2 + 6, 28, "#000000")
    sonst:
        # Normal
        zeichne_kreis(win, WIN_W / 2 - 5, 20, 2, "#000000")
        zeichne_kreis(win, WIN_W / 2 + 5, 20, 2, "#000000")
        zeichne_linie(win, WIN_W / 2 - 4, 28, WIN_W / 2 + 4, 28, "#000000")

# === Game Over: alle Minen zeigen ===
funktion zeige_alle_minen(spiel):
    setze inh auf spiel["inhalt"]
    setze status_arr auf spiel["status"]
    setze i auf 0
    solange i < ROWS * COLS:
        wenn inh[i] == -1:
            setze status_arr[i] auf 1
        setze i auf i + 1

# === Hauptprogramm ===
zeige "=== moo Minesweeper ==="
zeige "Klick=Aufdecken, F=Flagge, R=Neu, Escape=Beenden"

setze win auf fenster_erstelle("moo Minesweeper", WIN_W, WIN_H)
setze spiel auf neues_spiel()
setze klick_cooldown auf 0

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Neues Spiel
    wenn taste_gedrückt("r"):
        setze spiel auf neues_spiel()

    # Maus-Position → Grid
    setze mx auf maus_x(win)
    setze my auf maus_y(win)
    setze mcol auf boden(mx / CELL)
    setze mrow auf boden((my - HUD_H) / CELL)

    wenn klick_cooldown > 0:
        setze klick_cooldown auf klick_cooldown - 1

    # Mausklick → Aufdecken
    wenn maus_gedrückt(win) und klick_cooldown == 0:
        wenn spiel["game_over"] == falsch und spiel["gewonnen"] == falsch:
            wenn mrow >= 0 und mrow < ROWS und mcol >= 0 und mcol < COLS:
                aufdecken(spiel, mrow, mcol)
                wenn spiel["game_over"]:
                    zeige_alle_minen(spiel)
        setze klick_cooldown auf 10

    # F = Flagge
    wenn taste_gedrückt("f") und klick_cooldown == 0:
        wenn spiel["game_over"] == falsch und spiel["gewonnen"] == falsch:
            wenn mrow >= 0 und mrow < ROWS und mcol >= 0 und mcol < COLS:
                flagge_toggle(spiel, mrow, mcol)
        setze klick_cooldown auf 10

    # === Zeichnen ===
    fenster_löschen(win, "#1A1A2E")
    zeichne_hud(win, spiel)
    zeichne_spielfeld(win, spiel)

    # Hover-Highlight
    wenn mrow >= 0 und mrow < ROWS und mcol >= 0 und mcol < COLS:
        setze hx auf mcol * CELL
        setze hy auf HUD_H + mrow * CELL
        zeichne_rechteck(win, hx, hy, CELL, 2, "#FFFFFF")
        zeichne_rechteck(win, hx, hy, 2, CELL, "#FFFFFF")
        zeichne_rechteck(win, hx + CELL - 2, hy, 2, CELL, "#FFFFFF")
        zeichne_rechteck(win, hx, hy + CELL - 2, CELL, 2, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Minesweeper beendet."

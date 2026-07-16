# ============================================================
# memory.moo — Karten-Memory-Spiel
#
# Kompilieren: moo-compiler compile beispiele/memory.moo -o beispiele/memory
# Starten:     ./beispiele/memory
#
# Steuerung: Mausklick = Karte aufdecken, R = Neu, Escape = Beenden
# Ziel: Finde alle Paare mit moeglichst wenigen Versuchen
# ============================================================

konstante COLS auf 4
konstante ROWS auf 4
konstante PAARE auf 8
konstante CARD_W auf 100
konstante CARD_H auf 120
konstante GAP auf 12
konstante MARGIN_X auf 30
konstante MARGIN_Y auf 50
konstante WIN_W auf 478
konstante WIN_H auf 578

# Karten-Farben (8 Paare = 8 Farben)
funktion karten_farbe(typ):
    wenn typ == 0:
        gib_zurück "#F44336"
    wenn typ == 1:
        gib_zurück "#2196F3"
    wenn typ == 2:
        gib_zurück "#4CAF50"
    wenn typ == 3:
        gib_zurück "#FF9800"
    wenn typ == 4:
        gib_zurück "#9C27B0"
    wenn typ == 5:
        gib_zurück "#00BCD4"
    wenn typ == 6:
        gib_zurück "#FFEB3B"
    wenn typ == 7:
        gib_zurück "#E91E63"
    gib_zurück "#FFFFFF"

# Karten-Symbole (als geometrische Formen)
funktion zeichne_symbol(win, typ, cx, cy):
    setze farbe auf karten_farbe(typ)
    wenn typ == 0:
        # Herz (zwei Kreise + Dreieck-artig)
        zeichne_kreis(win, cx - 10, cy - 8, 14, farbe)
        zeichne_kreis(win, cx + 10, cy - 8, 14, farbe)
        zeichne_rechteck(win, cx - 20, cy - 4, 40, 20, farbe)
    wenn typ == 1:
        # Stern (Kreis mit Strahlen)
        zeichne_kreis(win, cx, cy, 18, farbe)
        zeichne_kreis(win, cx, cy, 10, "#BBDEFB")
    wenn typ == 2:
        # Quadrat
        zeichne_rechteck(win, cx - 18, cy - 18, 36, 36, farbe)
        zeichne_rechteck(win, cx - 10, cy - 10, 20, 20, "#C8E6C9")
    wenn typ == 3:
        # Diamant
        zeichne_rechteck(win, cx - 14, cy - 14, 28, 28, farbe)
        zeichne_kreis(win, cx, cy, 8, "#FFE0B2")
    wenn typ == 4:
        # Kreuz
        zeichne_rechteck(win, cx - 6, cy - 20, 12, 40, farbe)
        zeichne_rechteck(win, cx - 20, cy - 6, 40, 12, farbe)
    wenn typ == 5:
        # Ring
        zeichne_kreis(win, cx, cy, 20, farbe)
        zeichne_kreis(win, cx, cy, 12, "#E0F7FA")
    wenn typ == 6:
        # Sonne
        zeichne_kreis(win, cx, cy, 16, farbe)
        zeichne_linie(win, cx - 24, cy, cx + 24, cy, farbe)
        zeichne_linie(win, cx, cy - 24, cx, cy + 24, farbe)
        zeichne_linie(win, cx - 18, cy - 18, cx + 18, cy + 18, farbe)
        zeichne_linie(win, cx + 18, cy - 18, cx - 18, cy + 18, farbe)
    wenn typ == 7:
        # Mond
        zeichne_kreis(win, cx, cy, 18, farbe)
        zeichne_kreis(win, cx + 8, cy - 4, 14, "#1A1A2E")

# === Spielfeld ===
setze karten auf []
setze offen auf []
setze gefunden auf []
setze wahl1 auf -1
setze wahl2 auf -1
setze versuche auf 0
setze paare_gefunden auf 0
setze warte_timer auf 0
setze klick_cd auf 0

# PRNG
setze mem_seed auf 54321

funktion mem_zufall():
    setze mem_seed auf (mem_seed * 1103515245 + 12345) % 2147483648
    gib_zurück mem_seed

# === Karten mischen (Fisher-Yates) ===
funktion init_karten():
    setze karten auf []
    setze offen auf []
    setze gefunden auf []
    # 8 Paare = 16 Karten
    setze ki auf 0
    solange ki < PAARE:
        karten.hinzufügen(ki)
        karten.hinzufügen(ki)
        offen.hinzufügen(falsch)
        offen.hinzufügen(falsch)
        gefunden.hinzufügen(falsch)
        gefunden.hinzufügen(falsch)
        setze ki auf ki + 1
    # Fisher-Yates Shuffle
    setze idx auf länge(karten) - 1
    solange idx > 0:
        setze rand_idx auf mem_zufall() % (idx + 1)
        setze temp auf karten[idx]
        karten[idx] = karten[rand_idx]
        karten[rand_idx] = temp
        setze idx auf idx - 1
    setze wahl1 auf -1
    setze wahl2 auf -1
    setze versuche auf 0
    setze paare_gefunden auf 0
    setze warte_timer auf 0

# === Karte zeichnen ===
funktion zeichne_karte(win, idx, kx, ky):
    wenn gefunden[idx]:
        # Gefundenes Paar — leicht transparent
        zeichne_rechteck(win, kx, ky, CARD_W, CARD_H, "#2E7D32")
        zeichne_rechteck(win, kx + 3, ky + 3, CARD_W - 6, CARD_H - 6, "#388E3C")
        setze cx auf kx + CARD_W / 2
        setze cy auf ky + CARD_H / 2
        zeichne_symbol(win, karten[idx], cx, cy)
    sonst wenn offen[idx]:
        # Offene Karte
        zeichne_rechteck(win, kx, ky, CARD_W, CARD_H, "#FFFFFF")
        zeichne_rechteck(win, kx + 2, ky + 2, CARD_W - 4, CARD_H - 4, "#FAFAFA")
        setze cx auf kx + CARD_W / 2
        setze cy auf ky + CARD_H / 2
        zeichne_symbol(win, karten[idx], cx, cy)
    sonst:
        # Verdeckte Karte
        zeichne_rechteck(win, kx, ky, CARD_W, CARD_H, "#1565C0")
        zeichne_rechteck(win, kx + 3, ky + 3, CARD_W - 6, CARD_H - 6, "#1976D2")
        # Muster
        zeichne_linie(win, kx + 15, ky + 15, kx + CARD_W - 15, ky + CARD_H - 15, "#1E88E5")
        zeichne_linie(win, kx + CARD_W - 15, ky + 15, kx + 15, ky + CARD_H - 15, "#1E88E5")
        zeichne_kreis(win, kx + CARD_W / 2, ky + CARD_H / 2, 12, "#1E88E5")

# === HUD ===
funktion zeichne_mem_hud(win):
    zeichne_rechteck(win, 0, 0, WIN_W, 40, "#1A1A2E")
    # Versuche als Punkte
    setze vi auf 0
    solange vi < versuche und vi < 30:
        zeichne_kreis(win, 16 + vi * 10, 20, 3, "#FF9800")
        setze vi auf vi + 1
    # Gefundene Paare als gruene Kreise
    setze pi auf 0
    solange pi < paare_gefunden:
        zeichne_kreis(win, WIN_W - 16 - pi * 20, 20, 8, "#4CAF50")
        setze pi auf pi + 1

# === Hauptprogramm ===
zeige "=== moo Memory ==="
zeige "Klick = Karte aufdecken, R = Neu, Escape = Beenden"

setze win auf fenster_erstelle("moo Memory", WIN_W, WIN_H)
setze mem_seed auf zeit_ms() % 99991
init_karten()

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_gedrückt("r"):
        setze mem_seed auf zeit_ms() % 99991
        init_karten()

    # Warte-Timer (nach falschem Paar)
    wenn warte_timer > 0:
        setze warte_timer auf warte_timer - 1
        wenn warte_timer == 0:
            # Karten wieder zudecken
            wenn wahl1 >= 0:
                offen[wahl1] = falsch
            wenn wahl2 >= 0:
                offen[wahl2] = falsch
            setze wahl1 auf -1
            setze wahl2 auf -1

    # Klick-Cooldown
    wenn klick_cd > 0:
        setze klick_cd auf klick_cd - 1

    # Mausklick
    wenn maus_gedrückt(win) und klick_cd == 0 und warte_timer == 0:
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        # Welche Karte?
        setze card_col auf boden((mx - MARGIN_X) / (CARD_W + GAP))
        setze card_row auf boden((my - MARGIN_Y) / (CARD_H + GAP))
        wenn card_col >= 0 und card_col < COLS und card_row >= 0 und card_row < ROWS:
            setze card_idx auf card_row * COLS + card_col
            wenn card_idx >= 0 und card_idx < länge(karten):
                wenn offen[card_idx] == falsch und gefunden[card_idx] == falsch:
                    offen[card_idx] = wahr
                    wenn wahl1 == -1:
                        setze wahl1 auf card_idx
                    sonst wenn wahl2 == -1 und card_idx != wahl1:
                        setze wahl2 auf card_idx
                        setze versuche auf versuche + 1
                        # Paar pruefen
                        wenn karten[wahl1] == karten[wahl2]:
                            gefunden[wahl1] = wahr
                            gefunden[wahl2] = wahr
                            setze paare_gefunden auf paare_gefunden + 1
                            setze wahl1 auf -1
                            setze wahl2 auf -1
                        sonst:
                            setze warte_timer auf 30
        setze klick_cd auf 8

    # === Zeichnen ===
    fenster_löschen(win, "#263238")
    zeichne_mem_hud(win)

    setze row_idx auf 0
    solange row_idx < ROWS:
        setze col_idx auf 0
        solange col_idx < COLS:
            setze c_idx auf row_idx * COLS + col_idx
            setze kx auf MARGIN_X + col_idx * (CARD_W + GAP)
            setze ky auf MARGIN_Y + row_idx * (CARD_H + GAP)
            zeichne_karte(win, c_idx, kx, ky)
            setze col_idx auf col_idx + 1
        setze row_idx auf row_idx + 1

    # Gewonnen?
    wenn paare_gefunden == PAARE:
        zeichne_rechteck(win, WIN_W / 2 - 100, WIN_H / 2 - 25, 200, 50, "#4CAF50")
        zeichne_rechteck(win, WIN_W / 2 - 98, WIN_H / 2 - 23, 196, 46, "#66BB6A")
        # Stern
        zeichne_kreis(win, WIN_W / 2, WIN_H / 2, 14, "#FFD700")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Memory beendet. Versuche: " + text(versuche)

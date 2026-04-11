# ============================================================
# moo Sudoku — SDL2 GUI mit Backtracking-Solver & Generator
#
# Kompilieren: moo-compiler compile sudoku.moo -o sudoku
# Starten:     ./sudoku
#
# Bedienung (Maus-Only):
#   Klick auf Zelle      → Zelle auswaehlen
#   Klick auf Numpad 1-9 → Zahl in Zelle setzen
#   Klick auf X          → Zelle leeren
#   Klick auf "Loesen"   → Backtracking-Solver
#   Klick auf Leicht/    → Neues Puzzle (unterschiedliche Schwierigkeit)
#     Mittel/Schwer
#   Klick auf Speichern  → sudoku.json
#   Klick auf Laden      → sudoku.json
# ============================================================

konstante FENSTER_B auf 560
konstante FENSTER_H auf 780
konstante GRID_X auf 30
konstante GRID_Y auf 30
konstante ZELLE auf 50
konstante GRID_PX auf 450
konstante NUMPAD_Y auf 500
konstante BTN_Y auf 590
konstante BTN2_Y auf 660

# --- 3x5 Bitmap-Font fuer Ziffern 0-9 (15 Bits pro Ziffer, Zeilen oben->unten, Spalten links->rechts) ---
# Bit-Schreibweise: jede Zeile 3 Bit, 5 Zeilen = 15 Zellen. 1=gesetzt, 0=leer.
# Wir speichern pro Ziffer eine Liste aus 15 0/1-Werten.
setze FONT auf {}
FONT["0"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["1"] = [0,1,0, 1,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["2"] = [1,1,1, 0,0,1, 1,1,1, 1,0,0, 1,1,1]
FONT["3"] = [1,1,1, 0,0,1, 1,1,1, 0,0,1, 1,1,1]
FONT["4"] = [1,0,1, 1,0,1, 1,1,1, 0,0,1, 0,0,1]
FONT["5"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
FONT["6"] = [1,1,1, 1,0,0, 1,1,1, 1,0,1, 1,1,1]
FONT["7"] = [1,1,1, 0,0,1, 0,1,0, 0,1,0, 0,1,0]
FONT["8"] = [1,1,1, 1,0,1, 1,1,1, 1,0,1, 1,1,1]
FONT["9"] = [1,1,1, 1,0,1, 1,1,1, 0,0,1, 1,1,1]
FONT["X"] = [1,0,1, 0,1,0, 0,1,0, 0,1,0, 1,0,1]

# --- Mini-Buchstaben-Font fuer Beschriftungen (3x5, nur was gebraucht wird) ---
# Wir brauchen: L, O, E, S, N, I, C, H, T, M, R, P, A, D, B, Z
setze LETTER auf {}
LETTER["L"] = [1,0,0, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
LETTER["O"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
LETTER["E"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1]
LETTER["S"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
LETTER["N"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
LETTER["I"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 1,1,1]
LETTER["C"] = [1,1,1, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
LETTER["H"] = [1,0,1, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
LETTER["T"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,0]
LETTER["M"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
LETTER["R"] = [1,1,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1]
LETTER["P"] = [1,1,1, 1,0,1, 1,1,1, 1,0,0, 1,0,0]
LETTER["A"] = [0,1,0, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
LETTER["D"] = [1,1,0, 1,0,1, 1,0,1, 1,0,1, 1,1,0]
LETTER["B"] = [1,1,0, 1,0,1, 1,1,0, 1,0,1, 1,1,0]
LETTER["Z"] = [1,1,1, 0,0,1, 0,1,0, 1,0,0, 1,1,1]
LETTER["W"] = [1,0,1, 1,0,1, 1,1,1, 1,1,1, 1,0,1]

# Zeichnet ein 3x5 Bitmap an Position (x, y) mit Skalierung "px" pro Bit
funktion zeichne_bitmap(fenster, bits, x, y, px, farbe):
    setze zy auf 0
    solange zy < 5:
        setze zx auf 0
        solange zx < 3:
            setze idx auf zy * 3 + zx
            wenn bits[idx] == 1:
                zeichne_rechteck(fenster, x + zx * px, y + zy * px, px, px, farbe)
            setze zx auf zx + 1
        setze zy auf zy + 1

funktion zeichne_ziffer(fenster, ziffer, x, y, px, farbe):
    setze bits auf FONT[text(ziffer)]
    zeichne_bitmap(fenster, bits, x, y, px, farbe)

funktion zeichne_text(fenster, s, x, y, px, farbe):
    setze i auf 0
    setze cx auf x
    solange i < länge(s):
        setze ch auf s[i]
        wenn ch == " ":
            setze cx auf cx + 2 * px
        sonst:
            wenn LETTER.enthält(ch):
                zeichne_bitmap(fenster, LETTER[ch], cx, y, px, farbe)
            sonst:
                wenn FONT.enthält(ch):
                    zeichne_bitmap(fenster, FONT[ch], cx, y, px, farbe)
            setze cx auf cx + 4 * px
        setze i auf i + 1

# --- Sudoku-Logik ---

funktion leeres_grid():
    setze g auf []
    setze i auf 0
    solange i < 81:
        g.hinzufügen(0)
        setze i auf i + 1
    gib_zurück g

funktion kopiere(g):
    setze k auf []
    setze i auf 0
    solange i < 81:
        k.hinzufügen(g[i])
        setze i auf i + 1
    gib_zurück k

funktion gueltig(g, r, c, n):
    # Zeile
    setze i auf 0
    solange i < 9:
        wenn g[r * 9 + i] == n:
            gib_zurück falsch
        setze i auf i + 1
    # Spalte
    setze i auf 0
    solange i < 9:
        wenn g[i * 9 + c] == n:
            gib_zurück falsch
        setze i auf i + 1
    # 3x3 Box
    setze br auf boden(r / 3) * 3
    setze bc auf boden(c / 3) * 3
    setze dr auf 0
    solange dr < 3:
        setze dc auf 0
        solange dc < 3:
            wenn g[(br + dr) * 9 + (bc + dc)] == n:
                gib_zurück falsch
            setze dc auf dc + 1
        setze dr auf dr + 1
    gib_zurück wahr

# Backtracking-Solver. Veraendert g in place. Gibt wahr bei Erfolg.
funktion loese_rek(g, pos):
    wenn pos >= 81:
        gib_zurück wahr
    wenn g[pos] != 0:
        gib_zurück loese_rek(g, pos + 1)
    setze r auf boden(pos / 9)
    setze c auf pos - r * 9
    setze n auf 1
    solange n <= 9:
        wenn gueltig(g, r, c, n):
            g[pos] = n
            wenn loese_rek(g, pos + 1):
                gib_zurück wahr
            g[pos] = 0
        setze n auf n + 1
    gib_zurück falsch

funktion loese(g):
    gib_zurück loese_rek(g, 0)

# Generiert ein vollstaendiges gueltiges Grid (gefuellte Loesung) per randomisiertem Backtracking
funktion fuellen_rek(g, pos):
    wenn pos >= 81:
        gib_zurück wahr
    setze r auf boden(pos / 9)
    setze c auf pos - r * 9
    # zufaellige Reihenfolge 1..9
    setze nums auf [1, 2, 3, 4, 5, 6, 7, 8, 9]
    setze i auf 8
    solange i > 0:
        setze j auf boden(zufall() * (i + 1))
        setze tmp auf nums[i]
        nums[i] = nums[j]
        nums[j] = tmp
        setze i auf i - 1
    setze k auf 0
    solange k < 9:
        setze n auf nums[k]
        wenn gueltig(g, r, c, n):
            g[pos] = n
            wenn fuellen_rek(g, pos + 1):
                gib_zurück wahr
            g[pos] = 0
        setze k auf k + 1
    gib_zurück falsch

funktion fuellen():
    setze g auf leeres_grid()
    fuellen_rek(g, 0)
    gib_zurück g

# Entfernt "entfernen" Zellen zufaellig aus einem gefuellten Grid (erstellt Puzzle)
funktion puzzle_aus(solved, entfernen):
    setze g auf kopiere(solved)
    setze raus auf 0
    setze versuche auf 0
    solange raus < entfernen und versuche < 400:
        setze pos auf boden(zufall() * 81)
        wenn g[pos] != 0:
            g[pos] = 0
            setze raus auf raus + 1
        setze versuche auf versuche + 1
    gib_zurück g

funktion markiere_vorgaben(g):
    setze v auf []
    setze i auf 0
    solange i < 81:
        v.hinzufügen(g[i] != 0)
        setze i auf i + 1
    gib_zurück v

# --- Zeichnen ---

funktion zelle_hintergrund(fenster, r, c, selektion):
    setze x auf GRID_X + c * ZELLE
    setze y auf GRID_Y + r * ZELLE
    setze bg auf "weiss"
    wenn selektion == r * 9 + c:
        setze bg auf "gelb"
    zeichne_rechteck(fenster, x + 1, y + 1, ZELLE - 2, ZELLE - 2, bg)

funktion zeichne_grid_linien(fenster):
    setze i auf 0
    solange i <= 9:
        setze dick auf 1
        wenn i % 3 == 0:
            setze dick auf 3
        setze j auf 0
        solange j < dick:
            zeichne_linie(fenster, GRID_X + i * ZELLE + j, GRID_Y, GRID_X + i * ZELLE + j, GRID_Y + GRID_PX, "schwarz")
            zeichne_linie(fenster, GRID_X, GRID_Y + i * ZELLE + j, GRID_X + GRID_PX, GRID_Y + i * ZELLE + j, "schwarz")
            setze j auf j + 1
        setze i auf i + 1

funktion zeichne_zellen(fenster, grid, vorgaben, selektion):
    setze r auf 0
    solange r < 9:
        setze c auf 0
        solange c < 9:
            zelle_hintergrund(fenster, r, c, selektion)
            setze wert auf grid[r * 9 + c]
            wenn wert != 0:
                setze zx auf GRID_X + c * ZELLE + 16
                setze zy auf GRID_Y + r * ZELLE + 10
                setze farbe auf "schwarz"
                wenn vorgaben[r * 9 + c]:
                    setze farbe auf "blau"
                zeichne_ziffer(fenster, wert, zx, zy, 6, farbe)
            setze c auf c + 1
        setze r auf r + 1

funktion zeichne_numpad(fenster):
    setze n auf 1
    solange n <= 9:
        setze x auf GRID_X + (n - 1) * ZELLE
        setze y auf NUMPAD_Y
        zeichne_rechteck(fenster, x, y, ZELLE - 2, ZELLE - 2, "grau")
        zeichne_ziffer(fenster, n, x + 16, y + 10, 6, "schwarz")
        setze n auf n + 1

funktion zeichne_knopf(fenster, x, y, b, h, beschriftung, farbe_bg, farbe_fg):
    zeichne_rechteck(fenster, x, y, b, h, farbe_bg)
    zeichne_rechteck(fenster, x + 2, y + 2, b - 4, h - 4, farbe_bg)
    zeichne_text(fenster, beschriftung, x + 8, y + 12, 3, farbe_fg)

funktion zeichne_knoepfe(fenster):
    zeichne_knopf(fenster, GRID_X, BTN_Y, 90, 40, "LOESEN", "gruen", "weiss")
    zeichne_knopf(fenster, GRID_X + 100, BTN_Y, 90, 40, "LEICHT", "blau", "weiss")
    zeichne_knopf(fenster, GRID_X + 200, BTN_Y, 90, 40, "MITTEL", "blau", "weiss")
    zeichne_knopf(fenster, GRID_X + 300, BTN_Y, 90, 40, "SCHWER", "blau", "weiss")
    zeichne_knopf(fenster, GRID_X + 400, BTN_Y, 50, 40, "X", "rot", "weiss")
    zeichne_knopf(fenster, GRID_X, BTN2_Y, 140, 40, "SPEICHERN", "grau", "schwarz")
    zeichne_knopf(fenster, GRID_X + 150, BTN2_Y, 140, 40, "LADEN", "grau", "schwarz")

# --- Maus-Helfer ---
funktion im_rect(mx, my, x, y, b, h):
    gib_zurück mx >= x und mx < x + b und my >= y und my < y + h

# --- JSON Save/Load ---
funktion speichere_json(grid, vorgaben):
    setze d auf {}
    d["grid"] = grid
    setze v_nums auf []
    setze i auf 0
    solange i < 81:
        wenn vorgaben[i]:
            v_nums.hinzufügen(1)
        sonst:
            v_nums.hinzufügen(0)
        setze i auf i + 1
    d["vorgaben"] = v_nums
    setze s auf json_string(d)
    datei_schreiben("sudoku.json", s)

funktion lade_json():
    wenn nicht datei_existiert("sudoku.json"):
        gib_zurück nichts
    setze s auf datei_lesen("sudoku.json")
    setze d auf json_parse(s)
    setze erg auf {}
    erg["grid"] = d["grid"]
    setze v auf []
    setze vnums auf d["vorgaben"]
    setze i auf 0
    solange i < 81:
        v.hinzufügen(vnums[i] == 1)
        setze i auf i + 1
    erg["vorgaben"] = v
    gib_zurück erg

# --- Hauptschleife ---

zeige "=== moo Sudoku ==="
zeige "Generiere leichtes Puzzle..."

setze solved auf fuellen()
setze grid auf puzzle_aus(solved, 40)
setze vorgaben auf markiere_vorgaben(grid)
setze selektion auf -1
setze vorher_maus auf falsch

setze fenster auf fenster_erstelle("moo Sudoku", FENSTER_B, FENSTER_H)

solange fenster_offen(fenster):
    fenster_löschen(fenster, "schwarz")

    # Rahmen um Grid
    zeichne_rechteck(fenster, GRID_X - 2, GRID_Y - 2, GRID_PX + 4, GRID_PX + 4, "weiss")
    zeichne_zellen(fenster, grid, vorgaben, selektion)
    zeichne_grid_linien(fenster)

    zeichne_numpad(fenster)
    zeichne_knoepfe(fenster)

    fenster_aktualisieren(fenster)

    # Maus-Input (Edge-Detection)
    setze mx auf maus_x(fenster)
    setze my auf maus_y(fenster)
    setze jetzt auf maus_gedrückt(fenster)

    wenn jetzt und nicht vorher_maus:
        # Grid-Klick
        wenn im_rect(mx, my, GRID_X, GRID_Y, GRID_PX, GRID_PX):
            setze cc auf boden((mx - GRID_X) / ZELLE)
            setze rr auf boden((my - GRID_Y) / ZELLE)
            setze selektion auf rr * 9 + cc

        # Numpad-Klick
        setze n auf 1
        solange n <= 9:
            setze nx auf GRID_X + (n - 1) * ZELLE
            wenn im_rect(mx, my, nx, NUMPAD_Y, ZELLE - 2, ZELLE - 2):
                wenn selektion >= 0 und nicht vorgaben[selektion]:
                    grid[selektion] = n
            setze n auf n + 1

        # Loesen
        wenn im_rect(mx, my, GRID_X, BTN_Y, 90, 40):
            loese(grid)

        # Leicht / Mittel / Schwer
        wenn im_rect(mx, my, GRID_X + 100, BTN_Y, 90, 40):
            setze solved auf fuellen()
            setze grid auf puzzle_aus(solved, 35)
            setze vorgaben auf markiere_vorgaben(grid)
            setze selektion auf -1
        wenn im_rect(mx, my, GRID_X + 200, BTN_Y, 90, 40):
            setze solved auf fuellen()
            setze grid auf puzzle_aus(solved, 45)
            setze vorgaben auf markiere_vorgaben(grid)
            setze selektion auf -1
        wenn im_rect(mx, my, GRID_X + 300, BTN_Y, 90, 40):
            setze solved auf fuellen()
            setze grid auf puzzle_aus(solved, 55)
            setze vorgaben auf markiere_vorgaben(grid)
            setze selektion auf -1

        # Loeschen (X)
        wenn im_rect(mx, my, GRID_X + 400, BTN_Y, 50, 40):
            wenn selektion >= 0 und nicht vorgaben[selektion]:
                grid[selektion] = 0

        # Speichern
        wenn im_rect(mx, my, GRID_X, BTN2_Y, 140, 40):
            speichere_json(grid, vorgaben)

        # Laden
        wenn im_rect(mx, my, GRID_X + 150, BTN2_Y, 140, 40):
            setze geladen auf lade_json()
            wenn geladen != nichts:
                setze grid auf geladen["grid"]
                setze vorgaben auf geladen["vorgaben"]
                setze selektion auf -1

    setze vorher_maus auf jetzt
    warte(16)

zeige "Sudoku beendet"

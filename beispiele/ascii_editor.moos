# ============================================================
# moo ASCII-Art-Editor — 80x25 Zeichen-Grid mit SDL2
#
# Kompilieren: moo-compiler compile ascii_editor.moo -o ascii_editor
# Starten:     ./ascii_editor
#
# Bedienung:
#   Linke Maustaste in Grid  → Zelle mit aktuellem Zeichen+Farbe malen
#   Dragging                 → pinseln
#   Klick auf Zeichen-Palette → aktuelles Zeichen waehlen
#   Klick auf Farb-Palette    → aktuelle Farbe waehlen
#   Klick auf SAVE            → art.txt schreiben
#   Klick auf LOAD            → art.txt einlesen
#   Klick auf CLEAR           → Grid leeren
# ============================================================

konstante COLS auf 80
konstante ROWS auf 25
konstante ZELLE_B auf 10
konstante ZELLE_H auf 14
konstante GRID_X auf 10
konstante GRID_Y auf 10
konstante GRID_PX_B auf 800
konstante GRID_PX_H auf 350
konstante TOOLBAR_Y auf 370
konstante CHAR_PAL_Y auf 405
konstante COLOR_PAL_Y auf 440
konstante FENSTER_B auf 820
konstante FENSTER_H auf 480

# --- 3x5 Bitmap-Font (Ziffern + Buchstaben + Sonderzeichen) ---
setze FONT auf {}
FONT[" "] = [0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0]
FONT["."] = [0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,1,0]
FONT[":"] = [0,0,0, 0,1,0, 0,0,0, 0,1,0, 0,0,0]
FONT[";"] = [0,0,0, 0,1,0, 0,0,0, 0,1,0, 1,0,0]
FONT["-"] = [0,0,0, 0,0,0, 1,1,1, 0,0,0, 0,0,0]
FONT["+"] = [0,0,0, 0,1,0, 1,1,1, 0,1,0, 0,0,0]
FONT["*"] = [0,0,0, 1,0,1, 0,1,0, 1,0,1, 0,0,0]
FONT["#"] = [1,0,1, 1,1,1, 1,0,1, 1,1,1, 1,0,1]
FONT["%"] = [1,0,1, 0,0,1, 0,1,0, 1,0,0, 1,0,1]
FONT["@"] = [1,1,1, 1,0,1, 1,1,1, 1,0,0, 1,1,1]
FONT["$"] = [0,1,1, 1,1,0, 0,1,0, 0,1,1, 1,1,0]
FONT["^"] = [0,1,0, 1,0,1, 0,0,0, 0,0,0, 0,0,0]
FONT["~"] = [0,0,0, 1,0,1, 1,1,1, 1,0,1, 0,0,0]
FONT["/"] = [0,0,1, 0,0,1, 0,1,0, 1,0,0, 1,0,0]
FONT["\\"] = [1,0,0, 1,0,0, 0,1,0, 0,0,1, 0,0,1]
FONT["|"] = [0,1,0, 0,1,0, 0,1,0, 0,1,0, 0,1,0]
FONT["="] = [0,0,0, 1,1,1, 0,0,0, 1,1,1, 0,0,0]
FONT["<"] = [0,0,1, 0,1,0, 1,0,0, 0,1,0, 0,0,1]
FONT[">"] = [1,0,0, 0,1,0, 0,0,1, 0,1,0, 1,0,0]
FONT["?"] = [1,1,1, 0,0,1, 0,1,0, 0,0,0, 0,1,0]
FONT["!"] = [0,1,0, 0,1,0, 0,1,0, 0,0,0, 0,1,0]
FONT["("] = [0,0,1, 0,1,0, 0,1,0, 0,1,0, 0,0,1]
FONT[")"] = [1,0,0, 0,1,0, 0,1,0, 0,1,0, 1,0,0]
FONT["["] = [0,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,1]
FONT["]"] = [1,1,0, 0,1,0, 0,1,0, 0,1,0, 1,1,0]
FONT["{"] = [0,1,1, 0,1,0, 1,1,0, 0,1,0, 0,1,1]
FONT["}"] = [1,1,0, 0,1,0, 0,1,1, 0,1,0, 1,1,0]
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
FONT["A"] = [0,1,0, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
FONT["B"] = [1,1,0, 1,0,1, 1,1,0, 1,0,1, 1,1,0]
FONT["C"] = [1,1,1, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["D"] = [1,1,0, 1,0,1, 1,0,1, 1,0,1, 1,1,0]
FONT["E"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1]
FONT["F"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,0,0]
FONT["G"] = [1,1,1, 1,0,0, 1,0,1, 1,0,1, 1,1,1]
FONT["H"] = [1,0,1, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
FONT["I"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["J"] = [1,1,1, 0,0,1, 0,0,1, 1,0,1, 1,1,1]
FONT["K"] = [1,0,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1]
FONT["L"] = [1,0,0, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["M"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT["N"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT["O"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["P"] = [1,1,1, 1,0,1, 1,1,1, 1,0,0, 1,0,0]
FONT["Q"] = [1,1,1, 1,0,1, 1,0,1, 1,1,0, 1,1,1]
FONT["R"] = [1,1,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1]
FONT["S"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
FONT["T"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,0]
FONT["U"] = [1,0,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["V"] = [1,0,1, 1,0,1, 1,0,1, 1,0,1, 0,1,0]
FONT["W"] = [1,0,1, 1,0,1, 1,1,1, 1,1,1, 1,0,1]
FONT["X"] = [1,0,1, 1,0,1, 0,1,0, 1,0,1, 1,0,1]
FONT["Y"] = [1,0,1, 1,0,1, 0,1,0, 0,1,0, 0,1,0]
FONT["Z"] = [1,1,1, 0,0,1, 0,1,0, 1,0,0, 1,1,1]

# Palette-Zeichen (16) und Farben (16 — vom Parse-Color unterstuetzt)
setze PAL_CHARS auf [" ", ".", ":", ";", "-", "+", "*", "#", "%", "@", "$", "O", "X", "^", "~", "/"]
setze PAL_COLORS auf ["weiss", "schwarz", "grau", "rot", "gruen", "blau", "gelb", "magenta", "orange", "cyan", "lila", "braun", "rosa", "dunkelgrau", "hellgruen", "hellblau"]

funktion zeichne_char(fenster, bits, x, y, px, farbe):
    setze zy auf 0
    solange zy < 5:
        setze zx auf 0
        solange zx < 3:
            wenn bits[zy * 3 + zx] == 1:
                zeichne_rechteck(fenster, x + zx * px, y + zy * px, px, px, farbe)
            setze zx auf zx + 1
        setze zy auf zy + 1

funktion zeichne_text(fenster, s, x, y, px, farbe):
    setze i auf 0
    setze cx auf x
    solange i < länge(s):
        setze ch auf s[i]
        wenn FONT.enthält(ch):
            zeichne_char(fenster, FONT[ch], cx, y, px, farbe)
        setze cx auf cx + 4 * px
        setze i auf i + 1

# --- Grid-State: flache Liste mit 80*25 Eintraegen.
# Jede Zelle ist ein Dict {"char": "...", "farbe": "..."}
funktion leeres_grid():
    setze g auf []
    setze i auf 0
    solange i < COLS * ROWS:
        g.hinzufügen({"char": " ", "farbe": "weiss"})
        setze i auf i + 1
    gib_zurück g

funktion zelle_setzen(grid, r, c, ch, farbe):
    setze idx auf r * COLS + c
    grid[idx] = {"char": ch, "farbe": farbe}

funktion im_rect(mx, my, x, y, b, h):
    gib_zurück mx >= x und mx < x + b und my >= y und my < y + h

# --- Grid zeichnen ---
funktion zeichne_grid(fenster, grid):
    # Hintergrund
    zeichne_rechteck(fenster, GRID_X - 1, GRID_Y - 1, GRID_PX_B + 2, GRID_PX_H + 2, "schwarz")
    setze r auf 0
    solange r < ROWS:
        setze c auf 0
        solange c < COLS:
            setze z auf grid[r * COLS + c]
            setze ch auf z["char"]
            wenn ch != " " und FONT.enthält(ch):
                setze px auf GRID_X + c * ZELLE_B + 2
                setze py auf GRID_Y + r * ZELLE_H + 2
                zeichne_char(fenster, FONT[ch], px, py, 2, z["farbe"])
            setze c auf c + 1
        setze r auf r + 1

# --- Paletten ---
funktion zeichne_char_palette(fenster, aktueller_char):
    setze i auf 0
    solange i < 16:
        setze x auf 10 + i * 35
        setze y auf CHAR_PAL_Y
        setze bg auf "grau"
        wenn PAL_CHARS[i] == aktueller_char:
            setze bg auf "gelb"
        zeichne_rechteck(fenster, x, y, 30, 30, bg)
        setze ch auf PAL_CHARS[i]
        wenn FONT.enthält(ch):
            zeichne_char(fenster, FONT[ch], x + 10, y + 8, 3, "schwarz")
        setze i auf i + 1

funktion zeichne_color_palette(fenster, aktuelle_farbe):
    setze i auf 0
    solange i < 16:
        setze x auf 10 + i * 35
        setze y auf COLOR_PAL_Y
        zeichne_rechteck(fenster, x, y, 30, 30, PAL_COLORS[i])
        wenn PAL_COLORS[i] == aktuelle_farbe:
            zeichne_rechteck(fenster, x, y, 30, 3, "weiss")
            zeichne_rechteck(fenster, x, y + 27, 30, 3, "weiss")
            zeichne_rechteck(fenster, x, y, 3, 30, "weiss")
            zeichne_rechteck(fenster, x + 27, y, 3, 30, "weiss")
        setze i auf i + 1

# --- Toolbar ---
funktion zeichne_knopf(fenster, x, y, b, h, label, bg, vg):
    zeichne_rechteck(fenster, x, y, b, h, bg)
    zeichne_text(fenster, label, x + 10, y + 8, 3, vg)

funktion zeichne_toolbar(fenster, aktueller_char, aktuelle_farbe):
    zeichne_knopf(fenster, 10, TOOLBAR_Y, 80, 28, "SAVE", "gruen", "weiss")
    zeichne_knopf(fenster, 100, TOOLBAR_Y, 80, 28, "LOAD", "blau", "weiss")
    zeichne_knopf(fenster, 190, TOOLBAR_Y, 100, 28, "CLEAR", "rot", "weiss")
    # Aktuelle Auswahl-Anzeige
    zeichne_text(fenster, "CHAR:", 310, TOOLBAR_Y + 8, 3, "weiss")
    zeichne_rechteck(fenster, 380, TOOLBAR_Y, 28, 28, "schwarz")
    wenn FONT.enthält(aktueller_char):
        zeichne_char(fenster, FONT[aktueller_char], 388, TOOLBAR_Y + 6, 3, aktuelle_farbe)
    zeichne_text(fenster, "COLOR:", 420, TOOLBAR_Y + 8, 3, "weiss")
    zeichne_rechteck(fenster, 510, TOOLBAR_Y, 28, 28, aktuelle_farbe)

# --- Save / Load (.txt, farbe geht verloren) ---
funktion speichere_txt(grid):
    setze zeilen auf []
    setze r auf 0
    solange r < ROWS:
        setze zeile auf ""
        setze c auf 0
        solange c < COLS:
            setze zeile auf zeile + grid[r * COLS + c]["char"]
            setze c auf c + 1
        zeilen.hinzufügen(zeile)
        setze r auf r + 1
    setze inhalt auf zeilen.verbinden("\n")
    datei_schreiben("art.txt", inhalt + "\n")

funktion lade_txt():
    wenn nicht datei_existiert("art.txt"):
        gib_zurück nichts
    setze inhalt auf datei_lesen("art.txt")
    setze g auf leeres_grid()
    setze r auf 0
    setze c auf 0
    setze i auf 0
    solange i < länge(inhalt) und r < ROWS:
        setze ch auf inhalt[i]
        wenn ch == "\n":
            setze r auf r + 1
            setze c auf 0
        sonst:
            wenn c < COLS:
                g[r * COLS + c] = {"char": ch, "farbe": "weiss"}
                setze c auf c + 1
        setze i auf i + 1
    gib_zurück g

# --- Hauptschleife ---

zeige "=== moo ASCII-Art-Editor ==="
zeige "80x25 Zeichen-Grid, 16 Zeichen + 16 Farben"

setze grid auf leeres_grid()
setze aktueller_char auf "#"
setze aktuelle_farbe auf "hellgruen"
setze vorher_maus auf falsch

setze fenster auf fenster_erstelle("moo ASCII-Editor", FENSTER_B, FENSTER_H)

solange fenster_offen(fenster):
    setze mx auf maus_x(fenster)
    setze my auf maus_y(fenster)
    setze jetzt auf maus_gedrückt(fenster)

    # Dragging im Grid — Pinsel bei jeder Bewegung
    wenn jetzt und im_rect(mx, my, GRID_X, GRID_Y, GRID_PX_B, GRID_PX_H):
        setze cc auf boden((mx - GRID_X) / ZELLE_B)
        setze rr auf boden((my - GRID_Y) / ZELLE_H)
        wenn cc >= 0 und cc < COLS und rr >= 0 und rr < ROWS:
            zelle_setzen(grid, rr, cc, aktueller_char, aktuelle_farbe)

    # Einmalige Klicks auf Paletten/Toolbar (Edge)
    wenn jetzt und nicht vorher_maus:
        # Char-Palette
        setze i auf 0
        solange i < 16:
            wenn im_rect(mx, my, 10 + i * 35, CHAR_PAL_Y, 30, 30):
                setze aktueller_char auf PAL_CHARS[i]
            setze i auf i + 1
        # Color-Palette
        setze i auf 0
        solange i < 16:
            wenn im_rect(mx, my, 10 + i * 35, COLOR_PAL_Y, 30, 30):
                setze aktuelle_farbe auf PAL_COLORS[i]
            setze i auf i + 1
        # Toolbar
        wenn im_rect(mx, my, 10, TOOLBAR_Y, 80, 28):
            speichere_txt(grid)
        wenn im_rect(mx, my, 100, TOOLBAR_Y, 80, 28):
            setze geladen auf lade_txt()
            wenn geladen != nichts:
                setze grid auf geladen
        wenn im_rect(mx, my, 190, TOOLBAR_Y, 100, 28):
            setze grid auf leeres_grid()

    setze vorher_maus auf jetzt

    # --- Render ---
    fenster_löschen(fenster, "dunkelgrau")
    zeichne_grid(fenster, grid)
    zeichne_toolbar(fenster, aktueller_char, aktuelle_farbe)
    zeichne_char_palette(fenster, aktueller_char)
    zeichne_color_palette(fenster, aktuelle_farbe)
    fenster_aktualisieren(fenster)

zeige "ASCII-Editor beendet"

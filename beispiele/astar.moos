# ============================================================
# moo A* Pathfinding mit SDL2-GUI
#
# Steuerung:
#   W        - Wall-Modus (Mausklick setzt Wand)
#   S        - Start-Modus (Mausklick setzt Start)
#   E        - End-Modus (Mausklick setzt Ziel)
#   C        - Clear-Modus (Mausklick loescht Zelle)
#   Space    - Ein Schritt vorwaerts
#   Enter    - Komplett durchrechnen
#   R        - Zuruecksetzen
#   Tab      - Heuristik Manhattan <-> Euklid
#   Escape   - Beenden
# ============================================================

konstante GRID_W auf 40
konstante GRID_H auf 30
konstante ZELLE auf 20
konstante FENSTER_B auf 800
konstante FENSTER_H auf 640     # 600 fuer grid + 40 fuer statusbar

konstante ZELL_LEER auf 0
konstante ZELL_WAND auf 1
konstante ZELL_START auf 2
konstante ZELL_ZIEL auf 3

konstante MODE_WAND auf 0
konstante MODE_START auf 1
konstante MODE_ZIEL auf 2
konstante MODE_LOESCHEN auf 3

konstante HEUR_MANHATTAN auf 0
konstante HEUR_EUKLID auf 1

# ------------------------------------------------------------
# Grid-Zustand
# ------------------------------------------------------------

funktion leeres_grid():
    setze g auf []
    setze i auf 0
    solange i < GRID_W * GRID_H:
        g.hinzufügen(ZELL_LEER)
        setze i auf i + 1
    gib_zurück g

funktion get_zelle(grid, x, y):
    gib_zurück grid[y * GRID_W + x]

funktion set_zelle(grid, x, y, wert):
    grid[y * GRID_W + x] = wert

funktion in_grid(x, y):
    wenn x < 0 oder x >= GRID_W:
        gib_zurück falsch
    wenn y < 0 oder y >= GRID_H:
        gib_zurück falsch
    gib_zurück wahr

# ------------------------------------------------------------
# Heuristik
# ------------------------------------------------------------

funktion abs_int(n):
    wenn n < 0:
        gib_zurück 0 - n
    gib_zurück n

funktion manhattan(x1, y1, x2, y2):
    gib_zurück abs_int(x1 - x2) + abs_int(y1 - y2)

funktion euklid(x1, y1, x2, y2):
    setze dx auf x1 - x2
    setze dy auf y1 - y2
    setze d auf sqrt(dx * dx + dy * dy)
    # Skaliere auf aehnliche Groessenordnung wie Manhattan
    gib_zurück boden(d * 10) / 10

# ------------------------------------------------------------
# Priority Queue (sorted insert, min am Ende für O(1) pop)
# ------------------------------------------------------------

funktion pq_neu():
    gib_zurück []

funktion pq_einfuegen(pq, f, x, y):
    setze eintrag auf {}
    eintrag["f"] = f
    eintrag["x"] = x
    eintrag["y"] = y
    # Sortiert einfuegen (absteigend → min ist am Ende)
    setze i auf 0
    setze eingefuegt auf falsch
    solange i < länge(pq) und eingefuegt == falsch:
        wenn pq[i]["f"] < f:
            # Einfuegen vor i
            setze np auf []
            setze j auf 0
            solange j < i:
                np.hinzufügen(pq[j])
                setze j auf j + 1
            np.hinzufügen(eintrag)
            setze j auf i
            solange j < länge(pq):
                np.hinzufügen(pq[j])
                setze j auf j + 1
            gib_zurück np
        setze i auf i + 1
    # Am Ende anhaengen
    setze kopie auf []
    setze j auf 0
    solange j < länge(pq):
        kopie.hinzufügen(pq[j])
        setze j auf j + 1
    kopie.hinzufügen(eintrag)
    gib_zurück kopie

funktion pq_pop_min(pq):
    wenn länge(pq) == 0:
        gib_zurück nichts
    setze letzter auf pq[länge(pq) - 1]
    setze np auf []
    setze i auf 0
    solange i < länge(pq) - 1:
        np.hinzufügen(pq[i])
        setze i auf i + 1
    setze r auf {}
    r["pq"] = np
    r["eintrag"] = letzter
    gib_zurück r

# ------------------------------------------------------------
# A* Suche (inkrementell)
# ------------------------------------------------------------

klasse AStar:
    funktion erstelle(grid, sx, sy, zx, zy, heur):
        selbst.grid = grid
        selbst.sx = sx
        selbst.sy = sy
        selbst.zx = zx
        selbst.zy = zy
        selbst.heur = heur
        selbst.pq = pq_neu()
        # score als dicts "x,y" → zahl
        selbst.g_score = {}
        selbst.f_score = {}
        selbst.came_from = {}
        selbst.closed = {}
        selbst.open_marker = {}
        setze key auf text(sx) + "," + text(sy)
        selbst.g_score[key] = 0
        setze h auf selbst.h_wert(sx, sy)
        selbst.f_score[key] = h
        setze selbst.pq auf pq_einfuegen(selbst.pq, h, sx, sy)
        selbst.open_marker[key] = wahr
        selbst.fertig = falsch
        selbst.gefunden = falsch
        selbst.path = []
        selbst.expanded = 0

    funktion h_wert(x, y):
        wenn selbst.heur == HEUR_MANHATTAN:
            gib_zurück manhattan(x, y, selbst.zx, selbst.zy)
        gib_zurück euklid(x, y, selbst.zx, selbst.zy)

    funktion rekonstruiere():
        setze path auf []
        setze cur auf text(selbst.zx) + "," + text(selbst.zy)
        setze start_key auf text(selbst.sx) + "," + text(selbst.sy)
        solange cur != start_key:
            wenn selbst.came_from.hat(cur) == falsch:
                gib_zurück nichts
            path.hinzufügen(cur)
            setze cur auf selbst.came_from[cur]
        path.hinzufügen(start_key)
        selbst.path = path

    funktion schritt():
        wenn selbst.fertig:
            gib_zurück nichts
        setze res auf pq_pop_min(selbst.pq)
        wenn res == nichts:
            selbst.fertig = wahr
            gib_zurück nichts
        selbst.pq = res["pq"]
        setze e auf res["eintrag"]
        setze cx auf e["x"]
        setze cy auf e["y"]
        setze key auf text(cx) + "," + text(cy)
        wenn selbst.closed.hat(key):
            gib_zurück nichts
        selbst.closed[key] = wahr
        selbst.expanded = selbst.expanded + 1
        wenn cx == selbst.zx und cy == selbst.zy:
            selbst.fertig = wahr
            selbst.gefunden = wahr
            selbst.rekonstruiere()
            gib_zurück nichts
        # Nachbarn (4-directional)
        setze dxs auf [1, 0 - 1, 0, 0]
        setze dys auf [0, 0, 1, 0 - 1]
        setze i auf 0
        solange i < 4:
            setze nx auf cx + dxs[i]
            setze ny auf cy + dys[i]
            setze i auf i + 1
            wenn in_grid(nx, ny):
                wenn get_zelle(selbst.grid, nx, ny) != ZELL_WAND:
                    setze nkey auf text(nx) + "," + text(ny)
                    wenn selbst.closed.hat(nkey) == falsch:
                        setze tentative auf selbst.g_score[key] + 1
                        setze alt auf 99999999
                        wenn selbst.g_score.hat(nkey):
                            setze alt auf selbst.g_score[nkey]
                        wenn tentative < alt:
                            selbst.came_from[nkey] = key
                            selbst.g_score[nkey] = tentative
                            setze f auf tentative + selbst.h_wert(nx, ny)
                            selbst.f_score[nkey] = f
                            setze selbst.pq auf pq_einfuegen(selbst.pq, f, nx, ny)
                            selbst.open_marker[nkey] = wahr

    funktion komplett_loesen():
        solange selbst.fertig == falsch:
            selbst.schritt()

# ------------------------------------------------------------
# Mini-Font fuer Statusbar (3x5 Bitmap)
# ------------------------------------------------------------

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
FONT["A"] = [0,1,0, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
FONT["B"] = [1,1,0, 1,0,1, 1,1,0, 1,0,1, 1,1,0]
FONT["C"] = [1,1,1, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["D"] = [1,1,0, 1,0,1, 1,0,1, 1,0,1, 1,1,0]
FONT["E"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1]
FONT["F"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,0,0]
FONT["G"] = [1,1,1, 1,0,0, 1,0,1, 1,0,1, 1,1,1]
FONT["H"] = [1,0,1, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
FONT["I"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["K"] = [1,0,1, 1,1,0, 1,0,0, 1,1,0, 1,0,1]
FONT["L"] = [1,0,0, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["M"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT["N"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT["O"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["P"] = [1,1,1, 1,0,1, 1,1,1, 1,0,0, 1,0,0]
FONT["R"] = [1,1,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1]
FONT["S"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
FONT["T"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,0]
FONT["U"] = [1,0,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["W"] = [1,0,1, 1,0,1, 1,1,1, 1,1,1, 1,0,1]
FONT["X"] = [1,0,1, 1,0,1, 0,1,0, 1,0,1, 1,0,1]
FONT["Y"] = [1,0,1, 1,0,1, 0,1,0, 0,1,0, 0,1,0]
FONT["Z"] = [1,1,1, 0,0,1, 0,1,0, 1,0,0, 1,1,1]
FONT[":"] = [0,0,0, 0,1,0, 0,0,0, 0,1,0, 0,0,0]
FONT["/"] = [0,0,1, 0,0,1, 0,1,0, 1,0,0, 1,0,0]
FONT["="] = [0,0,0, 1,1,1, 0,0,0, 1,1,1, 0,0,0]
FONT[" "] = [0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0]
FONT["-"] = [0,0,0, 0,0,0, 1,1,1, 0,0,0, 0,0,0]

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

funktion zeichne_text(fenster, s, x, y, px, farbe):
    setze cx auf x
    setze i auf 0
    solange i < länge(s):
        setze ch auf s[i].gross()
        wenn FONT.hat(ch):
            zeichne_bitmap(fenster, FONT[ch], cx, y, px, farbe)
        setze cx auf cx + 4 * px
        setze i auf i + 1

# ------------------------------------------------------------
# Rendering
# ------------------------------------------------------------

funktion zeichne_grid(fenster, grid, astar):
    setze y auf 0
    solange y < GRID_H:
        setze x auf 0
        solange x < GRID_W:
            setze px auf x * ZELLE
            setze py auf y * ZELLE
            setze z auf get_zelle(grid, x, y)
            setze farbe auf "schwarz"
            wenn z == ZELL_LEER:
                setze farbe auf "weiss"
            sonst wenn z == ZELL_WAND:
                setze farbe auf "grau"
            sonst wenn z == ZELL_START:
                setze farbe auf "gruen"
            sonst wenn z == ZELL_ZIEL:
                setze farbe auf "rot"
            # Overlay von A*
            wenn astar != nichts und z == ZELL_LEER:
                setze key auf text(x) + "," + text(y)
                wenn astar.closed.hat(key):
                    setze farbe auf "grau"
                sonst wenn astar.open_marker.hat(key):
                    setze farbe auf "blau"
            zeichne_rechteck(fenster, px, py, ZELLE - 1, ZELLE - 1, farbe)
            setze x auf x + 1
        setze y auf y + 1

funktion zeichne_path(fenster, astar):
    wenn astar == nichts:
        gib_zurück nichts
    setze i auf 0
    solange i < länge(astar.path):
        setze teile auf astar.path[i].teilen(",")
        setze px auf 0
        setze j auf 0
        solange j < länge(teile[0]):
            setze px auf px * 10 + (bytes_zu_liste(teile[0][j])[0] - 48)
            setze j auf j + 1
        setze py auf 0
        setze j auf 0
        solange j < länge(teile[1]):
            setze py auf py * 10 + (bytes_zu_liste(teile[1][j])[0] - 48)
            setze j auf j + 1
        # Ueberschreibe mit gelb (ausser start/ziel)
        wenn (px != astar.sx oder py != astar.sy) und (px != astar.zx oder py != astar.zy):
            zeichne_rechteck(fenster, px * ZELLE, py * ZELLE, ZELLE - 1, ZELLE - 1, "gelb")
        setze i auf i + 1

funktion zeichne_statusbar(fenster, modus, heur, astar):
    zeichne_rechteck(fenster, 0, GRID_H * ZELLE, FENSTER_B, FENSTER_H - GRID_H * ZELLE, "schwarz")
    setze s auf "MODE "
    wenn modus == MODE_WAND:
        setze s auf s + "WALL"
    sonst wenn modus == MODE_START:
        setze s auf s + "START"
    sonst wenn modus == MODE_ZIEL:
        setze s auf s + "END"
    sonst wenn modus == MODE_LOESCHEN:
        setze s auf s + "CLEAR"
    setze s auf s + "  H "
    wenn heur == HEUR_MANHATTAN:
        setze s auf s + "MANHATTAN"
    sonst:
        setze s auf s + "EUKLID"
    wenn astar != nichts:
        setze s auf s + "  EXP " + text(astar.expanded)
        wenn astar.gefunden:
            setze s auf s + "  PATH " + text(länge(astar.path))
    zeichne_text(fenster, s, 10, GRID_H * ZELLE + 10, 3, "weiss")

# ------------------------------------------------------------
# Main Loop
# ------------------------------------------------------------

setze grid auf leeres_grid()
# Default Start/Ziel
setze sx auf 5
setze sy auf 15
setze zx auf 34
setze zy auf 15
set_zelle(grid, sx, sy, ZELL_START)
set_zelle(grid, zx, zy, ZELL_ZIEL)

setze modus auf MODE_WAND
setze heur auf HEUR_MANHATTAN
setze astar auf nichts
setze vorher_maus auf falsch
setze vorher_space auf falsch
setze vorher_enter auf falsch
setze vorher_tab auf falsch
setze vorher_r auf falsch
setze vorher_w auf falsch
setze vorher_s auf falsch
setze vorher_e auf falsch
setze vorher_c auf falsch

setze fenster auf fenster_erstelle("moo A* Pathfinding", FENSTER_B, FENSTER_H)

solange fenster_offen(fenster):
    fenster_löschen(fenster, "schwarz")
    zeichne_grid(fenster, grid, astar)
    wenn astar != nichts und astar.gefunden:
        zeichne_path(fenster, astar)
    zeichne_statusbar(fenster, modus, heur, astar)
    fenster_aktualisieren(fenster)

    # Keys
    setze jetzt_space auf key_pressed("space")
    wenn jetzt_space und vorher_space == falsch:
        wenn astar == nichts:
            setze astar auf neu AStar(grid, sx, sy, zx, zy, heur)
        wenn astar.fertig == falsch:
            astar.schritt()
    setze vorher_space auf jetzt_space

    setze jetzt_enter auf key_pressed("enter")
    wenn jetzt_enter und vorher_enter == falsch:
        wenn astar == nichts:
            setze astar auf neu AStar(grid, sx, sy, zx, zy, heur)
        astar.komplett_loesen()
    setze vorher_enter auf jetzt_enter

    setze jetzt_tab auf key_pressed("tab")
    wenn jetzt_tab und vorher_tab == falsch:
        wenn heur == HEUR_MANHATTAN:
            setze heur auf HEUR_EUKLID
        sonst:
            setze heur auf HEUR_MANHATTAN
    setze vorher_tab auf jetzt_tab

    setze jetzt_r auf key_pressed("r")
    wenn jetzt_r und vorher_r == falsch:
        setze astar auf nichts
    setze vorher_r auf jetzt_r

    wenn key_pressed("w") und vorher_w == falsch:
        setze modus auf MODE_WAND
    setze vorher_w auf key_pressed("w")

    wenn key_pressed("s") und vorher_s == falsch:
        setze modus auf MODE_START
    setze vorher_s auf key_pressed("s")

    wenn key_pressed("e") und vorher_e == falsch:
        setze modus auf MODE_ZIEL
    setze vorher_e auf key_pressed("e")

    wenn key_pressed("c") und vorher_c == falsch:
        setze modus auf MODE_LOESCHEN
    setze vorher_c auf key_pressed("c")

    wenn key_pressed("escape"):
        fenster_schliessen(fenster)

    # Maus (Edge-Detection)
    setze mx auf maus_x(fenster)
    setze my auf maus_y(fenster)
    setze jetzt_maus auf maus_gedrückt(fenster)
    wenn jetzt_maus und my < GRID_H * ZELLE:
        setze gx auf boden(mx / ZELLE)
        setze gy auf boden(my / ZELLE)
        wenn in_grid(gx, gy):
            wenn modus == MODE_WAND:
                wenn get_zelle(grid, gx, gy) == ZELL_LEER:
                    set_zelle(grid, gx, gy, ZELL_WAND)
            sonst wenn modus == MODE_START:
                set_zelle(grid, sx, sy, ZELL_LEER)
                setze sx auf gx
                setze sy auf gy
                set_zelle(grid, sx, sy, ZELL_START)
                setze astar auf nichts
            sonst wenn modus == MODE_ZIEL:
                set_zelle(grid, zx, zy, ZELL_LEER)
                setze zx auf gx
                setze zy auf gy
                set_zelle(grid, zx, zy, ZELL_ZIEL)
                setze astar auf nichts
            sonst wenn modus == MODE_LOESCHEN:
                wenn get_zelle(grid, gx, gy) == ZELL_WAND:
                    set_zelle(grid, gx, gy, ZELL_LEER)
    setze vorher_maus auf jetzt_maus

    warte(16)

zeige "A* beendet"

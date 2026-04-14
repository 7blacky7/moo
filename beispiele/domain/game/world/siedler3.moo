# ============================================================
# Siedler 3 — Isometrische 2.5D-Variante (moo)
#
# Kompilieren: moo-compiler compile beispiele/domain/game/world/siedler3.moo \
#              -o beispiele/domain/game/world/siedler3
# Starten:     ./beispiele/domain/game/world/siedler3
#
# Steuerung: WASD = Kamera pan, Escape = Beenden
# Renderer:  Diamond-Iso (2:1), Perlin-ish Terrain mit Hoehenstufen,
#            4 Gebaeude, 2 Resource-Ketten (Holz→Bretter, Stein).
# ============================================================

konstante TILE_W auf 64
konstante TILE_H auf 32
konstante HEIGHT_STEP auf 16
konstante WORLD auf 15
konstante WIN_W auf 1024
konstante WIN_H auf 640

# ------------------------------------------------------------
# Seed-basierte Terrain-Hoehe (fbm-Approximation via Sinus-Mix)
# ------------------------------------------------------------
funktion hash01(x, y):
    setze n auf (x * 73856093) ^ (y * 19349663)
    setze n auf n & 2147483647
    gib_zurück (n % 1000) / 1000.0

funktion terrain_z(x, y):
    setze v auf sinus(x * 0.35) * 1.2 + cosinus(y * 0.45) * 1.1
    setze v auf v + sinus((x + y) * 0.25) * 0.8
    setze v auf v + hash01(x, y) * 0.6
    setze z auf boden(v + 2.5)
    wenn z < 0:
        setze z auf 0
    wenn z > 4:
        setze z auf 4
    gib_zurück z

funktion biom_farbe(z):
    wenn z <= 0:
        gib_zurück "#2060A0"
    wenn z == 1:
        gib_zurück "#D0C078"
    wenn z == 2:
        gib_zurück "#4CAF50"
    wenn z == 3:
        gib_zurück "#6E5B3E"
    gib_zurück "#9E9E9E"

# ------------------------------------------------------------
# Isometrische Projektion
# ------------------------------------------------------------
funktion iso_x(wx, wy, cam_x):
    gib_zurück (wx - wy) * (TILE_W / 2) + cam_x

funktion iso_y(wx, wy, wz, cam_y):
    gib_zurück (wx + wy) * (TILE_H / 2) - wz * HEIGHT_STEP + cam_y

# Zeichnet einen Diamond-Tile als gefuelltes Viereck via 2 Dreiecke-Approx
# (moo hat kein "fill_polygon" — simulieren mit vielen horizontalen Linien).
funktion zeichne_diamond(win, sx, sy, farbe, rand):
    setze i auf 0
    setze hh auf TILE_H / 2
    solange i < TILE_H:
        setze v auf i - hh
        setze breite auf TILE_W - abs(v) * 2
        wenn breite > 0:
            zeichne_linie(win, sx - breite / 2, sy + v, sx + breite / 2, sy + v, farbe)
        setze i auf i + 1
    # Rand oben links / oben rechts / unten links / unten rechts
    zeichne_linie(win, sx, sy - hh, sx + TILE_W / 2, sy, rand)
    zeichne_linie(win, sx + TILE_W / 2, sy, sx, sy + hh, rand)
    zeichne_linie(win, sx, sy + hh, sx - TILE_W / 2, sy, rand)
    zeichne_linie(win, sx - TILE_W / 2, sy, sx, sy - hh, rand)

# Hoehen-Waende (Seitenflaechen unter dem Tile) als Rechtecke
funktion zeichne_waende(win, sx, sy, z, farbe_seitl):
    wenn z <= 0:
        gib_zurück nichts
    setze hoehe auf z * HEIGHT_STEP
    # links: Parallelogramm approximiert durch Rechteck + zwei Kanten
    zeichne_rechteck(win, sx - TILE_W / 2, sy, TILE_W / 2, hoehe, farbe_seitl)

# ------------------------------------------------------------
# Gebaeude (statisch platziert)
# ------------------------------------------------------------
# typ: 0=Holzfaeller, 1=Saegewerk, 2=Steinmetz, 3=Haus
funktion gebaeude_farbe(typ):
    wenn typ == 0:
        gib_zurück "#2E7D32"
    wenn typ == 1:
        gib_zurück "#8D6E63"
    wenn typ == 2:
        gib_zurück "#607D8B"
    wenn typ == 3:
        gib_zurück "#C62828"
    wenn typ == 4:
        gib_zurück "#F9A825"
    wenn typ == 5:
        gib_zurück "#D7CCC8"
    gib_zurück "#FF8F00"

funktion gebaeude_name(typ):
    wenn typ == 0:
        gib_zurück "Holzfaeller"
    wenn typ == 1:
        gib_zurück "Saegewerk"
    wenn typ == 2:
        gib_zurück "Steinmetz"
    gib_zurück "Haus"

funktion zeichne_gebaeude(win, sx, sy, typ):
    setze f auf gebaeude_farbe(typ)
    # Basis-Rechteck (als Wuerfel approximiert)
    zeichne_rechteck(win, sx - 14, sy - 30, 28, 24, f)
    # Dach-Dreieck-Approx: Linien
    zeichne_linie(win, sx - 16, sy - 30, sx, sy - 46, "#222222")
    zeichne_linie(win, sx, sy - 46, sx + 16, sy - 30, "#222222")
    zeichne_linie(win, sx - 16, sy - 30, sx + 16, sy - 30, "#222222")
    # Tuer
    zeichne_rechteck(win, sx - 3, sy - 14, 6, 8, "#3E2723")

# ------------------------------------------------------------
# Spieler-Position + Gebaeude-Liste (k3 Follow-up: 3 neue Typen 4-6)
# Jedes Gebaeude: [wx, wy, typ]
# typ: 0=Holzfaeller, 1=Saegewerk, 2=Steinmetz, 3=Haus, 4=Bauer, 5=Muehle, 6=Baecker
# ------------------------------------------------------------
setze gebaeude auf [[3, 4, 0], [4, 5, 1], [8, 3, 2], [6, 8, 3], [10, 10, 0], [11, 11, 1], [2, 10, 2], [9, 6, 3], [5, 11, 4], [7, 12, 5], [9, 13, 6], [12, 8, 3]]

# Resource-Counter (als 1-Element-Listen, damit Funktions-Zuweisung persistiert)
setze r_holz auf [0]
setze r_bretter auf [0]
setze r_stein auf [0]
setze r_korn auf [0]
setze r_mehl auf [0]
setze r_brot auf [0]

# Produktions-Tick: produziere + wandle um (k3 Follow-up: erweiterte Ketten)
funktion tick_wirtschaft():
    setze i auf 0
    solange i < länge(gebaeude):
        setze typ auf gebaeude[i][2]
        wenn typ == 0:
            r_holz[0] = r_holz[0] + 1
        wenn typ == 2:
            r_stein[0] = r_stein[0] + 1
        wenn typ == 4:
            r_korn[0] = r_korn[0] + 1
        setze i auf i + 1
    # Saegewerk: 2 Holz -> 1 Brett. Muehle: 2 Korn -> 1 Mehl. Baecker: 1 Mehl -> 1 Brot.
    setze j auf 0
    solange j < länge(gebaeude):
        setze t auf gebaeude[j][2]
        wenn t == 1 und r_holz[0] >= 2:
            r_holz[0] = r_holz[0] - 2
            r_bretter[0] = r_bretter[0] + 1
        wenn t == 5 und r_korn[0] >= 2:
            r_korn[0] = r_korn[0] - 2
            r_mehl[0] = r_mehl[0] + 1
        wenn t == 6 und r_mehl[0] >= 1:
            r_mehl[0] = r_mehl[0] - 1
            r_brot[0] = r_brot[0] + 1
        setze j auf j + 1

# ------------------------------------------------------------
# k3 Follow-up: Strassen-Routing (BFS auf passierbaren Tiles z<=2)
# ------------------------------------------------------------
funktion ist_passierbar(x, y):
    wenn x < 0 oder y < 0 oder x >= WORLD oder y >= WORLD:
        gib_zurück falsch
    gib_zurück terrain_z(x, y) <= 2

# BFS-Routing: gibt eine Liste [tx, ty]-Tiles zurueck, leer wenn unerreichbar.
# Begrenzt auf 200 Schritte um Endlosschleife auszuschliessen.
funktion finde_route(sx, sy, zx, zy):
    wenn nicht ist_passierbar(sx, sy) oder nicht ist_passierbar(zx, zy):
        gib_zurück []
    setze besucht auf {}
    setze queue auf [[sx, sy, []]]
    setze schritte auf 0
    solange länge(queue) > 0 und schritte < 200:
        setze kopf auf queue[0]
        setze queue auf queue.teilstring(1, länge(queue))
        setze cx auf kopf[0]
        setze cy auf kopf[1]
        setze pfad auf kopf[2]
        wenn cx == zx und cy == zy:
            gib_zurück pfad
        setze key auf text(cx) + "," + text(cy)
        wenn besucht.hat(key):
            setze schritte auf schritte + 1
            weiter
        besucht[key] = wahr
        setze nachbarn auf [[cx + 1, cy], [cx - 1, cy], [cx, cy + 1], [cx, cy - 1]]
        für n in nachbarn:
            wenn ist_passierbar(n[0], n[1]):
                setze np auf pfad + [[n[0], n[1]]]
                setze queue auf queue + [[n[0], n[1], np]]
        setze schritte auf schritte + 1
    gib_zurück []

# ------------------------------------------------------------
# k3 Follow-up: Partikel-System (Rauch, Staub, Funken)
# Jedes Partikel: [sx, sy, vx, vy, leben, farbe]
# ------------------------------------------------------------
setze partikel auf [[]]
partikel[0] = []

funktion partikel_neu(sx, sy, vx, vy, leben, farbe):
    partikel[0] = partikel[0] + [[sx, sy, vx, vy, leben, farbe]]

funktion partikel_tick():
    setze frisch auf []
    für p in partikel[0]:
        setze leben auf p[4] - 1
        wenn leben > 0:
            setze frisch auf frisch + [[p[0] + p[2], p[1] + p[3], p[2], p[3] * 0.95, leben, p[5]]]
    partikel[0] = frisch

funktion partikel_render(win):
    für p in partikel[0]:
        setze groesse auf 1 + p[4] / 20
        zeichne_kreis(win, p[0], p[1], groesse, p[5])

# Schornstein-Rauch fuer aktive Produzenten (Saegewerk, Muehle, Baecker)
funktion emit_rauch(cam_x, cam_y):
    für g in gebaeude:
        setze t auf g[2]
        wenn t == 1 oder t == 5 oder t == 6:
            setze gz auf terrain_z(g[0], g[1])
            setze sx auf iso_x(g[0], g[1], cam_x)
            setze sy auf iso_y(g[0], g[1], gz, cam_y) - 46
            partikel_neu(sx + (hash01(g[0], g[1]) - 0.5) * 4, sy, (hash01(g[0] + 1, g[1]) - 0.5) * 1.0, -0.8, 40, "#9E9E9E")

# HUD: Resource-Counter oben links
funktion zeichne_hud(win):
    zeichne_rechteck(win, 10, 10, 260, 60, "#101820")
    zeichne_rechteck(win, 15, 15, 20, 20, "#4CAF50")
    zeichne_rechteck(win, 95, 15, 20, 20, "#8D6E63")
    zeichne_rechteck(win, 175, 15, 20, 20, "#607D8B")
    # Ziffern-Balken-Approx (da kein zeichne_text): Laenge = Anzahl
    zeichne_rechteck(win, 40, 20, r_holz[0] * 2 + 1, 10, "#FFFFFF")
    zeichne_rechteck(win, 120, 20, r_bretter[0] * 2 + 1, 10, "#FFFFFF")
    zeichne_rechteck(win, 200, 20, r_stein[0] * 2 + 1, 10, "#FFFFFF")
    # Label-Streifen: 3 kleine farbige Pillen als Legende
    zeichne_rechteck(win, 15, 45, 60, 8, "#2E7D32")
    zeichne_rechteck(win, 95, 45, 60, 8, "#A1887F")
    zeichne_rechteck(win, 175, 45, 60, 8, "#455A64")

# ------------------------------------------------------------
# Haupt-Schleife
# ------------------------------------------------------------
setze win auf fenster_erstelle("Siedler 3 — Isometrisch (moo)", WIN_W, WIN_H)

setze cam_x auf WIN_W / 2
setze cam_y auf 80
setze cam_speed auf 8
setze tick_counter auf 0

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    wenn taste_gedrückt("w"):
        setze cam_y auf cam_y + cam_speed
    wenn taste_gedrückt("s"):
        setze cam_y auf cam_y - cam_speed
    wenn taste_gedrückt("a"):
        setze cam_x auf cam_x + cam_speed
    wenn taste_gedrückt("d"):
        setze cam_x auf cam_x - cam_speed

    # Wirtschafts-Tick alle 60 Frames (~1 Sekunde bei 60 FPS)
    setze tick_counter auf tick_counter + 1
    wenn tick_counter >= 60:
        tick_wirtschaft()
        setze tick_counter auf 0
    # k3 Follow-up: Partikel jedes Frame, Schornstein-Rauch alle 8 Frames
    partikel_tick()
    wenn tick_counter % 8 == 0:
        emit_rauch(cam_x, cam_y)

    fenster_löschen(win, "#1A1A2E")

    # Terrain in Render-Order (hinten nach vorne): kleines x+y zuerst
    # Dadurch kommen hoehere Tiles vorne drueber.
    setze s auf 0
    solange s <= 2 * WORLD - 2:
        setze x auf 0
        solange x < WORLD:
            setze y auf s - x
            wenn y >= 0 und y < WORLD:
                setze z auf terrain_z(x, y)
                setze sx auf iso_x(x, y, cam_x)
                setze sy auf iso_y(x, y, z, cam_y)
                zeichne_waende(win, sx, sy, z, "#3A2A1A")
                zeichne_diamond(win, sx, sy, biom_farbe(z), "#101010")
            setze x auf x + 1
        setze s auf s + 1

    # Gebaeude-Pass (nach Terrain, damit sie oben liegen)
    setze b auf 0
    solange b < länge(gebaeude):
        setze gx auf gebaeude[b][0]
        setze gy auf gebaeude[b][1]
        setze gz auf terrain_z(gx, gy)
        setze sxg auf iso_x(gx, gy, cam_x)
        setze syg auf iso_y(gx, gy, gz, cam_y)
        zeichne_gebaeude(win, sxg, syg, gebaeude[b][2])
        setze b auf b + 1

    # Siedler-Dots (bewegen sich im Kreis um Gebaeude 3 = Haus)
    setze zentrum_x auf iso_x(6, 8, cam_x)
    setze zentrum_y auf iso_y(6, 8, terrain_z(6, 8), cam_y)
    setze t auf tick_counter * 6.0
    setze dx auf cosinus(t * 0.05) * 30
    setze dy auf sinus(t * 0.05) * 15
    zeichne_kreis(win, zentrum_x + dx, zentrum_y + dy - 10, 4, "#FFD54F")

    # k3 Follow-up: Partikel ueber alles (nach Gebaeude, vor HUD)
    partikel_render(win)

    zeichne_hud(win)
    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)

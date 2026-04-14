# ============================================================
# Siedler 3 — Echte 3D-Szene (raum_*-API) mit Wind + Atmosphaere
#
# Kompilieren: moo-compiler compile beispiele/domain/game/world/siedler3.moo \
#              -o beispiele/domain/game/world/siedler3
# Starten:     ./beispiele/domain/game/world/siedler3
#
# Steuerung: WASD = Kamera-Umlauf, Q/E = Zoom, Escape = Beenden
#
# Architektur (M1-Skelett, k4):
#  - raum_*-API (3D-Fenster, iso-aehnliche Schraeg-Kamera mit Umlauf)
#  - Wind-System (globaler 2D-Vektor + Staerke, sinus-animiert)
#  - Atmosphaere: Schornstein-Rauch, Funken, Gras-Sway, Baum-Schaukeln, Flaggen
#  - Volumetrische 3D-Siedler-Figuren (Wuerfel-Stack mit Animation)
#  - HOOKS fuer Wirtschaftslogik (M2 von k3): siehe Abschnitt "WIRTSCHAFTS-HOOKS"
# ============================================================

konstante WORLD auf 14
konstante WIN_W auf 1024
konstante WIN_H auf 640

# Iso-Projektion-Konstanten (Pixel-Space fuer zeichne_rechteck_z)
konstante TILE_W auf 48
konstante TILE_H auf 24
konstante ISO_HEIGHT auf 8
konstante ISO_OX auf 512
konstante ISO_OY auf 160

# Iso-Projektion-Helper (Pixel-Koord, Y um Hoehe versetzt)
funktion iso_x(wx, wy):
    gib_zurück ISO_OX + (wx - wy) * (TILE_W / 2)

funktion iso_y(wx, wy, h):
    gib_zurück ISO_OY + (wx + wy) * (TILE_H / 2) - h * ISO_HEIGHT

# Iso-Painter-Z (weiter weg + niedriger = hinten)
funktion iso_z(wx, wy, h):
    gib_zurück 60.0 + (wx + wy) * 0.5 - h * 2.0

# -----------------------------------------------------------------
# Noise-Approx fuer Terrain-Hoehe
# -----------------------------------------------------------------
funktion hash01(x, y):
    setze n auf (x * 73856093) ^ (y * 19349663)
    setze n auf n & 2147483647
    gib_zurück (n % 1000) / 1000.0

funktion terrain_h(x, y):
    setze v auf sinus(x * 0.35) * 1.2 + cosinus(y * 0.45) * 1.1
    setze v auf v + sinus((x + y) * 0.25) * 0.8 + hash01(x, y) * 0.6
    setze h auf boden(v + 2.5)
    wenn h < 0:
        setze h auf 0
    wenn h > 4:
        setze h auf 4
    gib_zurück h

funktion biom_farbe(h):
    wenn h <= 0:
        gib_zurück "blau"
    wenn h == 1:
        gib_zurück "gelb"
    wenn h == 2:
        gib_zurück "gruen"
    wenn h == 3:
        gib_zurück "orange"
    gib_zurück "grau"

# -----------------------------------------------------------------
# Gebaeude-Daten (Position + Typ)
# typ: 0=Haus (Senke), 1=Holzfaeller, 2=Saegewerk, 3=Steinmetz,
#      4=Bauer, 5=Muehle, 6=Baecker
# Datenmodell wird von k3 in M2 fuer Wirtschaftslogik konsumiert.
# -----------------------------------------------------------------
setze gebaeude auf [[3, 4, 1], [4, 5, 2], [8, 3, 3], [6, 8, 0], [10, 10, 1], [5, 11, 4], [7, 12, 5], [9, 13, 6], [12, 8, 0]]

funktion gebaeude_farbe(typ):
    wenn typ == 0:
        gib_zurück "rot"
    wenn typ == 1:
        gib_zurück "gruen"
    wenn typ == 2:
        gib_zurück "orange"
    wenn typ == 3:
        gib_zurück "grau"
    wenn typ == 4:
        gib_zurück "gelb"
    wenn typ == 5:
        gib_zurück "weiss"
    gib_zurück "magenta"

funktion hat_schornstein(typ):
    # Saegewerk, Muehle, Baecker qualmen
    wenn typ == 2 oder typ == 5 oder typ == 6:
        gib_zurück wahr
    gib_zurück falsch

# -----------------------------------------------------------------
# WIRTSCHAFTS-HOOKS (k3 fuellt in M2)
# -----------------------------------------------------------------
# Resource-Counter als 1-Element-Listen, damit Funktions-Zuweisung persistiert.
# Schon im Skelett angelegt, damit M2 sie nicht initialisieren muss.
setze r_holz auf [0]
setze r_bretter auf [0]
setze r_stein auf [0]
setze r_korn auf [0]
setze r_mehl auf [0]
setze r_brot auf [0]

# HOOK 1: tick_wirtschaft()
# Wird alle 60 Frames aufgerufen. Soll:
#  - Holzfaeller (typ 1) → r_holz[0] += 1
#  - Steinmetz (typ 3) → r_stein[0] += 1
#  - Bauer (typ 4) → r_korn[0] += 1
#  - Saegewerk (typ 2) → 2 Holz → 1 Brett
#  - Muehle (typ 5) → 2 Korn → 1 Mehl
#  - Baecker (typ 6) → 1 Mehl → 1 Brot
# k3 portiert hier die v1-Logik (commit e96e0b5).
funktion tick_wirtschaft():
    # k3-M2: 3 Resource-Ketten (Holz/Bretter/Stein, Korn/Mehl/Brot).
    setze i auf 0
    solange i < länge(gebaeude):
        setze typ auf gebaeude[i][2]
        wenn typ == 1:
            r_holz[0] = r_holz[0] + 1
        wenn typ == 3:
            r_stein[0] = r_stein[0] + 1
        wenn typ == 4:
            r_korn[0] = r_korn[0] + 1
        setze i auf i + 1
    setze j auf 0
    solange j < länge(gebaeude):
        setze t auf gebaeude[j][2]
        wenn t == 2 und r_holz[0] >= 2:
            r_holz[0] = r_holz[0] - 2
            r_bretter[0] = r_bretter[0] + 1
        wenn t == 5 und r_korn[0] >= 2:
            r_korn[0] = r_korn[0] - 2
            r_mehl[0] = r_mehl[0] + 1
        wenn t == 6 und r_mehl[0] >= 1:
            r_mehl[0] = r_mehl[0] - 1
            r_brot[0] = r_brot[0] + 1
        setze j auf j + 1

# HOOK 2: gebaeude_state(idx)
# Liefert ein State-Dict pro Gebaeude (z.B. {"aktiv": wahr, "produziert": "holz"}).
# Wird von M3 (k3) gebraucht fuer Pfad-Walking + Render-Visualisierung.
funktion gebaeude_state(idx):
    setze g auf gebaeude[idx]
    setze typ auf g[2]
    setze produziert auf "nichts"
    wenn typ == 1:
        setze produziert auf "holz"
    wenn typ == 2:
        setze produziert auf "bretter"
    wenn typ == 3:
        setze produziert auf "stein"
    wenn typ == 4:
        setze produziert auf "korn"
    wenn typ == 5:
        setze produziert auf "mehl"
    wenn typ == 6:
        setze produziert auf "brot"
    gib_zurück {"x": g[0], "y": g[1], "typ": typ, "produziert": produziert, "aktiv": wahr}

# HOOK 3: pfade()
# BFS-Routing zwischen passierbaren Tiles (z<=2). Liefert Liste von Routen-Listen
# zwischen Produktions-Paaren. k3 portiert hier die v1-BFS-Implementierung.
funktion ist_passierbar(x, y):
    wenn x < 0 oder y < 0 oder x >= WORLD oder y >= WORLD:
        gib_zurück falsch
    gib_zurück terrain_h(x, y) <= 2

# BFS auf passierbaren Tiles. 200-Schritte-Limit.
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

# pfade(): berechnet Routen zwischen Produzenten- und Verbraucher-Paaren.
# Liefert Liste von Routen-Listen (jede Route ist eine Liste von [x,y]-Tiles).
funktion pfade():
    setze result auf []
    setze i auf 0
    solange i < länge(gebaeude):
        setze ti auf gebaeude[i][2]
        # Holzfaeller -> Saegewerk, Korn -> Muehle, Mehl -> Baecker, Brot -> Haus.
        wenn ti == 1 oder ti == 4 oder ti == 5 oder ti == 6:
            setze ziel_typ auf 0
            wenn ti == 1:
                setze ziel_typ auf 2
            wenn ti == 4:
                setze ziel_typ auf 5
            wenn ti == 5:
                setze ziel_typ auf 6
            wenn ti == 6:
                setze ziel_typ auf 0
            setze j auf 0
            solange j < länge(gebaeude):
                wenn gebaeude[j][2] == ziel_typ:
                    setze r auf finde_route(gebaeude[i][0], gebaeude[i][1], gebaeude[j][0], gebaeude[j][1])
                    wenn länge(r) > 0:
                        setze result auf result + [r]
                    stopp
                setze j auf j + 1
        setze i auf i + 1
    gib_zurück result

# HOOK 4: siedler_walk_step() — IMPLEMENTIERT in M3 unten (siehe Siedler-Block).

# -----------------------------------------------------------------
# Wind-System (globaler Vektor + Staerke)
# -----------------------------------------------------------------
setze wind_phase auf [0.0]
funktion wind_x():
    gib_zurück sinus(wind_phase[0] * 0.018) * 1.5

funktion wind_z():
    gib_zurück cosinus(wind_phase[0] * 0.012) * 1.2

funktion wind_staerke():
    gib_zurück 0.5 + (sinus(wind_phase[0] * 0.03) + 1.0) * 0.4

# -----------------------------------------------------------------
# Partikel-Pool (Rauch, Funken, Staub, fliegende Blaetter)
# Jedes Partikel: [x, y, z, vx, vy, vz, leben, farbe]
# -----------------------------------------------------------------
setze partikel auf [[]]
partikel[0] = []

funktion partikel_neu(x, y, z, vx, vy, vz, leben, farbe):
    partikel[0] = partikel[0] + [[x, y, z, vx, vy, vz, leben, farbe]]

funktion partikel_tick():
    setze liste auf []
    für p in partikel[0]:
        setze leben auf p[6] - 1
        wenn leben > 0:
            setze nx auf p[0] + p[3] + wind_x() * 0.05 * wind_staerke()
            setze ny auf p[1] + p[4]
            setze nz auf p[2] + p[5] + wind_z() * 0.05 * wind_staerke()
            setze liste auf liste + [[nx, ny, nz, p[3] * 0.96, p[4] * 0.97, p[5] * 0.96, leben, p[7]]]
    partikel[0] = liste

funktion partikel_render(win):
    für p in partikel[0]:
        # p[0]=wx, p[1]=y_off_world, p[2]=wy; Iso-Projektion fuer Render
        setze sx auf iso_x(p[0], p[2])
        setze sy auf iso_y(p[0], p[2], p[1])
        setze sz auf iso_z(p[0], p[2], p[1]) - 2.0
        setze groesse auf 1 + p[6] / 20
        zeichne_kreis_z(win, sx, sy, sz, groesse, p[7])

# Schornstein-Rauch
funktion emit_rauch():
    für g in gebaeude:
        wenn hat_schornstein(g[2]):
            setze h auf terrain_h(g[0], g[1])
            setze px auf g[0] * 1.0 + (hash01(g[0], wind_phase[0]) - 0.5) * 0.3
            setze pvx auf (hash01(g[0] + 1, g[1]) - 0.5) * 0.01
            setze pvz auf (hash01(g[1] + 1, g[0]) - 0.5) * 0.01
            partikel_neu(px, h + 2.0, g[1] * 1.0, pvx, 0.04, pvz, 60, "grau")

# Fliegende Blaetter/Bluetenstaub aus Baumkronen — wind-driven
funktion emit_blatt():
    für p in baum_positionen:
        setze bh auf terrain_h(p[0], p[1])
        wenn bh >= 1 und bh <= 2:
            setze ph auf wind_phase[0]
            setze bvx auf wind_x() * 0.04
            setze bvz auf wind_z() * 0.04
            setze farbe auf "gruen"
            wenn (hash01(p[0] + ph, p[1]) > 0.7):
                setze farbe auf "gelb"
            wenn (hash01(p[1] + ph, p[0]) > 0.85):
                setze farbe auf "weiss"
            partikel_neu(p[0] + 0.3 + (hash01(ph, p[0]) - 0.5) * 0.2, bh + 0.85, p[1] + 0.3 + (hash01(p[1], ph) - 0.5) * 0.2, bvx, -0.005, bvz, 100, farbe)

# Funken am Steinmetz (typ 3)
funktion emit_funken():
    für g in gebaeude:
        wenn g[2] == 3:
            setze h auf terrain_h(g[0], g[1])
            setze ph auf wind_phase[0]
            setze fvx auf (hash01(g[0] + ph, g[1]) - 0.5) * 0.06
            setze fvz auf (hash01(g[1] + ph, g[0]) - 0.5) * 0.06
            partikel_neu(g[0] * 1.0, h + 0.8, g[1] * 1.0, fvx, 0.02, fvz, 20, "orange")

# -----------------------------------------------------------------
# Figuren: 6 Siedler die zwischen zwei Gebaeuden pendeln
# -----------------------------------------------------------------
# M3 (k3): Siedler folgen BFS-Routen + tragen sichtbar Ressource.
# Schema: [von_idx, nach_idx, schritt, t_seg, speed, farbe, res_typ, route]
# Beim ersten Tick: ensure_route() berechnet via finde_route().
setze siedler auf [[0, 1, 0, 0.0, 0.04, "gruen", "holz", []], [1, 3, 0, 0.0, 0.04, "weiss", "bretter", []], [2, 3, 0, 0.0, 0.04, "grau", "stein", []], [5, 6, 0, 0.0, 0.04, "gelb", "korn", []], [6, 7, 0, 0.0, 0.04, "weiss", "mehl", []], [7, 3, 0, 0.0, 0.04, "orange", "brot", []], [4, 1, 0, 0.0, 0.04, "gruen", "holz", []]]

funktion res_farbe(typ):
    wenn typ == "holz":
        gib_zurück "orange"
    wenn typ == "bretter":
        gib_zurück "weiss"
    wenn typ == "stein":
        gib_zurück "grau"
    wenn typ == "korn":
        gib_zurück "gelb"
    wenn typ == "mehl":
        gib_zurück "weiss"
    wenn typ == "brot":
        gib_zurück "orange"
    gib_zurück "magenta"

funktion ensure_route(idx):
    setze s auf siedler[idx]
    wenn länge(s[7]) == 0:
        setze a auf gebaeude[s[0]]
        setze b auf gebaeude[s[1]]
        setze r auf finde_route(a[0], a[1], b[0], b[1])
        wenn länge(r) > 0:
            s[7] = r
            s[2] = 0
            s[3] = 0.0
            siedler[idx] = s

# Liefert [fx, fy] der aktuellen Weltposition.
funktion siedler_pos(s):
    wenn länge(s[7]) == 0:
        setze g auf gebaeude[s[0]]
        gib_zurück [g[0] * 1.0, g[1] * 1.0]
    setze schritt auf s[2]
    setze t auf s[3]
    setze ax auf 0.0
    setze ay auf 0.0
    wenn schritt == 0:
        setze g auf gebaeude[s[0]]
        setze ax auf g[0] * 1.0
        setze ay auf g[1] * 1.0
    sonst:
        setze a_tile auf s[7][schritt - 1]
        setze ax auf a_tile[0] * 1.0
        setze ay auf a_tile[1] * 1.0
    setze b_tile auf s[7][schritt]
    setze fx auf ax * (1.0 - t) + b_tile[0] * t
    setze fy auf ay * (1.0 - t) + b_tile[1] * t
    gib_zurück [fx, fy]

funktion siedler_walk_step():
    setze i auf 0
    solange i < länge(siedler):
        ensure_route(i)
        setze s auf siedler[i]
        wenn länge(s[7]) > 0:
            setze t_neu auf s[3] + s[4]
            wenn t_neu >= 1.0:
                s[2] = s[2] + 1
                s[3] = 0.0
                wenn s[2] >= länge(s[7]):
                    # Ziel erreicht: Swap von/nach + Rueckweg
                    setze tmp auf s[0]
                    s[0] = s[1]
                    s[1] = tmp
                    s[7] = []
                    s[2] = 0
            sonst:
                s[3] = t_neu
            siedler[i] = s
        setze i auf i + 1

funktion siedler_zeichnen(win):
    für s in siedler:
        setze pos auf siedler_pos(s)
        setze fx auf pos[0]
        setze fy auf pos[1]
        setze fh auf terrain_h(boden(fx), boden(fy))
        setze bob auf sinus(wind_phase[0] * 0.25 + s[3] * 20) * 2.0
        setze sx auf iso_x(fx, fy)
        setze sy auf iso_y(fx, fy, fh)
        setze sz auf iso_z(fx, fy, fh) - 4.0
        # Beine (kleiner Block), Koerper (groesser), Kopf (Kreis)
        zeichne_rechteck_z(win, sx - 3, sy - 16, sz, 6, 6, s[5])
        zeichne_rechteck_z(win, sx - 5, sy - 24 + bob, sz, 10, 8, s[5])
        zeichne_kreis_z(win, sx, sy - 30 + bob, sz, 4, s[5])
        # Resource-Tragen: kleines Rechteck ueber Kopf
        zeichne_rechteck_z(win, sx - 3, sy - 38 + bob, sz - 0.1, 6, 5, res_farbe(s[6]))

# -----------------------------------------------------------------
# Flaggen auf Haeusern
# -----------------------------------------------------------------
funktion flaggen_zeichnen(win):
    für g in gebaeude:
        wenn g[2] == 0:
            setze h auf terrain_h(g[0], g[1])
            setze sx auf iso_x(g[0], g[1])
            setze sy auf iso_y(g[0], g[1], h)
            setze sz auf iso_z(g[0], g[1], h) - 5.0
            # Pfosten: vertikale duenne Linie
            zeichne_rechteck_z(win, sx + 8, sy - 48, sz, 2, 28, "#555555")
            # Flagge wehend — 3 Segmente mit Wind-Offset
            setze j auf 0
            solange j < 3:
                setze off auf wind_x() * 3 * (j + 1) * wind_staerke()
                zeichne_rechteck_z(win, sx + 10 + j * 4 + off, sy - 48, sz - 0.1, 4, 8, "rot")
                setze j auf j + 1

# -----------------------------------------------------------------
# Baeume mit Wind-Schaukel
# -----------------------------------------------------------------
setze baum_positionen auf []
setze bi auf 0
solange bi < 14:
    setze bx auf boden(hash01(bi, 7) * WORLD)
    setze by auf boden(hash01(bi + 50, 13) * WORLD)
    baum_positionen.hinzufügen([bx, by])
    setze bi auf bi + 1

funktion baeume_zeichnen(win):
    für p in baum_positionen:
        setze bh auf terrain_h(p[0], p[1])
        wenn bh >= 1 und bh <= 2:
            setze sx auf iso_x(p[0], p[1])
            setze sy auf iso_y(p[0], p[1], bh)
            setze sz auf iso_z(p[0], p[1], bh) - 3.0
            # Stamm
            zeichne_rechteck_z(win, sx - 2, sy - 18, sz, 4, 10, "#5D4037")
            # Krone — Kreis, schwingt mit Wind
            setze sway auf wind_x() * 4 * wind_staerke()
            zeichne_kreis_z(win, sx + sway, sy - 26, sz, 9, "#2E7D32")

# -----------------------------------------------------------------
# Gras-Wiegen
# -----------------------------------------------------------------
funktion gras_zeichnen(win):
    setze gx auf 0
    solange gx < WORLD:
        setze gy auf 0
        solange gy < WORLD:
            wenn terrain_h(gx, gy) == 2 und (hash01(gx, gy) > 0.55):
                setze gh auf terrain_h(gx, gy)
                setze sx auf iso_x(gx, gy)
                setze sy auf iso_y(gx, gy, gh)
                setze sz auf iso_z(gx, gy, gh) - 1.5
                setze sway auf wind_x() * 2 * wind_staerke()
                zeichne_rechteck_z(win, sx - 1 + sway, sy - 4, sz, 2, 3, "#4CAF50")
            setze gy auf gy + 1
        setze gx auf gx + 1

# -----------------------------------------------------------------
# Haupt-Szene
# -----------------------------------------------------------------
setze win auf fenster_unified("Siedler 3 — Hybrid (moo)", WIN_W, WIN_H)
# Niedriger FOV simuliert orthografische Iso-Projektion (Voxel-Charme).
# raum_* funktioniert via P6-Bridge auf dem Hybrid-Window (shared Z-Buffer).
raum_perspektive(win, 28.0, 0.1, 200.0)

# Iso-Default: hoch ueber der Karte, ~30° Elevation, 45° Azimuth
setze kamera_winkel auf 0.785
setze kamera_hoehe auf 14.0
setze kamera_radius auf 22.0
setze tick auf 0

# Test-Modus: env MOO_SIEDLER_TEST=1 → 60 Frames Loop mit Screenshots,
# dann sauberer Exit 0. Sonst normaler interaktiver Modus.
setze test_modus auf umgebung("MOO_SIEDLER_TEST") != ""

setze sun_phase auf [0.15]

funktion sky_r():
    setze t auf sun_phase[0]
    wenn t < 0.2:
        gib_zurück 0.9 - t * 2
    wenn t < 0.5:
        gib_zurück 0.5 - (t - 0.2) * 0.6
    wenn t < 0.7:
        gib_zurück 0.32 + (t - 0.5) * 1.5
    gib_zurück 0.62 - (t - 0.7) * 2

funktion sky_g():
    setze t auf sun_phase[0]
    wenn t < 0.2:
        gib_zurück 0.5 - t * 1
    wenn t < 0.5:
        gib_zurück 0.3 + (t - 0.2) * 0.8
    wenn t < 0.7:
        gib_zurück 0.54 + (t - 0.5) * 1.0
    gib_zurück 0.74 - (t - 0.7) * 2.4

funktion sky_b():
    setze t auf sun_phase[0]
    wenn t < 0.5:
        gib_zurück 0.6 + t * 0.4
    wenn t < 0.7:
        gib_zurück 0.8 + (t - 0.5) * 0.5
    gib_zurück 0.9 - (t - 0.7) * 2.5

raum_maus_fangen(win)

solange hybrid_offen(win):
    wenn raum_taste(win, "escape"):
        stopp
    # Test-Modus: 60 Frames + Screenshot alle 10, dann beenden
    wenn test_modus und tick >= 60:
        stopp
    wenn test_modus und tick % 10 == 0:
        setze fname auf "/tmp/siedler3_frame_" + text(tick) + ".bmp"
        screenshot(win, fname)
    wenn raum_taste(win, "a"):
        setze kamera_winkel auf kamera_winkel - 0.02
    wenn raum_taste(win, "d"):
        setze kamera_winkel auf kamera_winkel + 0.02
    wenn raum_taste(win, "w"):
        setze kamera_hoehe auf kamera_hoehe + 0.1
    wenn raum_taste(win, "s"):
        setze kamera_hoehe auf kamera_hoehe - 0.1
    wenn raum_taste(win, "q"):
        setze kamera_radius auf kamera_radius - 0.2
    wenn raum_taste(win, "e"):
        setze kamera_radius auf kamera_radius + 0.2
    setze mdx auf raum_maus_dx(win)
    setze mdy auf raum_maus_dy(win)
    setze kamera_winkel auf kamera_winkel + mdx * 0.005
    # Shift halten + Mausbewegung vertikal: Zoom statt Hoehe (Maus-Zoom)
    wenn raum_taste(win, "shift"):
        setze kamera_radius auf kamera_radius + mdy * 0.06
    sonst:
        setze kamera_hoehe auf kamera_hoehe + mdy * 0.04
    wenn kamera_hoehe < 3:
        setze kamera_hoehe auf 3.0
    wenn kamera_radius < 5:
        setze kamera_radius auf 5.0

    wind_phase[0] = wind_phase[0] + 1.0
    sun_phase[0] = sun_phase[0] + 0.004
    wenn sun_phase[0] > 1.0:
        sun_phase[0] = 0.0

    partikel_tick()
    wenn tick % 8 == 0:
        emit_rauch()
    wenn tick % 12 == 0:
        emit_funken()
    wenn tick % 20 == 0:
        emit_blatt()

    # Wirtschaft + Siedler-Bewegung (Hooks → k3 in M2/M3)
    wenn tick % 60 == 0:
        tick_wirtschaft()
    siedler_walk_step()

    # Frame rendern (Himmelfarbe = Tag/Nacht-Zyklus)
    hybrid_löschen(win, sky_r(), sky_g(), sky_b())
    setze zentrum_x auf WORLD / 2
    setze zentrum_z auf WORLD / 2
    setze cam_eye_x auf zentrum_x + cosinus(kamera_winkel) * kamera_radius
    setze cam_eye_z auf zentrum_z + sinus(kamera_winkel) * kamera_radius
    raum_kamera(win, cam_eye_x, kamera_hoehe, cam_eye_z, zentrum_x, 1.5, zentrum_z)

    # Terrain — 2.5D Iso-Tiles via zeichne_rechteck_z (M5.4 Option A):
    # Axis-aligned Pixel-Rects in iso-Pixel-Coords, Y-Offset fuer Hoehe.
    # Painter-Reihenfolge via Welt-Z (groesser = weiter hinten).
    setze tx auf 0
    solange tx < WORLD:
        setze ty auf 0
        solange ty < WORLD:
            setze h auf terrain_h(tx, ty)
            setze px auf ISO_OX + (tx - ty) * (TILE_W / 2)
            setze py auf ISO_OY + (tx + ty) * (TILE_H / 2) - h * ISO_HEIGHT
            # Welt-Z: weiter weg = groesserer z, Hoehe "nach vorne" = kleiner z
            setze tz auf 60.0 + (tx + ty) * 0.5 - h * 2.0
            zeichne_rechteck_z(win, px - TILE_W / 2, py, tz, TILE_W, TILE_H, biom_farbe(h))
            # Schatten-Linie an Oberkante fuer Iso-Andeutung
            zeichne_linie_z(win, px - TILE_W / 2, py, px + TILE_W / 2, py, tz - 0.1, "#00000040")
            setze ty auf ty + 1
        setze tx auf tx + 1

    gras_zeichnen(win)
    baeume_zeichnen(win)

    # Gebaeude — Iso-Pixel-Rect-Stack (Basis + Dach)
    für g in gebaeude:
        setze gh auf terrain_h(g[0], g[1])
        setze bx auf iso_x(g[0], g[1])
        setze by auf iso_y(g[0], g[1], gh)
        setze bz auf iso_z(g[0], g[1], gh) - 2.0
        # Basis (Quader)
        zeichne_rechteck_z(win, bx - 16, by - 28, bz, 32, 24, gebaeude_farbe(g[2]))
        # Dachschraege als zwei Rechtecke
        zeichne_rechteck_z(win, bx - 16, by - 38, bz - 0.1, 32, 10, "#B71C1C")
        zeichne_linie_z(win, bx - 16, by - 38, bx + 16, by - 38, bz - 0.2, "#000000")
        # Tuer
        zeichne_rechteck_z(win, bx - 3, by - 12, bz - 0.3, 6, 10, "#3E2723")

    flaggen_zeichnen(win)
    siedler_zeichnen(win)
    partikel_render(win)

    # 2D-HUD via Hybrid-API (zeichne_rechteck_z, Welt-Z=0.5 → vor allem 3D).
    # Sechs Resource-Balken oben links; Balkenlaenge = Counter * 3 Pixel.
    zeichne_rechteck_z(win, 10, 10, 0.5, 180, 80, "#10182088")
    zeichne_rechteck_z(win, 15, 15, 0.4, r_holz[0] * 3 + 2, 8, "#8D6E63")
    zeichne_rechteck_z(win, 15, 27, 0.4, r_bretter[0] * 3 + 2, 8, "#D7CCC8")
    zeichne_rechteck_z(win, 15, 39, 0.4, r_stein[0] * 3 + 2, 8, "#607D8B")
    zeichne_rechteck_z(win, 100, 15, 0.4, r_korn[0] * 3 + 2, 8, "#FFEB3B")
    zeichne_rechteck_z(win, 100, 27, 0.4, r_mehl[0] * 3 + 2, 8, "#FFFFFF")
    zeichne_rechteck_z(win, 100, 39, 0.4, r_brot[0] * 3 + 2, 8, "#FF9800")

    hybrid_aktualisieren(win)
    setze tick auf tick + 1
    warte(16)

hybrid_schliessen(win)

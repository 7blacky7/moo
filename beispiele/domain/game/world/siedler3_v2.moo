# ============================================================
# Siedler 3 v2 — Echte 3D-Szene (raum_*-API) mit Wind + Atmosphaere
#
# Kompilieren: moo-compiler compile beispiele/domain/game/world/siedler3_v2.moo \
#              -o beispiele/domain/game/world/siedler3_v2
# Starten:     ./beispiele/domain/game/world/siedler3_v2
#
# Steuerung: WASD = Kamera-Umlauf, Q/E = Zoom, Escape = Beenden
#
# Unterschiede zu v1:
# - raum_*-API (echte 3D-Szene, iso-aehnliche Schraeg-Kamera)
# - Wind-System (globaler 2D-Vektor, sinus-animiert)
# - Atmosphaere: Rauch aus Schornsteinen, Funken, Staub, animierte Baeume/Gras
# - Volumetrische 3D-Figuren (Koerper + Kopf + Arm) die sich bewegen
# - Gebaeude bleiben einfache Wuerfel-Stacks (2.5D-Stil ok laut Vorgabe)
# ============================================================

konstante WORLD auf 14
konstante WIN_W auf 1024
konstante WIN_H auf 640

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
# Gebaeude: [wx, wy, typ]
# typ: 0=Haus (Senke), 1=Holzfaeller, 2=Saegewerk, 3=Steinmetz, 4=Bauer, 5=Muehle, 6=Baecker
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
# Partikel-Pool (fuer Rauch, Funken, Staub)
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
        # kleine wuerfel als partikel
        setze groesse auf 0.08 + p[6] * 0.003
        raum_würfel(win, p[0], p[1], p[2], groesse, p[7])

# Schornstein-Rauch
funktion emit_rauch():
    für g in gebaeude:
        wenn hat_schornstein(g[2]):
            setze h auf terrain_h(g[0], g[1])
            setze px auf g[0] * 1.0 + (hash01(g[0], wind_phase[0]) - 0.5) * 0.3
            setze pvx auf (hash01(g[0] + 1, g[1]) - 0.5) * 0.01
            setze pvz auf (hash01(g[1] + 1, g[0]) - 0.5) * 0.01
            partikel_neu(px, h + 2.0, g[1] * 1.0, pvx, 0.04, pvz, 60, "grau")

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
# pos-index (0..1) animiert, Position = interpoliert zwischen zwei Gebaeuden
# -----------------------------------------------------------------
setze siedler auf [
    [0, 1, 0.0, 0.008, "gelb"],
    [1, 2, 0.3, 0.006, "gelb"],
    [2, 0, 0.7, 0.005, "rot"],
    [3, 5, 0.1, 0.007, "blau"],
    [5, 7, 0.4, 0.006, "weiss"],
    [7, 6, 0.8, 0.009, "magenta"]
]

funktion siedler_tick():
    setze i auf 0
    solange i < länge(siedler):
        setze s auf siedler[i]
        setze t auf s[2] + s[3]
        wenn t > 1.0:
            # umdrehen
            s[0] = siedler[i][1]
            s[1] = siedler[i][0]
            s[2] = 0.0
        sonst:
            s[2] = t
        siedler[i] = s
        setze i auf i + 1

funktion siedler_zeichnen(win):
    für s in siedler:
        setze g_a auf gebaeude[s[0]]
        setze g_b auf gebaeude[s[1]]
        setze t auf s[2]
        setze fx auf g_a[0] * (1.0 - t) + g_b[0] * t
        setze fy auf g_a[1] * (1.0 - t) + g_b[1] * t
        setze fh auf terrain_h(boden(fx), boden(fy))
        # Volumetrische 3D-Figur: Beine (wuerfel) + Koerper (wuerfel) + Kopf (kugel) + Arm-Schwung (wuerfel mit sinus-offset)
        setze bob auf sinus(wind_phase[0] * 0.25 + t * 20) * 0.05
        raum_würfel(win, fx, fh + 0.25, fy, 0.18, s[4])
        raum_würfel(win, fx, fh + 0.55 + bob, fy, 0.22, s[4])
        raum_kugel(win, fx, fh + 0.85 + bob, fy, 0.14, s[4], 5)
        # Arm schwenkt mit Wind (Platzhalter fuer Animations-Idee)
        setze arm_dx auf wind_x() * 0.05 + sinus(t * 15) * 0.06
        raum_würfel(win, fx + arm_dx, fh + 0.55 + bob, fy, 0.09, s[4])

# -----------------------------------------------------------------
# Flaggen: eine Flagge je Haus, dreht sich um Pfosten mit Wind
# -----------------------------------------------------------------
funktion flaggen_zeichnen(win):
    für g in gebaeude:
        wenn g[2] == 0:
            setze h auf terrain_h(g[0], g[1])
            # Pfosten
            setze i auf 0
            solange i < 4:
                raum_würfel(win, g[0] + 0.4, h + 0.5 + i * 0.2, g[1] + 0.4, 0.04, "grau")
                setze i auf i + 1
            # Tuch: 3 Segmente die mit Wind driften
            setze j auf 0
            solange j < 3:
                setze off auf wind_x() * 0.08 * (j + 1) * wind_staerke()
                raum_würfel(win, g[0] + 0.4 + off, h + 1.1, g[1] + 0.4 + (j + 1) * 0.12, 0.06, "rot")
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
            # Stamm
            raum_würfel(win, p[0] + 0.3, bh + 0.3, p[1] + 0.3, 0.1, "orange")
            # Krone: schwingt mit Wind
            setze sway auf wind_x() * 0.2 * wind_staerke()
            raum_kugel(win, p[0] + 0.3 + sway, bh + 0.85, p[1] + 0.3, 0.35, "gruen", 6)

# -----------------------------------------------------------------
# Gras-Wiegen: kleine Halme auf Wiesen-Tiles mit Wind-Neigung
# -----------------------------------------------------------------
funktion gras_zeichnen(win):
    setze gx auf 0
    solange gx < WORLD:
        setze gy auf 0
        solange gy < WORLD:
            wenn terrain_h(gx, gy) == 2 und (hash01(gx, gy) > 0.55):
                setze gh auf terrain_h(gx, gy)
                setze sway auf wind_x() * 0.08 * wind_staerke()
                raum_würfel(win, gx + 0.5 + sway, gh + 0.15, gy + 0.5, 0.04, "gruen")
            setze gy auf gy + 1
        setze gx auf gx + 1

# -----------------------------------------------------------------
# Haupt-Szene
# -----------------------------------------------------------------
setze win auf raum_erstelle("Siedler 3 v2 — Echt 3D (moo)", WIN_W, WIN_H)
raum_perspektive(win, 50.0, 0.1, 100.0)

setze kamera_winkel auf 0.0
setze kamera_hoehe auf 10.0
setze kamera_radius auf 16.0
setze tick auf 0

solange raum_offen(win):
    wenn raum_taste(win, "escape"):
        stopp
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
    wenn kamera_hoehe < 3:
        setze kamera_hoehe auf 3.0
    wenn kamera_radius < 5:
        setze kamera_radius auf 5.0

    # Wind-Phase animiert
    wind_phase[0] = wind_phase[0] + 1.0

    # Partikel-Tick + Spawn
    partikel_tick()
    wenn tick % 8 == 0:
        emit_rauch()
    wenn tick % 12 == 0:
        emit_funken()

    # Siedler bewegen
    siedler_tick()

    # Frame rendern
    raum_löschen(win, 0.35, 0.55, 0.8)
    setze zentrum_x auf WORLD / 2
    setze zentrum_z auf WORLD / 2
    setze cam_eye_x auf zentrum_x + cosinus(kamera_winkel) * kamera_radius
    setze cam_eye_z auf zentrum_z + sinus(kamera_winkel) * kamera_radius
    raum_kamera(win, cam_eye_x, kamera_hoehe, cam_eye_z, zentrum_x, 1.5, zentrum_z)

    # Terrain als Wuerfel
    setze tx auf 0
    solange tx < WORLD:
        setze ty auf 0
        solange ty < WORLD:
            setze h auf terrain_h(tx, ty)
            raum_würfel(win, tx + 0.5, h * 0.5, ty + 0.5, 1.0, biom_farbe(h))
            setze ty auf ty + 1
        setze tx auf tx + 1

    # Gras-Halme + Baeume (mit Wind)
    gras_zeichnen(win)
    baeume_zeichnen(win)

    # Gebaeude: Basis-Wuerfel + Dach-Wuerfel
    für g in gebaeude:
        setze gh auf terrain_h(g[0], g[1])
        raum_würfel(win, g[0] + 0.5, gh + 0.6, g[1] + 0.5, 0.6, gebaeude_farbe(g[2]))
        raum_würfel(win, g[0] + 0.5, gh + 1.2, g[1] + 0.5, 0.35, "rot")

    # Flaggen (Haus-Dach)
    flaggen_zeichnen(win)

    # Siedler (echte volumetrische 3D-Figuren)
    siedler_zeichnen(win)

    # Partikel (Rauch + Funken)
    partikel_render(win)

    raum_aktualisieren(win)
    setze tick auf tick + 1
    warte(16)

raum_schliessen(win)

# ============================================================
# moo 3D Wireframe-Renderer — Software-3D ohne OpenGL
#
# Kompilieren: moo-compiler compile wireframe.moo -o wireframe
# Starten:     ./wireframe
#
# Bedienung:
#   T (Taste)  → naechstes Modell (Cube → Pyramid → Torus)
#   X/Y/Z      → Auto-Rotation um Achse toggeln
#   Pfeiltasten → manuelle Rotation
# ============================================================

konstante BREITE auf 900
konstante HOEHE auf 700
konstante CX auf 450
konstante CY auf 350
konstante FOCAL auf 400.0
konstante CAM_Z auf 5.0

# --- Math-Naeherungen ---
funktion my_cos(x):
    setze pi auf 3.14159265
    solange x > pi:
        setze x auf x - 2 * pi
    solange x < -pi:
        setze x auf x + 2 * pi
    setze x2 auf x * x
    gib_zurück 1 - x2 / 2 + x2 * x2 / 24 - x2 * x2 * x2 / 720

funktion my_sin(x):
    gib_zurück my_cos(x - 1.5707963)

# --- 3x5 Font ---
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
FONT[" "] = [0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0]
FONT[":"] = [0,0,0, 0,1,0, 0,0,0, 0,1,0, 0,0,0]
FONT["F"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,0,0]
FONT["P"] = [1,1,1, 1,0,1, 1,1,1, 1,0,0, 1,0,0]
FONT["S"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
FONT["N"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT["O"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["T"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,0]
FONT["A"] = [0,1,0, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
FONT["I"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["L"] = [1,0,0, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["C"] = [1,1,1, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["U"] = [1,0,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["B"] = [1,1,0, 1,0,1, 1,1,0, 1,0,1, 1,1,0]
FONT["E"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1]
FONT["R"] = [1,1,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1]
FONT["Y"] = [1,0,1, 1,0,1, 0,1,0, 0,1,0, 0,1,0]
FONT["M"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT["D"] = [1,1,0, 1,0,1, 1,0,1, 1,0,1, 1,1,0]
FONT["H"] = [1,0,1, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
FONT["X"] = [1,0,1, 1,0,1, 0,1,0, 1,0,1, 1,0,1]
FONT["Z"] = [1,1,1, 0,0,1, 0,1,0, 1,0,0, 1,1,1]

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

# --- Modelle: Struct-of-Arrays ---
# verts = [x0,y0,z0, x1,y1,z1, ...]
# tris  = [i0,i1,i2, i3,i4,i5, ...]  (Indizes in Vertex-Liste)
# colors = Liste von Strings, 1 pro Triangle

funktion make_cube():
    setze v auf [-1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, 1, 1, -1, 1, 1, 1, 1, -1, 1, 1]
    setze t auf [0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1, 1, 5, 6, 1, 6, 2, 2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0]
    setze c auf ["rot", "rot", "gruen", "gruen", "blau", "blau", "gelb", "gelb", "magenta", "magenta", "cyan", "cyan"]
    gib_zurück {"verts": v, "tris": t, "colors": c}

funktion make_pyramid():
    setze v auf [-1, 0, -1,   1, 0, -1,   1, 0, 1,   -1, 0, 1,   0, 1.5, 0]
    setze t auf [0, 2, 1, 0, 3, 2, 0, 1, 4, 1, 2, 4, 2, 3, 4, 3, 0, 4]
    setze c auf ["gelb", "gelb", "rot", "gruen", "blau", "magenta"]
    gib_zurück {"verts": v, "tris": t, "colors": c}

# Torus: Major-Radius R, Minor-Radius r, ringSegs x tubeSegs
funktion make_torus():
    setze R auf 1.3
    setze r auf 0.45
    setze RS auf 16
    setze TS auf 10
    setze v auf []
    setze i auf 0
    solange i < RS:
        setze u auf i / RS * 6.28318
        setze j auf 0
        solange j < TS:
            setze vu auf j / TS * 6.28318
            setze xr auf (R + r * my_cos(vu)) * my_cos(u)
            setze yr auf r * my_sin(vu)
            setze zr auf (R + r * my_cos(vu)) * my_sin(u)
            v.hinzufügen(xr)
            v.hinzufügen(yr)
            v.hinzufügen(zr)
            setze j auf j + 1
        setze i auf i + 1
    # Triangles
    setze t auf []
    setze c auf []
    setze faerbe auf ["rot", "gruen", "blau", "gelb", "magenta", "cyan", "orange", "hellgruen"]
    setze i auf 0
    solange i < RS:
        setze ni auf (i + 1) % RS
        setze j auf 0
        solange j < TS:
            setze nj auf (j + 1) % TS
            setze a auf i * TS + j
            setze b auf ni * TS + j
            setze cc auf ni * TS + nj
            setze d auf i * TS + nj
            t.hinzufügen(a)
            t.hinzufügen(b)
            t.hinzufügen(cc)
            c.hinzufügen(faerbe[(i + j) % 8])
            t.hinzufügen(a)
            t.hinzufügen(cc)
            t.hinzufügen(d)
            c.hinzufügen(faerbe[(i + j) % 8])
            setze j auf j + 1
        setze i auf i + 1
    gib_zurück {"verts": v, "tris": t, "colors": c}

# --- Transform + Projektion ---
# Rotiert einen einzelnen Punkt um X, Y, Z und gibt projizierte screen-Koordinaten (inkl z fuer Painter)
funktion transform_und_projiziere(x, y, z, ax, ay, az):
    # Rotate X
    setze cs auf my_cos(ax)
    setze sn auf my_sin(ax)
    setze y2 auf y * cs - z * sn
    setze z2 auf y * sn + z * cs
    setze x2 auf x
    # Rotate Y
    setze cs auf my_cos(ay)
    setze sn auf my_sin(ay)
    setze x3 auf x2 * cs + z2 * sn
    setze z3 auf -x2 * sn + z2 * cs
    setze y3 auf y2
    # Rotate Z
    setze cs auf my_cos(az)
    setze sn auf my_sin(az)
    setze x4 auf x3 * cs - y3 * sn
    setze y4 auf x3 * sn + y3 * cs
    setze z4 auf z3
    # Perspektive
    setze zd auf CAM_Z - z4
    wenn zd < 0.1:
        setze zd auf 0.1
    setze sx auf x4 / zd * FOCAL + CX
    setze sy auf -y4 / zd * FOCAL + CY
    gib_zurück [sx, sy, z4, zd]

funktion transform_mesh(mesh, ax, ay, az):
    setze vin auf mesh["verts"]
    setze n auf boden(vin.länge() / 3)
    setze projected auf []
    setze i auf 0
    solange i < n:
        setze o auf i * 3
        setze p auf transform_und_projiziere(vin[o], vin[o + 1], vin[o + 2], ax, ay, az)
        projected.hinzufügen(p[0])
        projected.hinzufügen(p[1])
        projected.hinzufügen(p[2])
        projected.hinzufügen(p[3])
        setze i auf i + 1
    gib_zurück projected

# --- Triangle Rendering ---
# Painter's: sortiere Triangles nach avg-Z (von hinten nach vorn)
# Plus Back-Face-Culling via Screen-Space-Cross.
funktion zeichne_mesh(fenster, mesh, projected):
    setze tris auf mesh["tris"]
    setze colors auf mesh["colors"]
    setze anz auf boden(tris.länge() / 3)
    # avg-Z pro Triangle
    setze indices auf []
    setze zs auf []
    setze k auf 0
    solange k < anz:
        setze toff auf k * 3
        setze i0 auf tris[toff]
        setze i1 auf tris[toff + 1]
        setze i2 auf tris[toff + 2]
        setze z_avg auf (projected[i0 * 4 + 2] + projected[i1 * 4 + 2] + projected[i2 * 4 + 2]) / 3
        indices.hinzufügen(k)
        zs.hinzufügen(z_avg)
        setze k auf k + 1

    # Insertion-Sort indices by zs aufsteigend (von hinten = kleinstes z zuerst)
    setze i auf 1
    solange i < anz:
        setze j auf i
        solange j > 0:
            setze a auf indices[j - 1]
            setze b auf indices[j]
            wenn zs[a] > zs[b]:
                indices[j - 1] = b
                indices[j] = a
                setze j auf j - 1
            sonst:
                setze j auf 0
        setze i auf i + 1

    # Zeichne in sortierter Reihenfolge
    setze k auf 0
    solange k < anz:
        setze ti auf indices[k]
        setze toff auf ti * 3
        setze i0 auf tris[toff]
        setze i1 auf tris[toff + 1]
        setze i2 auf tris[toff + 2]

        setze sx0 auf projected[i0 * 4]
        setze sy0 auf projected[i0 * 4 + 1]
        setze sx1 auf projected[i1 * 4]
        setze sy1 auf projected[i1 * 4 + 1]
        setze sx2 auf projected[i2 * 4]
        setze sy2 auf projected[i2 * 4 + 1]

        # Back-face culling via 2D-Cross (clockwise vs counter)
        setze ex1 auf sx1 - sx0
        setze ey1 auf sy1 - sy0
        setze ex2 auf sx2 - sx0
        setze ey2 auf sy2 - sy0
        setze cross auf ex1 * ey2 - ey1 * ex2
        wenn cross < 0:
            setze farbe auf colors[ti]
            # Wireframe-Edges
            zeichne_linie(fenster, boden(sx0), boden(sy0), boden(sx1), boden(sy1), farbe)
            zeichne_linie(fenster, boden(sx1), boden(sy1), boden(sx2), boden(sy2), farbe)
            zeichne_linie(fenster, boden(sx2), boden(sy2), boden(sx0), boden(sy0), farbe)
        setze k auf k + 1

# --- Main ---
zeige "=== moo 3D Wireframe-Renderer ==="
zeige "Cube / Pyramid / Torus, Painter + Back-Face-Culling"
zeige "Bedienung: T=Modell, X/Y/Z=Auto-Rot, Pfeile=manuell"

setze modelle auf [make_cube(), make_pyramid(), make_torus()]
setze namen auf ["CUBE", "PYRAMID", "TORUS"]
setze idx auf 0

setze ax auf 0.3
setze ay auf 0.5
setze az auf 0.0
setze rot_x auf falsch
setze rot_y auf wahr
setze rot_z auf falsch

setze vorher_t auf falsch
setze vorher_x auf falsch
setze vorher_y auf falsch
setze vorher_z auf falsch

setze fenster auf fenster_erstelle("moo Wireframe", BREITE, HOEHE)

setze fps auf 0
setze frame_count auf 0
setze fps_start auf zeit_ms()

solange fenster_offen(fenster):
    # --- Input ---
    setze t_jetzt auf taste_gedrückt("t")
    wenn t_jetzt und nicht vorher_t:
        setze idx auf (idx + 1) % 3
    setze vorher_t auf t_jetzt

    setze x_jetzt auf taste_gedrückt("x")
    wenn x_jetzt und nicht vorher_x:
        setze rot_x auf nicht rot_x
    setze vorher_x auf x_jetzt

    setze y_jetzt auf taste_gedrückt("y")
    wenn y_jetzt und nicht vorher_y:
        setze rot_y auf nicht rot_y
    setze vorher_y auf y_jetzt

    setze z_jetzt auf taste_gedrückt("z")
    wenn z_jetzt und nicht vorher_z:
        setze rot_z auf nicht rot_z
    setze vorher_z auf z_jetzt

    wenn taste_gedrückt("links"):
        setze ay auf ay - 0.05
    wenn taste_gedrückt("rechts"):
        setze ay auf ay + 0.05
    wenn taste_gedrückt("oben"):
        setze ax auf ax - 0.05
    wenn taste_gedrückt("unten"):
        setze ax auf ax + 0.05

    wenn rot_x:
        setze ax auf ax + 0.015
    wenn rot_y:
        setze ay auf ay + 0.02
    wenn rot_z:
        setze az auf az + 0.015

    # --- Render ---
    setze mesh auf modelle[idx]
    setze proj auf transform_mesh(mesh, ax, ay, az)

    fenster_löschen(fenster, "schwarz")
    zeichne_mesh(fenster, mesh, proj)

    zeichne_text(fenster, namen[idx], 10, 10, 4, "weiss")
    zeichne_text(fenster, "FPS: " + text(fps), 10, 50, 3, "gelb")
    setze anz_tris auf boden(mesh["tris"].länge() / 3)
    zeichne_text(fenster, "TRIS: " + text(anz_tris), 10, 75, 3, "gelb")

    fenster_aktualisieren(fenster)

    # FPS
    setze frame_count auf frame_count + 1
    setze verstrichen auf zeit_ms() - fps_start
    wenn verstrichen >= 1000:
        setze fps auf boden(frame_count * 1000 / verstrichen)
        zeige "FPS=" + text(fps) + " model=" + namen[idx] + " tris=" + text(anz_tris)
        setze frame_count auf 0
        setze fps_start auf zeit_ms()

zeige "Wireframe-Renderer beendet"

# ============================================================
# moo Physik-Simulation — 2D Rigid-Body mit Kreisen
#
# Kompilieren: moo-compiler compile physik.moo -o physik
# Starten:     ./physik
#
# Bedienung:
#   Klick+Drag auf leere Flaeche → neuer Kreis
#     (Vektor von Klick zu Loslassen = Initialgeschwindigkeit)
#   RESET Knopf → alle Kreise entfernen, Standard-Set neu laden
#
# Features:
#   * Elastische Kreis-Kreis-Kollisionen (Momentum-Erhaltung)
#   * Kreis-Wand-Reflexion
#   * Gravitation nach unten
#   * Euler-Integration mit fester Tick-Rate
#   * FPS-Anzeige (zeit_ms)
# ============================================================

konstante BREITE auf 800
konstante HOEHE auf 600
konstante GRAVITATION auf 600.0
konstante RESTITUTION auf 0.88
konstante DAMPING auf 0.999
konstante DT auf 0.016
konstante FARBEN auf ["rot", "gruen", "blau", "gelb", "magenta", "orange", "weiss"]

# --- 3x5 Bitmap-Font (nur was wir brauchen) ---
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
FONT["F"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,0,0]
FONT["P"] = [1,1,1, 1,0,1, 1,1,1, 1,0,0, 1,0,0]
FONT["S"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
FONT["R"] = [1,1,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1]
FONT["E"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1]
FONT["T"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,0]
FONT["N"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT[":"] = [0,0,0, 0,1,0, 0,0,0, 0,1,0, 0,0,0]
FONT[" "] = [0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0]

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

# --- Kreis-Klasse ---
klasse Kreis:
    funktion erstelle(x, y, vx, vy, r, m, farbe):
        selbst.x = x
        selbst.y = y
        selbst.vx = vx
        selbst.vy = vy
        selbst.r = r
        selbst.m = m
        selbst.farbe = farbe

# --- Simulation ---

funktion standard_set():
    setze liste auf []
    setze i auf 0
    solange i < 20:
        setze rad auf 15 + boden(zufall() * 20)
        setze x auf rad + zufall() * (BREITE - 2 * rad)
        setze y auf rad + zufall() * (HOEHE / 2)
        setze vx auf (zufall() - 0.5) * 200
        setze vy auf (zufall() - 0.5) * 100
        setze masse auf rad * rad * 0.01
        setze farbe auf FARBEN[boden(zufall() * 7)]
        liste.hinzufügen(neu Kreis(x, y, vx, vy, rad, masse, farbe))
        setze i auf i + 1
    gib_zurück liste

funktion update(kreise, dt):
    # Gravitation + Integration
    setze i auf 0
    setze n auf kreise.länge()
    solange i < n:
        setze k auf kreise[i]
        k.vy = k.vy + GRAVITATION * dt
        k.vx = k.vx * DAMPING
        k.vy = k.vy * DAMPING
        k.x = k.x + k.vx * dt
        k.y = k.y + k.vy * dt
        setze i auf i + 1

    # Wand-Kollision
    setze i auf 0
    solange i < n:
        setze k auf kreise[i]
        wenn k.x - k.r < 0:
            k.x = k.r
            k.vx = -k.vx * RESTITUTION
        wenn k.x + k.r > BREITE:
            k.x = BREITE - k.r
            k.vx = -k.vx * RESTITUTION
        wenn k.y - k.r < 0:
            k.y = k.r
            k.vy = -k.vy * RESTITUTION
        wenn k.y + k.r > HOEHE:
            k.y = HOEHE - k.r
            k.vy = -k.vy * RESTITUTION
        setze i auf i + 1

    # Kreis-Kreis-Kollision (O(n²))
    setze i auf 0
    solange i < n:
        setze j auf i + 1
        solange j < n:
            setze a auf kreise[i]
            setze b auf kreise[j]
            setze dx auf b.x - a.x
            setze dy auf b.y - a.y
            setze dist2 auf dx * dx + dy * dy
            setze minD auf a.r + b.r
            wenn dist2 < minD * minD und dist2 > 0.01:
                setze dist auf wurzel(dist2)
                setze nx auf dx / dist
                setze ny auf dy / dist

                # Relativgeschwindigkeit entlang der Normalen
                setze rvx auf b.vx - a.vx
                setze rvy auf b.vy - a.vy
                setze vrn auf rvx * nx + rvy * ny

                wenn vrn < 0:
                    # Impuls
                    setze e auf RESTITUTION
                    setze jj auf -(1 + e) * vrn / (1 / a.m + 1 / b.m)
                    setze ix auf jj * nx
                    setze iy auf jj * ny
                    a.vx = a.vx - ix / a.m
                    a.vy = a.vy - iy / a.m
                    b.vx = b.vx + ix / b.m
                    b.vy = b.vy + iy / b.m

                # Positionskorrektur
                setze overlap auf (minD - dist) / 2
                a.x = a.x - overlap * nx
                a.y = a.y - overlap * ny
                b.x = b.x + overlap * nx
                b.y = b.y + overlap * ny
            setze j auf j + 1
        setze i auf i + 1

funktion zeichne_alle(fenster, kreise):
    setze i auf 0
    setze n auf kreise.länge()
    solange i < n:
        setze k auf kreise[i]
        zeichne_kreis(fenster, boden(k.x), boden(k.y), boden(k.r), k.farbe)
        setze i auf i + 1

funktion im_rect(mx, my, x, y, b, h):
    gib_zurück mx >= x und mx < x + b und my >= y und my < y + h

# --- Main ---
zeige "=== moo Physik-Simulation ==="
zeige "20 Kreise, Gravitation, elastische Kollisionen"

setze kreise auf standard_set()
setze fenster auf fenster_erstelle("moo Physik", BREITE, HOEHE)

# FPS-Tracking
setze fps auf 0
setze frame_count auf 0
setze fps_start auf zeit_ms()

# Drag-State
setze drag_aktiv auf falsch
setze drag_start_x auf 0
setze drag_start_y auf 0
setze vorher_maus auf falsch

# Reset-Button
konstante BTN_X auf 700
konstante BTN_Y auf 10
konstante BTN_B auf 90
konstante BTN_H auf 30

solange fenster_offen(fenster):
    setze frame_start auf zeit_ms()

    # --- Input ---
    setze mx auf maus_x(fenster)
    setze my auf maus_y(fenster)
    setze jetzt auf maus_gedrückt(fenster)

    wenn jetzt und nicht vorher_maus:
        # Button zuerst
        wenn im_rect(mx, my, BTN_X, BTN_Y, BTN_B, BTN_H):
            setze kreise auf standard_set()
        sonst:
            setze drag_aktiv auf wahr
            setze drag_start_x auf mx
            setze drag_start_y auf my

    wenn nicht jetzt und vorher_maus und drag_aktiv:
        # Loslassen → Kreis spawnen
        setze vx auf (drag_start_x - mx) * 3.0
        setze vy auf (drag_start_y - my) * 3.0
        setze rad auf 20
        setze masse auf rad * rad * 0.01
        setze farbe auf FARBEN[boden(zufall() * 7)]
        kreise.hinzufügen(neu Kreis(drag_start_x, drag_start_y, vx, vy, rad, masse, farbe))
        setze drag_aktiv auf falsch

    setze vorher_maus auf jetzt

    # --- Update ---
    update(kreise, DT)

    # --- Render ---
    fenster_löschen(fenster, "schwarz")
    zeichne_alle(fenster, kreise)

    # Drag-Vorschau
    wenn drag_aktiv:
        zeichne_linie(fenster, drag_start_x, drag_start_y, mx, my, "weiss")
        zeichne_kreis(fenster, drag_start_x, drag_start_y, 20, "grau")

    # Reset-Button
    zeichne_rechteck(fenster, BTN_X, BTN_Y, BTN_B, BTN_H, "rot")
    zeichne_text(fenster, "RESET", BTN_X + 10, BTN_Y + 8, 3, "weiss")

    # FPS-Anzeige
    zeichne_text(fenster, "FPS: " + text(fps), 10, 10, 3, "weiss")
    # Anzahl Kreise
    zeichne_text(fenster, "N: " + text(kreise.länge()), 10, 35, 3, "weiss")

    fenster_aktualisieren(fenster)

    # --- Frame-Timing ---
    setze frame_count auf frame_count + 1
    setze verstrichen auf zeit_ms() - fps_start
    wenn verstrichen >= 1000:
        setze fps auf boden(frame_count * 1000 / verstrichen)
        setze frame_count auf 0
        setze fps_start auf zeit_ms()

    # SDL2 vsync limitiert bereits auf Monitor-Rate; kein manuelles Schlafen noetig

zeige "Physik-Simulation beendet"

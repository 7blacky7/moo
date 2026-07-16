# Grafik-Demo Showcase — zeigt alle Zeichenfunktionen von moo
# Jede Demo laeuft ~3 Sekunden, dann kommt die naechste

setze fenster auf fenster_erstelle("Moo Grafik-Demo", 800, 600)

# ===== Demo 1: Regenbogen-Kreise =====
fenster_löschen(fenster, "schwarz")

# 7 Kreise uebereinander, groesster zuerst
zeichne_kreis(fenster, 400, 300, 200, "rot")
zeichne_kreis(fenster, 400, 300, 170, "orange")
zeichne_kreis(fenster, 400, 300, 140, "gelb")
zeichne_kreis(fenster, 400, 300, 110, "gruen")
zeichne_kreis(fenster, 400, 300, 80, "cyan")
zeichne_kreis(fenster, 400, 300, 50, "blau")
zeichne_kreis(fenster, 400, 300, 20, "magenta")

fenster_aktualisieren(fenster)
warte(3000)

# ===== Demo 2: Schachbrett =====
fenster_löschen(fenster, "schwarz")

setze feld auf 75
setze zeile auf 0
solange zeile < 8:
    setze spalte auf 0
    solange spalte < 8:
        setze x auf spalte * feld
        setze y auf zeile * feld
        # Abwechselnd weiss/grau
        wenn (zeile + spalte) % 2 == 0:
            zeichne_rechteck(fenster, x, y, feld, feld, "weiss")
        sonst:
            zeichne_rechteck(fenster, x, y, feld, feld, "grau")
        setze spalte auf spalte + 1
    setze zeile auf zeile + 1

fenster_aktualisieren(fenster)
warte(3000)

# ===== Demo 3: Spirale aus Punkten =====
fenster_löschen(fenster, "schwarz")

# Spirale: Annaeherung an sin/cos ueber Kreisbewegung
# Wir nutzen viele kleine Schritte mit dx/dy Rotation
setze cx auf 400.0
setze cy auf 300.0
setze px auf 400.0
setze py auf 300.0
setze winkel_x auf 1.0
setze winkel_y auf 0.0
setze schritt auf 0
solange schritt < 2000:
    # Radius waechst mit jedem Schritt
    setze r auf schritt * 0.1

    # Rotation: cos/sin Annaeherung ueber kleine Drehung (0.1 rad pro Schritt)
    # Neue Richtung = alte * cos(0.1) - sin(0.1) / cos(0.1) + sin(0.1)
    # cos(0.1) ~ 0.995, sin(0.1) ~ 0.0998
    setze neu_x auf winkel_x * 0.995 - winkel_y * 0.0998
    setze neu_y auf winkel_x * 0.0998 + winkel_y * 0.995
    setze winkel_x auf neu_x
    setze winkel_y auf neu_y

    setze px auf cx + r * winkel_x
    setze py auf cy + r * winkel_y

    # Farbe wechselt: abwechselnd cyan und magenta
    wenn schritt % 20 < 10:
        zeichne_pixel(fenster, px, py, "cyan")
    sonst:
        zeichne_pixel(fenster, px, py, "magenta")

    setze schritt auf schritt + 1

fenster_aktualisieren(fenster)
warte(3000)

# ===== Demo 4: Bouncing Ball =====
setze bx auf 400.0
setze by auf 300.0
setze bdx auf 4.0
setze bdy auf 3.0
setze brad auf 20

setze frame auf 0
solange frame < 200:
    wenn fenster_offen(fenster) == falsch:
        stopp

    fenster_löschen(fenster, "schwarz")

    # Ball bewegen
    setze bx auf bx + bdx
    setze by auf by + bdy

    # Wand-Kollision
    wenn bx - brad < 0:
        setze bdx auf 0 - bdx
        setze bx auf brad
    wenn bx + brad > 800:
        setze bdx auf 0 - bdx
        setze bx auf 800 - brad
    wenn by - brad < 0:
        setze bdy auf 0 - bdy
        setze by auf brad
    wenn by + brad > 600:
        setze bdy auf 0 - bdy
        setze by auf 600 - brad

    # Spur zeichnen (Linie von Mitte zum Ball)
    zeichne_linie(fenster, 400, 300, bx, by, "grau")

    # Ball zeichnen
    zeichne_kreis(fenster, bx, by, brad, "gelb")

    # Rahmen
    zeichne_linie(fenster, 0, 0, 799, 0, "weiss")
    zeichne_linie(fenster, 799, 0, 799, 599, "weiss")
    zeichne_linie(fenster, 799, 599, 0, 599, "weiss")
    zeichne_linie(fenster, 0, 599, 0, 0, "weiss")

    fenster_aktualisieren(fenster)
    warte(16)

    setze frame auf frame + 1

# ===== Demo 5: Farbverlauf =====
fenster_löschen(fenster, "schwarz")

# Horizontaler Verlauf: 800 Rechtecke, je 1px breit
# Von rot (#FF0000) ueber gelb (#FFFF00) zu gruen (#00FF00) zu cyan (#00FFFF) zu blau (#0000FF)
# Wir nutzen hex-Farben in 4 Segmenten a 200px
setze x auf 0
solange x < 800:
    # Segment bestimmen (0-199, 200-399, 400-599, 600-799)
    wenn x < 200:
        # Rot → Gelb: R=FF, G steigt, B=00
        zeichne_rechteck(fenster, x, 0, 1, 600, "rot")
    sonst wenn x < 400:
        # Gelb → Gruen
        zeichne_rechteck(fenster, x, 0, 1, 600, "gelb")
    sonst wenn x < 600:
        # Gruen → Cyan
        zeichne_rechteck(fenster, x, 0, 1, 600, "cyan")
    sonst:
        # Cyan → Blau
        zeichne_rechteck(fenster, x, 0, 1, 600, "blau")

    setze x auf x + 1

# Overlay-Text als Rechtecke (MOO in Bloecken)
# M
zeichne_rechteck(fenster, 250, 200, 20, 200, "weiss")
zeichne_rechteck(fenster, 270, 200, 20, 20, "weiss")
zeichne_rechteck(fenster, 290, 220, 20, 20, "weiss")
zeichne_rechteck(fenster, 310, 200, 20, 20, "weiss")
zeichne_rechteck(fenster, 330, 200, 20, 200, "weiss")

# O
zeichne_rechteck(fenster, 370, 200, 20, 200, "weiss")
zeichne_rechteck(fenster, 390, 200, 40, 20, "weiss")
zeichne_rechteck(fenster, 430, 200, 20, 200, "weiss")
zeichne_rechteck(fenster, 390, 380, 40, 20, "weiss")

# O
zeichne_rechteck(fenster, 470, 200, 20, 200, "weiss")
zeichne_rechteck(fenster, 490, 200, 40, 20, "weiss")
zeichne_rechteck(fenster, 530, 200, 20, 200, "weiss")
zeichne_rechteck(fenster, 490, 380, 40, 20, "weiss")

fenster_aktualisieren(fenster)
warte(3000)

# Aufraeumen
fenster_schliessen(fenster)

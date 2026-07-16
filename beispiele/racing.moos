# ============================================================
# moo Racing — Top-Down Rennspiel
#
# Kompilieren: moo-compiler compile beispiele/racing.moo -o beispiele/racing
# Starten:     ./beispiele/racing
#
# Bedienung:
#   Hoch/W - Beschleunigen
#   Runter/S - Bremsen
#   Links/A, Rechts/D - Lenken
#   Escape - Beenden
#
# Features: Top-Down Strecke, Geschwindigkeit, Drift, Runden, Hindernisse
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600

# Spieler-Auto
setze auto_x auf 400.0
setze auto_y auf 500.0
setze auto_winkel auf 0.0
setze auto_speed auf 0.0
setze MAX_SPEED auf 6.0
setze BESCHLEUNIGUNG auf 0.15
setze BREMSE auf 0.1
setze REIBUNG auf 0.02
setze LENKUNG auf 0.04

# Strecke (Checkpoints als Kreise)
setze cp_x auf []
setze cp_y auf []
setze cp_radius auf []

funktion checkpoint(x, y, r):
    cp_x.hinzufügen(x)
    cp_y.hinzufügen(y)
    cp_radius.hinzufügen(r)

# Ovale Strecke
checkpoint(400, 100, 80)
checkpoint(650, 150, 80)
checkpoint(700, 300, 80)
checkpoint(650, 450, 80)
checkpoint(400, 500, 80)
checkpoint(150, 450, 80)
checkpoint(100, 300, 80)
checkpoint(150, 150, 80)

setze cp_anzahl auf länge(cp_x)
setze naechster_cp auf 0
setze runden auf 0
setze max_runden auf 3
setze zeit auf 0

# Hindernisse (Oelflecken)
setze oel_x auf []
setze oel_y auf []
funktion oelfleck(x, y):
    oel_x.hinzufügen(x)
    oel_y.hinzufügen(y)

oelfleck(500, 120)
oelfleck(680, 250)
oelfleck(550, 480)
oelfleck(200, 400)
oelfleck(120, 200)
oelfleck(350, 300)

setze oel_anzahl auf länge(oel_x)
setze auf_oel auf 0

# KI-Gegner
setze ki_x auf 380.0
setze ki_y auf 500.0
setze ki_winkel auf 0.0
setze ki_speed auf 3.5
setze ki_cp auf 0

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Racing", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn runden >= max_runden:
        stopp

    # === INPUT ===
    wenn taste_gedrückt("oben") oder taste_gedrückt("w"):
        setze auto_speed auf auto_speed + BESCHLEUNIGUNG
    wenn taste_gedrückt("unten") oder taste_gedrückt("s"):
        setze auto_speed auf auto_speed - BREMSE

    setze lenk_faktor auf LENKUNG
    wenn auf_oel > 0:
        setze lenk_faktor auf LENKUNG * 0.3
    wenn taste_gedrückt("links") oder taste_gedrückt("a"):
        setze auto_winkel auf auto_winkel - lenk_faktor * auto_speed
    wenn taste_gedrückt("rechts") oder taste_gedrückt("d"):
        setze auto_winkel auf auto_winkel + lenk_faktor * auto_speed

    # Reibung
    wenn auto_speed > 0:
        setze auto_speed auf auto_speed - REIBUNG
    wenn auto_speed < 0:
        setze auto_speed auf auto_speed + REIBUNG
    wenn auto_speed > MAX_SPEED:
        setze auto_speed auf MAX_SPEED
    wenn auto_speed < -2.0:
        setze auto_speed auf -2.0
    wenn auto_speed > -0.05 und auto_speed < 0.05:
        setze auto_speed auf 0.0

    # Bewegung
    setze auto_x auf auto_x + sinus(auto_winkel) * auto_speed
    setze auto_y auf auto_y - cosinus(auto_winkel) * auto_speed

    # Bildschirm-Grenzen
    wenn auto_x < 20:
        setze auto_x auf 20.0
        setze auto_speed auf auto_speed * 0.5
    wenn auto_x > BREITE - 20:
        setze auto_x auf (BREITE - 20) * 1.0
        setze auto_speed auf auto_speed * 0.5
    wenn auto_y < 20:
        setze auto_y auf 20.0
        setze auto_speed auf auto_speed * 0.5
    wenn auto_y > HOEHE - 20:
        setze auto_y auf (HOEHE - 20) * 1.0
        setze auto_speed auf auto_speed * 0.5

    # Oel-Check
    wenn auf_oel > 0:
        setze auf_oel auf auf_oel - 1
    setze oi auf 0
    solange oi < oel_anzahl:
        setze odx auf auto_x - oel_x[oi]
        setze ody auf auto_y - oel_y[oi]
        wenn (odx * odx + ody * ody) < 900:
            setze auf_oel auf 30
        setze oi auf oi + 1

    # Checkpoint-Check
    setze cdx auf auto_x - cp_x[naechster_cp]
    setze cdy auf auto_y - cp_y[naechster_cp]
    setze cp_dist auf cdx * cdx + cdy * cdy
    setze cp_r auf cp_radius[naechster_cp]
    wenn cp_dist < cp_r * cp_r:
        setze naechster_cp auf naechster_cp + 1
        wenn naechster_cp >= cp_anzahl:
            setze naechster_cp auf 0
            setze runden auf runden + 1

    # KI-Gegner bewegen (folgt Checkpoints)
    setze ki_ziel_x auf cp_x[ki_cp]
    setze ki_ziel_y auf cp_y[ki_cp]
    setze ki_dx auf ki_ziel_x - ki_x
    setze ki_dy auf ki_ziel_y - ki_y
    setze ki_dist auf sqrt(ki_dx * ki_dx + ki_dy * ki_dy)
    wenn ki_dist > 1:
        setze ki_x auf ki_x + ki_dx / ki_dist * ki_speed
        setze ki_y auf ki_y + ki_dy / ki_dist * ki_speed
    wenn ki_dist < 50:
        setze ki_cp auf ki_cp + 1
        wenn ki_cp >= cp_anzahl:
            setze ki_cp auf 0

    setze zeit auf zeit + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#2E7D32")

    # Strecke (Checkpoints als Kreise)
    setze ci auf 0
    solange ci < cp_anzahl:
        # Streckensegment zum naechsten CP
        setze ni auf ci + 1
        wenn ni >= cp_anzahl:
            setze ni auf 0
        zeichne_linie(win, cp_x[ci], cp_y[ci], cp_x[ni], cp_y[ni], "#5D4037")
        # Breitere Strecke simulieren
        setze ci auf ci + 1

    # Checkpoint-Markierungen
    setze ci auf 0
    solange ci < cp_anzahl:
        wenn ci == naechster_cp:
            zeichne_kreis(win, cp_x[ci], cp_y[ci], 15, "#FFEB3B")
        sonst:
            zeichne_kreis(win, cp_x[ci], cp_y[ci], 10, "#8D6E63")
        setze ci auf ci + 1

    # Oelflecken
    setze oi auf 0
    solange oi < oel_anzahl:
        zeichne_kreis(win, oel_x[oi], oel_y[oi], 15, "#1A1A1A")
        zeichne_kreis(win, oel_x[oi], oel_y[oi], 10, "#333333")
        setze oi auf oi + 1

    # KI-Auto (rot)
    zeichne_rechteck(win, ki_x - 10, ki_y - 15, 20, 30, "#F44336")
    zeichne_rechteck(win, ki_x - 8, ki_y - 12, 16, 6, "#FFCDD2")

    # Spieler-Auto
    setze draw_x auf auto_x - 10
    setze draw_y auf auto_y - 15
    wenn auf_oel > 0:
        zeichne_rechteck(win, draw_x, draw_y, 20, 30, "#FF9800")
    sonst:
        zeichne_rechteck(win, draw_x, draw_y, 20, 30, "#2196F3")
    # Windschutzscheibe
    zeichne_rechteck(win, draw_x + 2, draw_y + 3, 16, 6, "#90CAF9")

    # Speed-Anzeige (Balken unten)
    setze speed_w auf abs(auto_speed) * 40
    zeichne_rechteck(win, 10, HOEHE - 25, 250, 15, "#424242")
    zeichne_rechteck(win, 10, HOEHE - 25, speed_w, 15, "#4CAF50")

    # Runden-Anzeige
    setze ri auf 0
    solange ri < max_runden:
        wenn ri < runden:
            zeichne_kreis(win, BREITE - 30 - ri * 25, 20, 8, "#FFD700")
        sonst:
            zeichne_kreis(win, BREITE - 30 - ri * 25, 20, 8, "#616161")
        setze ri auf ri + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Rennen beendet! Runden: " + text(runden) + " Zeit: " + text(zeit / 60) + "s"

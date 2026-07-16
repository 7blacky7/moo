# ============================================================
# moo Rhythm — Guitar Hero Style
#
# Kompilieren: moo-compiler compile beispiele/rhythm.moo -o beispiele/rhythm
# Starten:     ./beispiele/rhythm
#
# A/S/D/F = 4 Lanes treffen
# Escape = Beenden
# ============================================================

setze BREITE auf 400
setze HOEHE auf 600
setze LANES auf 4
setze LANE_W auf 80
setze NOTE_H auf 20
setze HIT_Y auf 530
setze MAX_NOTEN auf 60
setze NOTE_SPEED auf 3

# Noten
setze note_lane auf []
setze note_y auf []
setze note_aktiv auf []
setze ni auf 0
solange ni < MAX_NOTEN:
    note_lane.hinzufügen(0)
    note_y.hinzufügen(0.0)
    note_aktiv.hinzufügen(falsch)
    setze ni auf ni + 1

# Song generieren (prozedural)
setze seed auf 77
setze ni auf 0
solange ni < MAX_NOTEN:
    setze seed auf (seed * 1103515245 + 12345) % 2147483648
    setze note_lane[ni] auf seed % LANES
    setze note_y[ni] auf (0 - ni * 50 - 100) * 1.0
    setze note_aktiv[ni] auf wahr
    setze ni auf ni + 1

setze punkte auf 0
setze combo auf 0
setze max_combo auf 0
setze verpasst auf 0
setze frame auf 0

funktion lane_farbe(lane):
    wenn lane == 0:
        gib_zurück "#F44336"
    wenn lane == 1:
        gib_zurück "#4CAF50"
    wenn lane == 2:
        gib_zurück "#2196F3"
    gib_zurück "#FF9800"

funktion lane_taste(lane):
    wenn lane == 0:
        gib_zurück "a"
    wenn lane == 1:
        gib_zurück "s"
    wenn lane == 2:
        gib_zurück "d"
    gib_zurück "f"

# Hit-Effekte
setze hit_flash auf []
setze hi auf 0
solange hi < LANES:
    hit_flash.hinzufügen(0)
    setze hi auf hi + 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Rhythm", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    setze frame auf frame + 1

    # Noten bewegen
    setze ni auf 0
    solange ni < MAX_NOTEN:
        wenn note_aktiv[ni]:
            setze note_y[ni] auf note_y[ni] + NOTE_SPEED
            # Verpasst
            wenn note_y[ni] > HIT_Y + 40:
                setze note_aktiv[ni] auf falsch
                setze verpasst auf verpasst + 1
                setze combo auf 0
        setze ni auf ni + 1

    # Input: Lanes treffen
    setze li auf 0
    solange li < LANES:
        setze taste auf lane_taste(li)
        wenn taste_gedrückt(taste):
            # Naechste Note in dieser Lane finden
            setze ni auf 0
            setze getroffen auf falsch
            solange ni < MAX_NOTEN:
                wenn note_aktiv[ni] und note_lane[ni] == li und nicht getroffen:
                    setze dist auf note_y[ni] - HIT_Y
                    wenn dist < 0:
                        setze dist auf 0 - dist
                    wenn dist < 30:
                        setze note_aktiv[ni] auf falsch
                        setze punkte auf punkte + 10 + combo
                        setze combo auf combo + 1
                        wenn combo > max_combo:
                            setze max_combo auf combo
                        setze hit_flash[li] auf 8
                        setze getroffen auf wahr
                setze ni auf ni + 1
        setze li auf li + 1

    # Flash-Timer
    setze li auf 0
    solange li < LANES:
        wenn hit_flash[li] > 0:
            setze hit_flash[li] auf hit_flash[li] - 1
        setze li auf li + 1

    # Alle Noten vorbei?
    setze alle_weg auf wahr
    setze ni auf 0
    solange ni < MAX_NOTEN:
        wenn note_aktiv[ni]:
            setze alle_weg auf falsch
        setze ni auf ni + 1
    wenn alle_weg und frame > 100:
        stopp

    # === ZEICHNEN ===
    fenster_löschen(win, "#1A1A2E")

    # Lanes
    setze li auf 0
    solange li < LANES:
        setze lx auf 20 + li * LANE_W
        # Lane-Hintergrund
        zeichne_rechteck(win, lx, 0, LANE_W - 5, HOEHE, "#16213E")
        # Lane-Linie
        zeichne_linie(win, lx + LANE_W - 5, 0, lx + LANE_W - 5, HOEHE, "#0F3460")

        # Hit-Zone
        setze lf auf lane_farbe(li)
        wenn hit_flash[li] > 0:
            zeichne_rechteck(win, lx, HIT_Y - 5, LANE_W - 5, 15, "#FFFFFF")
        sonst:
            zeichne_rechteck(win, lx + 5, HIT_Y, LANE_W - 15, 5, lf)
        setze li auf li + 1

    # Noten
    setze ni auf 0
    solange ni < MAX_NOTEN:
        wenn note_aktiv[ni]:
            setze ny auf note_y[ni]
            wenn ny > -NOTE_H und ny < HOEHE:
                setze lx auf 20 + note_lane[ni] * LANE_W
                setze nf auf lane_farbe(note_lane[ni])
                zeichne_rechteck(win, lx + 5, ny, LANE_W - 15, NOTE_H, nf)
                # Glanz
                zeichne_rechteck(win, lx + 8, ny + 2, LANE_W - 21, 4, "#FFFFFF")
        setze ni auf ni + 1

    # HUD
    # Combo
    setze ci auf 0
    solange ci < combo und ci < 20:
        zeichne_kreis(win, BREITE / 2 - combo * 5 + ci * 10, 20, 4, "#FFD700")
        setze ci auf ci + 1

    # Score
    setze si auf 0
    solange si < punkte / 20 und si < 30:
        zeichne_rechteck(win, 10 + si * 6, HOEHE - 15, 4, 8, "#4CAF50")
        setze si auf si + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Song vorbei! Punkte: " + text(punkte) + " Max Combo: " + text(max_combo) + " Verpasst: " + text(verpasst)

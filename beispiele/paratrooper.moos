# ============================================================
# moo Paratrooper — Turm-Verteidigung gegen Fallschirmspringer
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/paratrooper.moo -o beispiele/paratrooper
#   ./beispiele/paratrooper
#
# Bedienung:
#   Links/Rechts - Turm drehen
#   Leertaste - Schiessen
#   Escape - Beenden
#
# Features:
#   * Hubschrauber fliegen links/rechts, werfen Fallschirmspringer ab
#   * Schiesse Helikopter oder Fallschirmspringer
#   * Fallschirmspringer am Boden formen Leiter → wenn 4 auf einer Seite
#     den Turm angreifen, ist das Spiel vorbei
#   * Score + Wellen
# ============================================================

setze BREITE auf 700
setze HOEHE auf 500
setze TURM_X auf BREITE / 2
setze TURM_Y auf HOEHE - 40
setze MAX_HELI auf 6
setze MAX_PARA auf 20
setze MAX_SCHUSS auf 20
setze MAX_EXPL auf 15

setze rng_state auf 71717
funktion rng():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    wenn rng_state < 0:
        setze rng_state auf 0 - rng_state
    gib_zurück rng_state

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

# Turm
setze turm_winkel auf 90.0
setze feuer_cd auf 0

# Helikopter
setze heli_x auf []
setze heli_y auf []
setze heli_dx auf []
setze heli_aktiv auf []
setze heli_drop_cd auf []
setze hi auf 0
solange hi < MAX_HELI:
    heli_x.hinzufügen(0.0)
    heli_y.hinzufügen(0.0)
    heli_dx.hinzufügen(0.0)
    heli_aktiv.hinzufügen(falsch)
    heli_drop_cd.hinzufügen(0)
    setze hi auf hi + 1

# Paratrooper
setze para_x auf []
setze para_y auf []
setze para_vy auf []
setze para_landed auf []
setze para_gehen auf []
setze para_aktiv auf []
setze pi auf 0
solange pi < MAX_PARA:
    para_x.hinzufügen(0.0)
    para_y.hinzufügen(0.0)
    para_vy.hinzufügen(0.0)
    para_landed.hinzufügen(falsch)
    para_gehen.hinzufügen(1)
    para_aktiv.hinzufügen(falsch)
    setze pi auf pi + 1

# Schuesse
setze sch_x auf []
setze sch_y auf []
setze sch_vx auf []
setze sch_vy auf []
setze sch_aktiv auf []
setze si auf 0
solange si < MAX_SCHUSS:
    sch_x.hinzufügen(0.0)
    sch_y.hinzufügen(0.0)
    sch_vx.hinzufügen(0.0)
    sch_vy.hinzufügen(0.0)
    sch_aktiv.hinzufügen(falsch)
    setze si auf si + 1

# Explosionen
setze ex_x auf []
setze ex_y auf []
setze ex_t auf []
setze ex_aktiv auf []
setze ei auf 0
solange ei < MAX_EXPL:
    ex_x.hinzufügen(0.0)
    ex_y.hinzufügen(0.0)
    ex_t.hinzufügen(0)
    ex_aktiv.hinzufügen(falsch)
    setze ei auf ei + 1

setze score auf 0
setze welle auf 1
setze spawn_cd auf 60

funktion explosion(x, y):
    setze ei auf 0
    solange ei < MAX_EXPL:
        wenn nicht ex_aktiv[ei]:
            ex_x[ei] = x
            ex_y[ei] = y
            ex_t[ei] = 15
            ex_aktiv[ei] = wahr
            gib_zurück nichts
        setze ei auf ei + 1

funktion heli_spawn():
    setze hi auf 0
    solange hi < MAX_HELI:
        wenn nicht heli_aktiv[hi]:
            wenn rng() % 2 == 0:
                heli_x[hi] = -30.0
                heli_dx[hi] = 1.0 + welle * 0.1
            sonst:
                heli_x[hi] = (BREITE + 30) * 1.0
                heli_dx[hi] = 0 - (1.0 + welle * 0.1)
            heli_y[hi] = 40 + rng() % 80
            heli_aktiv[hi] = wahr
            heli_drop_cd[hi] = 60 + rng() % 60
            gib_zurück nichts
        setze hi auf hi + 1

funktion para_spawn(x, y):
    setze pi auf 0
    solange pi < MAX_PARA:
        wenn nicht para_aktiv[pi]:
            para_x[pi] = x
            para_y[pi] = y
            para_vy[pi] = 1.5
            para_landed[pi] = falsch
            para_gehen[pi] = 0
            para_aktiv[pi] = wahr
            gib_zurück nichts
        setze pi auf pi + 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Paratrooper", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Spiel vorbei: 4 Paratrooper erreichen Turm-Seite
    setze links_count auf 0
    setze rechts_count auf 0
    setze pi auf 0
    solange pi < MAX_PARA:
        wenn para_aktiv[pi] und para_landed[pi]:
            wenn para_x[pi] < TURM_X:
                setze links_count auf links_count + 1
            sonst:
                setze rechts_count auf rechts_count + 1
        setze pi auf pi + 1
    wenn links_count >= 4 oder rechts_count >= 4:
        # Check ob ein Paratrooper den Turm erreicht
        setze pi auf 0
        solange pi < MAX_PARA:
            wenn para_aktiv[pi] und para_landed[pi]:
                setze dist auf abs_wert(para_x[pi] - TURM_X)
                wenn dist < 30:
                    stopp
            setze pi auf pi + 1

    # === INPUT ===
    wenn taste_gedrückt("links"):
        setze turm_winkel auf turm_winkel + 2
        wenn turm_winkel > 170:
            setze turm_winkel auf 170.0
    wenn taste_gedrückt("rechts"):
        setze turm_winkel auf turm_winkel - 2
        wenn turm_winkel < 10:
            setze turm_winkel auf 10.0

    wenn taste_gedrückt("leertaste") und feuer_cd <= 0:
        setze si auf 0
        solange si < MAX_SCHUSS:
            wenn nicht sch_aktiv[si]:
                setze rad auf turm_winkel * 3.14159 / 180.0
                sch_x[si] = TURM_X * 1.0
                sch_y[si] = (TURM_Y - 20) * 1.0
                sch_vx[si] = cosinus(rad) * 10.0
                sch_vy[si] = 0 - sinus(rad) * 10.0
                sch_aktiv[si] = wahr
                setze feuer_cd auf 8
                setze si auf MAX_SCHUSS
            setze si auf si + 1
    wenn feuer_cd > 0:
        setze feuer_cd auf feuer_cd - 1

    # === HELI SPAWNING ===
    setze spawn_cd auf spawn_cd - 1
    wenn spawn_cd <= 0:
        heli_spawn()
        setze spawn_cd auf 120 - welle * 10
        wenn spawn_cd < 40:
            setze spawn_cd auf 40

    # === HELI UPDATE ===
    setze hi auf 0
    solange hi < MAX_HELI:
        wenn heli_aktiv[hi]:
            heli_x[hi] = heli_x[hi] + heli_dx[hi]
            # Aus dem Bildschirm
            wenn heli_dx[hi] > 0 und heli_x[hi] > BREITE + 30:
                heli_aktiv[hi] = falsch
            wenn heli_dx[hi] < 0 und heli_x[hi] < -30:
                heli_aktiv[hi] = falsch
            # Paratrooper droppen
            heli_drop_cd[hi] = heli_drop_cd[hi] - 1
            wenn heli_drop_cd[hi] <= 0:
                wenn heli_x[hi] > 50 und heli_x[hi] < BREITE - 50:
                    para_spawn(heli_x[hi], heli_y[hi] + 15)
                heli_drop_cd[hi] = 80 + rng() % 60
        setze hi auf hi + 1

    # === PARATROOPER UPDATE ===
    setze pi auf 0
    solange pi < MAX_PARA:
        wenn para_aktiv[pi]:
            wenn nicht para_landed[pi]:
                para_y[pi] = para_y[pi] + para_vy[pi]
                wenn para_y[pi] >= HOEHE - 30:
                    para_y[pi] = (HOEHE - 30) * 1.0
                    para_landed[pi] = wahr
                    # Gehrichtung: Richtung Turm
                    wenn para_x[pi] < TURM_X:
                        para_gehen[pi] = 1
                    sonst:
                        para_gehen[pi] = -1
            sonst:
                # Gehen Richtung Turm (stoppen bei anderem Para)
                setze naechster auf 999.0
                setze pj auf 0
                solange pj < MAX_PARA:
                    wenn pj != pi und para_aktiv[pj] und para_landed[pj]:
                        # Gleiche Seite?
                        wenn (para_x[pi] < TURM_X) == (para_x[pj] < TURM_X):
                            setze dist auf abs_wert(para_x[pj] - para_x[pi])
                            setze zwischen auf falsch
                            wenn para_gehen[pi] > 0 und para_x[pj] > para_x[pi]:
                                setze zwischen auf wahr
                            wenn para_gehen[pi] < 0 und para_x[pj] < para_x[pi]:
                                setze zwischen auf wahr
                            wenn zwischen und dist < naechster:
                                setze naechster auf dist
                    setze pj auf pj + 1
                wenn naechster > 18:
                    # Auch Abstand zum Turm pruefen
                    setze dist_turm auf abs_wert(para_x[pi] - TURM_X)
                    wenn dist_turm > 20:
                        para_x[pi] = para_x[pi] + para_gehen[pi] * 0.5
        setze pi auf pi + 1

    # === SCHUESSE ===
    setze si auf 0
    solange si < MAX_SCHUSS:
        wenn sch_aktiv[si]:
            sch_x[si] = sch_x[si] + sch_vx[si]
            sch_y[si] = sch_y[si] + sch_vy[si]
            sch_vy[si] = sch_vy[si] + 0.1
            wenn sch_y[si] > HOEHE oder sch_x[si] < 0 oder sch_x[si] > BREITE:
                sch_aktiv[si] = falsch
        setze si auf si + 1

    # === TREFFER ===
    setze si auf 0
    solange si < MAX_SCHUSS:
        wenn sch_aktiv[si]:
            # Heli
            setze hi auf 0
            solange hi < MAX_HELI:
                wenn heli_aktiv[hi]:
                    wenn abs_wert(sch_x[si] - heli_x[hi]) < 20 und abs_wert(sch_y[si] - heli_y[hi]) < 10:
                        sch_aktiv[si] = falsch
                        heli_aktiv[hi] = falsch
                        explosion(heli_x[hi], heli_y[hi])
                        setze score auf score + 100
                setze hi auf hi + 1
            # Para
            wenn sch_aktiv[si]:
                setze pi auf 0
                solange pi < MAX_PARA:
                    wenn para_aktiv[pi]:
                        wenn abs_wert(sch_x[si] - para_x[pi]) < 10 und abs_wert(sch_y[si] - para_y[pi]) < 15:
                            sch_aktiv[si] = falsch
                            para_aktiv[pi] = falsch
                            explosion(para_x[pi], para_y[pi])
                            wenn para_landed[pi]:
                                setze score auf score + 25
                            sonst:
                                setze score auf score + 50
                    setze pi auf pi + 1
        setze si auf si + 1

    # Explosionen
    setze ei auf 0
    solange ei < MAX_EXPL:
        wenn ex_aktiv[ei]:
            ex_t[ei] = ex_t[ei] - 1
            wenn ex_t[ei] <= 0:
                ex_aktiv[ei] = falsch
        setze ei auf ei + 1

    # Welle-Steigerung
    wenn score > welle * 500:
        setze welle auf welle + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#87CEEB")
    zeichne_rechteck(win, 0, HOEHE / 2, BREITE, HOEHE / 2, "#B3E5FC")

    # Sonne
    zeichne_kreis(win, BREITE - 80, 80, 30, "#FFEB3B")
    zeichne_kreis(win, BREITE - 80, 80, 22, "#FFF59D")

    # Wolken
    setze w_idx auf 0
    solange w_idx < 3:
        setze wx auf 100 + w_idx * 250
        zeichne_kreis(win, wx, 50, 20, "#FFFFFF")
        zeichne_kreis(win, wx + 20, 50, 22, "#FFFFFF")
        zeichne_kreis(win, wx - 20, 55, 18, "#FFFFFF")
        setze w_idx auf w_idx + 1

    # Boden
    zeichne_rechteck(win, 0, HOEHE - 30, BREITE, 30, "#4CAF50")
    zeichne_rechteck(win, 0, HOEHE - 30, BREITE, 3, "#66BB6A")

    # Helikopter
    setze hi auf 0
    solange hi < MAX_HELI:
        wenn heli_aktiv[hi]:
            setze hx auf heli_x[hi]
            setze hy auf heli_y[hi]
            zeichne_rechteck(win, hx - 15, hy - 5, 30, 10, "#424242")
            zeichne_rechteck(win, hx - 8, hy - 12, 16, 7, "#616161")
            # Rotor
            zeichne_linie(win, hx - 20, hy - 14, hx + 20, hy - 14, "#000000")
            wenn heli_dx[hi] > 0:
                zeichne_rechteck(win, hx + 15, hy - 2, 8, 4, "#757575")
            sonst:
                zeichne_rechteck(win, hx - 23, hy - 2, 8, 4, "#757575")
        setze hi auf hi + 1

    # Paratrooper
    setze pi auf 0
    solange pi < MAX_PARA:
        wenn para_aktiv[pi]:
            setze px auf para_x[pi]
            setze py auf para_y[pi]
            wenn nicht para_landed[pi]:
                # Fallschirm
                zeichne_kreis(win, px, py - 15, 12, "#F44336")
                zeichne_linie(win, px - 10, py - 10, px - 2, py - 5, "#000000")
                zeichne_linie(win, px + 10, py - 10, px + 2, py - 5, "#000000")
            # Soldat
            zeichne_rechteck(win, px - 3, py - 5, 6, 8, "#4CAF50")
            zeichne_kreis(win, px, py - 10, 3, "#FFCC80")
            zeichne_rechteck(win, px - 2, py + 3, 2, 4, "#1B5E20")
            zeichne_rechteck(win, px + 1, py + 3, 2, 4, "#1B5E20")
        setze pi auf pi + 1

    # Turm
    zeichne_rechteck(win, TURM_X - 25, TURM_Y, 50, 30, "#616161")
    zeichne_rechteck(win, TURM_X - 20, TURM_Y + 3, 40, 24, "#757575")
    zeichne_kreis(win, TURM_X, TURM_Y - 5, 15, "#424242")
    # Kanone
    setze rad auf turm_winkel * 3.14159 / 180.0
    setze kx auf TURM_X + cosinus(rad) * 30
    setze ky auf TURM_Y - 5 - sinus(rad) * 30
    zeichne_linie(win, TURM_X, TURM_Y - 5, kx, ky, "#212121")
    zeichne_linie(win, TURM_X + 1, TURM_Y - 5, kx + 1, ky, "#212121")

    # Schuesse
    setze si auf 0
    solange si < MAX_SCHUSS:
        wenn sch_aktiv[si]:
            zeichne_kreis(win, sch_x[si], sch_y[si], 2, "#FFEB3B")
        setze si auf si + 1

    # Explosionen
    setze ei auf 0
    solange ei < MAX_EXPL:
        wenn ex_aktiv[ei]:
            setze er auf 15 - ex_t[ei]
            zeichne_kreis(win, ex_x[ei], ex_y[ei], er, "#FF9800")
            wenn er > 5:
                zeichne_kreis(win, ex_x[ei], ex_y[ei], er - 5, "#FFEB3B")
        setze ei auf ei + 1

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 25, "#0D1B2A")
    setze si auf 0
    solange si < score / 100 und si < 30:
        zeichne_kreis(win, 15 + si * 12, 12, 4, "#FFD700")
        setze si auf si + 1
    setze w_idx auf 0
    solange w_idx < welle und w_idx < 10:
        zeichne_rechteck(win, BREITE - 120 + w_idx * 10, 8, 8, 8, "#FF9800")
        setze w_idx auf w_idx + 1
    # Para-Warn-Balken
    setze p_left auf 0
    setze p_right auf 0
    setze pi auf 0
    solange pi < MAX_PARA:
        wenn para_aktiv[pi] und para_landed[pi]:
            wenn para_x[pi] < TURM_X:
                setze p_left auf p_left + 1
            sonst:
                setze p_right auf p_right + 1
        setze pi auf pi + 1
    setze lbi auf 0
    solange lbi < p_left:
        zeichne_rechteck(win, 250 + lbi * 10, 10, 8, 8, "#F44336")
        setze lbi auf lbi + 1
    setze rbi auf 0
    solange rbi < p_right:
        zeichne_rechteck(win, 350 + rbi * 10, 10, 8, 8, "#F44336")
        setze rbi auf rbi + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Paratrooper! Score: " + text(score) + " | Welle: " + text(welle)

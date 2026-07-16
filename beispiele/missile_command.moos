# ============================================================
# moo Missile Command — Staedte verteidigen
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/missile_command.moo -o beispiele/missile_command
#   ./beispiele/missile_command
#
# Bedienung:
#   Maus-Klick - Abwehr-Rakete abfeuern (zum Klick-Punkt)
#   1/2/3 - Basis auswaehlen (links/mitte/rechts)
#   Escape - Beenden
#
# Features:
#   * 6 Staedte verteidigen
#   * 3 Abwehr-Basen (je 10 Raketen)
#   * Einfallende Feind-Raketen (trennen sich!)
#   * Explosionen zerstoeren Feind-Raketen
#   * Wellen-System, Bonus-Stadt
# ============================================================

setze BREITE auf 800
setze HOEHE auf 500
setze BODEN_Y auf 450
setze MAX_FEIND_RAKETEN auf 25
setze MAX_EIGENE_RAKETEN auf 20
setze MAX_EXPLOSIONEN auf 30

setze rng_state auf 22222
funktion rng():
    setze rng_state auf (rng_state * 1103515245 + 12345) % 2147483647
    wenn rng_state < 0:
        setze rng_state auf 0 - rng_state
    gib_zurück rng_state

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

# Städte (6)
setze STADT_COUNT auf 6
setze stadt_x auf [100, 220, 340, 460, 580, 700]
setze stadt_aktiv auf [wahr, wahr, wahr, wahr, wahr, wahr]

# Basen (3)
setze basis_x auf [50, BREITE / 2, BREITE - 50]
setze basis_y auf BODEN_Y - 20
setze basis_munition auf [10, 10, 10]
setze aktive_basis auf 1

# Feind-Raketen (einfallend)
setze fr_x auf []
setze fr_y auf []
setze fr_ziel_x auf []
setze fr_ziel_y auf []
setze fr_vx auf []
setze fr_vy auf []
setze fr_aktiv auf []
setze fr_split auf []
setze fi auf 0
solange fi < MAX_FEIND_RAKETEN:
    fr_x.hinzufügen(0.0)
    fr_y.hinzufügen(0.0)
    fr_ziel_x.hinzufügen(0.0)
    fr_ziel_y.hinzufügen(0.0)
    fr_vx.hinzufügen(0.0)
    fr_vy.hinzufügen(0.0)
    fr_aktiv.hinzufügen(falsch)
    fr_split.hinzufügen(falsch)
    setze fi auf fi + 1

# Eigene Raketen
setze er_x auf []
setze er_y auf []
setze er_ziel_x auf []
setze er_ziel_y auf []
setze er_vx auf []
setze er_vy auf []
setze er_aktiv auf []
setze er_trail auf []
setze ei auf 0
solange ei < MAX_EIGENE_RAKETEN:
    er_x.hinzufügen(0.0)
    er_y.hinzufügen(0.0)
    er_ziel_x.hinzufügen(0.0)
    er_ziel_y.hinzufügen(0.0)
    er_vx.hinzufügen(0.0)
    er_vy.hinzufügen(0.0)
    er_aktiv.hinzufügen(falsch)
    er_trail.hinzufügen(0.0)
    setze ei auf ei + 1

# Explosionen
setze ex_x auf []
setze ex_y auf []
setze ex_radius auf []
setze ex_max_radius auf []
setze ex_timer auf []
setze ex_aktiv auf []
setze exi auf 0
solange exi < MAX_EXPLOSIONEN:
    ex_x.hinzufügen(0.0)
    ex_y.hinzufügen(0.0)
    ex_radius.hinzufügen(0.0)
    ex_max_radius.hinzufügen(0.0)
    ex_timer.hinzufügen(0)
    ex_aktiv.hinzufügen(falsch)
    setze exi auf exi + 1

setze score auf 0
setze welle auf 1
setze welle_timer auf 0
setze spawn_timer auf 0
setze klick_cd auf 0

funktion feind_rakete_spawnen(ziel_x, ziel_y):
    setze fi auf 0
    solange fi < MAX_FEIND_RAKETEN:
        wenn nicht fr_aktiv[fi]:
            setze start_x auf rng() % BREITE * 1.0
            fr_x[fi] = start_x
            fr_y[fi] = 0.0
            fr_ziel_x[fi] = ziel_x
            fr_ziel_y[fi] = ziel_y
            setze dx auf ziel_x - start_x
            setze dy auf ziel_y
            setze dist auf wurzel(dx * dx + dy * dy)
            setze speed auf 0.8 + welle * 0.15
            wenn speed > 3.0:
                setze speed auf 3.0
            fr_vx[fi] = dx / dist * speed
            fr_vy[fi] = dy / dist * speed
            fr_aktiv[fi] = wahr
            fr_split[fi] = falsch
            gib_zurück nichts
        setze fi auf fi + 1

funktion eigene_rakete(start_x, start_y, ziel_x, ziel_y):
    setze ei auf 0
    solange ei < MAX_EIGENE_RAKETEN:
        wenn nicht er_aktiv[ei]:
            er_x[ei] = start_x * 1.0
            er_y[ei] = start_y * 1.0
            er_ziel_x[ei] = ziel_x * 1.0
            er_ziel_y[ei] = ziel_y * 1.0
            setze dx auf ziel_x - start_x
            setze dy auf ziel_y - start_y
            setze dist auf wurzel(dx * dx + dy * dy)
            er_vx[ei] = dx / dist * 6.0
            er_vy[ei] = dy / dist * 6.0
            er_aktiv[ei] = wahr
            er_trail[ei] = dist
            gib_zurück wahr
        setze ei auf ei + 1
    gib_zurück falsch

funktion explosion_spawnen(x, y, radius):
    setze exi auf 0
    solange exi < MAX_EXPLOSIONEN:
        wenn nicht ex_aktiv[exi]:
            ex_x[exi] = x
            ex_y[exi] = y
            ex_radius[exi] = 0.0
            ex_max_radius[exi] = radius * 1.0
            ex_timer[exi] = 40
            ex_aktiv[exi] = wahr
            gib_zurück nichts
        setze exi auf exi + 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Missile Command", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp
    # Game Over: Alle Städte zerstört
    setze alive_staedte auf 0
    setze si auf 0
    solange si < STADT_COUNT:
        wenn stadt_aktiv[si]:
            setze alive_staedte auf alive_staedte + 1
        setze si auf si + 1
    wenn alive_staedte == 0:
        stopp

    # Basis waehlen
    wenn taste_gedrückt("1"):
        setze aktive_basis auf 0
    wenn taste_gedrückt("2"):
        setze aktive_basis auf 1
    wenn taste_gedrückt("3"):
        setze aktive_basis auf 2

    # Maus-Klick = Abwehr-Rakete
    wenn klick_cd > 0:
        setze klick_cd auf klick_cd - 1
    wenn maus_gedrückt(win) und klick_cd == 0:
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        # Naechste Basis mit Munition
        setze start_basis auf aktive_basis
        setze tries auf 0
        solange tries < 3 und basis_munition[start_basis] <= 0:
            setze start_basis auf (start_basis + 1) % 3
            setze tries auf tries + 1
        wenn basis_munition[start_basis] > 0:
            wenn eigene_rakete(basis_x[start_basis], basis_y, mx, my):
                setze basis_munition[start_basis] auf basis_munition[start_basis] - 1
                setze klick_cd auf 5

    # === WELLEN-SYSTEM ===
    setze welle_timer auf welle_timer + 1
    setze spawn_timer auf spawn_timer + 1
    setze spawn_rate auf 50 - welle * 3
    wenn spawn_rate < 15:
        setze spawn_rate auf 15
    wenn spawn_timer >= spawn_rate:
        setze spawn_timer auf 0
        # Ziel: Stadt oder Basis
        setze ziel_typ auf rng() % 10
        wenn ziel_typ < 7:
            # Stadt
            setze ti auf rng() % STADT_COUNT
            wenn stadt_aktiv[ti]:
                feind_rakete_spawnen(stadt_x[ti] * 1.0, BODEN_Y * 1.0)
        sonst:
            # Basis
            setze ti auf rng() % 3
            feind_rakete_spawnen(basis_x[ti] * 1.0, basis_y * 1.0)

    # Naechste Welle
    wenn welle_timer > 600:
        setze welle_timer auf 0
        setze welle auf welle + 1
        # Munition auffuellen
        setze bi auf 0
        solange bi < 3:
            basis_munition[bi] = 10
            setze bi auf bi + 1
        # Bonus: Zerstoerte Stadt wieder bauen?
        wenn welle % 3 == 0:
            setze si auf 0
            solange si < STADT_COUNT:
                wenn nicht stadt_aktiv[si]:
                    stadt_aktiv[si] = wahr
                    setze si auf STADT_COUNT
                setze si auf si + 1

    # === FEIND-RAKETEN BEWEGEN ===
    setze fi auf 0
    solange fi < MAX_FEIND_RAKETEN:
        wenn fr_aktiv[fi]:
            fr_x[fi] = fr_x[fi] + fr_vx[fi]
            fr_y[fi] = fr_y[fi] + fr_vy[fi]

            # Split bei Welle > 3 auf halber Hoehe
            wenn welle > 3 und nicht fr_split[fi] und fr_y[fi] > HOEHE / 2:
                fr_split[fi] = wahr
                # Zwei neue spawnen
                setze splits auf 0
                solange splits < 2:
                    setze ni auf 0
                    solange ni < MAX_FEIND_RAKETEN:
                        wenn nicht fr_aktiv[ni]:
                            fr_x[ni] = fr_x[fi]
                            fr_y[ni] = fr_y[fi]
                            setze new_target_x auf rng() % BREITE
                            fr_ziel_x[ni] = new_target_x * 1.0
                            fr_ziel_y[ni] = BODEN_Y * 1.0
                            setze dx auf new_target_x - fr_x[fi]
                            setze dy auf BODEN_Y - fr_y[fi]
                            setze dist auf wurzel(dx * dx + dy * dy)
                            fr_vx[ni] = dx / dist * 1.0
                            fr_vy[ni] = dy / dist * 1.0
                            fr_aktiv[ni] = wahr
                            fr_split[ni] = wahr
                            setze ni auf MAX_FEIND_RAKETEN
                        setze ni auf ni + 1
                    setze splits auf splits + 1

            # Ziel erreicht?
            wenn fr_y[fi] >= fr_ziel_y[fi]:
                explosion_spawnen(fr_x[fi], fr_y[fi], 30)
                fr_aktiv[fi] = falsch
                # Stadt zerstoeren
                setze si auf 0
                solange si < STADT_COUNT:
                    wenn stadt_aktiv[si] und abs_wert(stadt_x[si] - fr_x[fi]) < 30:
                        stadt_aktiv[si] = falsch
                    setze si auf si + 1
                # Basis zerstoeren?
                setze bi auf 0
                solange bi < 3:
                    wenn abs_wert(basis_x[bi] - fr_x[fi]) < 30:
                        basis_munition[bi] = 0
                    setze bi auf bi + 1
        setze fi auf fi + 1

    # === EIGENE RAKETEN ===
    setze ei auf 0
    solange ei < MAX_EIGENE_RAKETEN:
        wenn er_aktiv[ei]:
            er_x[ei] = er_x[ei] + er_vx[ei]
            er_y[ei] = er_y[ei] + er_vy[ei]
            setze dx auf er_ziel_x[ei] - er_x[ei]
            setze dy auf er_ziel_y[ei] - er_y[ei]
            setze dist auf wurzel(dx * dx + dy * dy)
            wenn dist < 5:
                # Ziel erreicht → Explosion
                explosion_spawnen(er_ziel_x[ei], er_ziel_y[ei], 40)
                er_aktiv[ei] = falsch
        setze ei auf ei + 1

    # === EXPLOSIONEN UPDATE ===
    setze exi auf 0
    solange exi < MAX_EXPLOSIONEN:
        wenn ex_aktiv[exi]:
            ex_timer[exi] = ex_timer[exi] - 1
            # Wachsen
            wenn ex_timer[exi] > 20:
                setze ex_radius[exi] auf ex_radius[exi] + ex_max_radius[exi] / 20.0
            sonst:
                setze ex_radius[exi] auf ex_radius[exi] - ex_max_radius[exi] / 20.0
            wenn ex_timer[exi] <= 0:
                ex_aktiv[exi] = falsch
            # Feind-Raketen in Explosion zerstoeren
            setze fi auf 0
            solange fi < MAX_FEIND_RAKETEN:
                wenn fr_aktiv[fi]:
                    setze dx auf fr_x[fi] - ex_x[exi]
                    setze dy auf fr_y[fi] - ex_y[exi]
                    wenn dx * dx + dy * dy < ex_radius[exi] * ex_radius[exi]:
                        fr_aktiv[fi] = falsch
                        explosion_spawnen(fr_x[fi], fr_y[fi], 25)
                        setze score auf score + 50
                setze fi auf fi + 1
        setze exi auf exi + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#0A0A2E")

    # Sterne
    setze star_i auf 0
    solange star_i < 40:
        setze sx auf (star_i * 347 + 89) % BREITE
        setze sy auf (star_i * 571 + 23) % (BODEN_Y - 50)
        zeichne_pixel(win, sx, sy, "#FFFFFF")
        setze star_i auf star_i + 1

    # Boden
    zeichne_rechteck(win, 0, BODEN_Y, BREITE, HOEHE - BODEN_Y, "#4E342E")
    zeichne_rechteck(win, 0, BODEN_Y, BREITE, 3, "#8D6E63")

    # Staedte
    setze si auf 0
    solange si < STADT_COUNT:
        wenn stadt_aktiv[si]:
            setze cx auf stadt_x[si]
            zeichne_rechteck(win, cx - 20, BODEN_Y - 15, 40, 15, "#42A5F5")
            zeichne_rechteck(win, cx - 15, BODEN_Y - 22, 10, 7, "#64B5F6")
            zeichne_rechteck(win, cx - 3, BODEN_Y - 26, 6, 11, "#64B5F6")
            zeichne_rechteck(win, cx + 8, BODEN_Y - 20, 10, 5, "#64B5F6")
        sonst:
            setze cx auf stadt_x[si]
            zeichne_rechteck(win, cx - 20, BODEN_Y - 8, 40, 8, "#424242")
        setze si auf si + 1

    # Basen
    setze bi auf 0
    solange bi < 3:
        setze bx auf basis_x[bi]
        wenn basis_munition[bi] > 0:
            zeichne_rechteck(win, bx - 25, basis_y, 50, 20, "#558B2F")
            zeichne_rechteck(win, bx - 15, basis_y - 10, 30, 10, "#689F38")
            # Raketen-Anzeige
            setze mi auf 0
            solange mi < basis_munition[bi]:
                zeichne_rechteck(win, bx - 20 + mi * 5, basis_y - 5, 2, 4, "#FFEB3B")
                setze mi auf mi + 1
        sonst:
            zeichne_rechteck(win, bx - 25, basis_y + 10, 50, 10, "#424242")
        wenn bi == aktive_basis und basis_munition[bi] > 0:
            zeichne_rechteck(win, bx - 27, basis_y - 12, 54, 2, "#FFFFFF")
        setze bi auf bi + 1

    # Eigene Raketen + Trails
    setze ei auf 0
    solange ei < MAX_EIGENE_RAKETEN:
        wenn er_aktiv[ei]:
            # Trail vom Start zur aktuellen Position
            setze b_x auf basis_x[0]
            setze best auf 0
            setze best_d auf 9999
            setze bi auf 0
            solange bi < 3:
                setze d auf abs_wert(basis_x[bi] - er_ziel_x[ei])
                wenn d < best_d:
                    setze best_d auf d
                    setze best auf bi
                setze bi auf bi + 1
            setze b_x auf basis_x[best]
            zeichne_linie(win, b_x, basis_y, er_x[ei], er_y[ei], "#42A5F5")
            zeichne_kreis(win, er_x[ei], er_y[ei], 3, "#FFFFFF")
        setze ei auf ei + 1

    # Feind-Raketen + Trails
    setze fi auf 0
    solange fi < MAX_FEIND_RAKETEN:
        wenn fr_aktiv[fi]:
            # Trail vom oberen Bildschirmrand
            setze start_x auf fr_x[fi] - fr_vx[fi] * 20
            setze start_y auf fr_y[fi] - fr_vy[fi] * 20
            zeichne_linie(win, start_x, start_y, fr_x[fi], fr_y[fi], "#F44336")
            zeichne_kreis(win, fr_x[fi], fr_y[fi], 4, "#FF5722")
        setze fi auf fi + 1

    # Explosionen
    setze exi auf 0
    solange exi < MAX_EXPLOSIONEN:
        wenn ex_aktiv[exi]:
            setze r auf boden(ex_radius[exi])
            wenn r > 0:
                zeichne_kreis(win, ex_x[exi], ex_y[exi], r, "#FFEB3B")
                wenn r > 5:
                    zeichne_kreis(win, ex_x[exi], ex_y[exi], r - 5, "#FF9800")
                wenn r > 10:
                    zeichne_kreis(win, ex_x[exi], ex_y[exi], r - 10, "#FFFFFF")
        setze exi auf exi + 1

    # Maus-Cursor
    setze mx auf maus_x(win)
    setze my auf maus_y(win)
    zeichne_linie(win, mx - 8, my, mx + 8, my, "#FFFFFF")
    zeichne_linie(win, mx, my - 8, mx, my + 8, "#FFFFFF")

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 25, "#0D1B2A")
    # Score
    setze si auf 0
    solange si < score / 100 und si < 30:
        zeichne_kreis(win, 15 + si * 12, 12, 4, "#FFD700")
        setze si auf si + 1
    # Welle
    setze w_idx auf 0
    solange w_idx < welle und w_idx < 15:
        zeichne_rechteck(win, BREITE - 200 + w_idx * 10, 8, 8, 8, "#FF9800")
        setze w_idx auf w_idx + 1
    # Städte-Status
    setze ci auf 0
    solange ci < STADT_COUNT:
        wenn stadt_aktiv[ci]:
            zeichne_rechteck(win, BREITE - 60 + ci * 8, 10, 6, 6, "#42A5F5")
        sonst:
            zeichne_rechteck(win, BREITE - 60 + ci * 8, 10, 6, 6, "#424242")
        setze ci auf ci + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "GAME OVER! Score: " + text(score) + " | Welle: " + text(welle) + " | Staedte: " + text(alive_staedte) + "/6"

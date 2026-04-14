# ============================================================
# moo Gravity Well — Physik-Puzzle mit Gravitation
#
# Kompilieren + Starten:
#   moo-compiler compile beispiele/gravity_well.moo -o beispiele/gravity_well
#   ./beispiele/gravity_well
#
# Bedienung:
#   Maus-Klick (halten) - Geschoss zielen + werfen (Power)
#   R - Level neu starten
#   Escape - Beenden
#
# Ziel: Treffe alle Ziele mit Geschossen, die von Planeten
# angezogen werden!
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600
setze MAX_PLANETEN auf 5
setze MAX_ZIELE auf 8
setze MAX_PROJ auf 10

# Planeten (Gravitation)
setze plan_x auf []
setze plan_y auf []
setze plan_masse auf []
setze plan_radius auf []

# Ziele
setze ziel_x auf []
setze ziel_y auf []
setze ziel_aktiv auf []

# Projektile
setze proj_x auf []
setze proj_y auf []
setze proj_vx auf []
setze proj_vy auf []
setze proj_aktiv auf []
setze proj_leben auf []

setze pi auf 0
solange pi < MAX_PROJ:
    proj_x.hinzufügen(0.0)
    proj_y.hinzufügen(0.0)
    proj_vx.hinzufügen(0.0)
    proj_vy.hinzufügen(0.0)
    proj_aktiv.hinzufügen(falsch)
    proj_leben.hinzufügen(0)
    setze pi auf pi + 1

# Startpunkt
setze start_x auf 60.0
setze start_y auf 300.0

# Spielzustand
setze schuesse auf 5
setze level auf 1
setze score auf 0
setze ziehen auf falsch
setze ziel_mx auf 0.0
setze ziel_my auf 0.0

funktion abs_wert(v):
    wenn v < 0:
        gib_zurück 0 - v
    gib_zurück v

funktion level_laden(lvl):
    setze plan_x auf []
    setze plan_y auf []
    setze plan_masse auf []
    setze plan_radius auf []
    setze ziel_x auf []
    setze ziel_y auf []
    setze ziel_aktiv auf []

    # Alle Projektile deaktivieren
    setze pi auf 0
    solange pi < MAX_PROJ:
        proj_aktiv[pi] = falsch
        setze pi auf pi + 1

    wenn lvl == 1:
        # 1 Planet, 2 Ziele
        plan_x.hinzufügen(400.0)
        plan_y.hinzufügen(300.0)
        plan_masse.hinzufügen(800.0)
        plan_radius.hinzufügen(35)
        ziel_x.hinzufügen(700.0)
        ziel_y.hinzufügen(200.0)
        ziel_aktiv.hinzufügen(wahr)
        ziel_x.hinzufügen(700.0)
        ziel_y.hinzufügen(400.0)
        ziel_aktiv.hinzufügen(wahr)
        setze schuesse auf 4

    wenn lvl == 2:
        # 2 Planeten, 3 Ziele
        plan_x.hinzufügen(300.0)
        plan_y.hinzufügen(200.0)
        plan_masse.hinzufügen(600.0)
        plan_radius.hinzufügen(30)
        plan_x.hinzufügen(500.0)
        plan_y.hinzufügen(400.0)
        plan_masse.hinzufügen(700.0)
        plan_radius.hinzufügen(32)
        ziel_x.hinzufügen(720.0)
        ziel_y.hinzufügen(100.0)
        ziel_aktiv.hinzufügen(wahr)
        ziel_x.hinzufügen(720.0)
        ziel_y.hinzufügen(300.0)
        ziel_aktiv.hinzufügen(wahr)
        ziel_x.hinzufügen(720.0)
        ziel_y.hinzufügen(500.0)
        ziel_aktiv.hinzufügen(wahr)
        setze schuesse auf 5

    wenn lvl == 3:
        # 3 Planeten, 4 Ziele
        plan_x.hinzufügen(250.0)
        plan_y.hinzufügen(150.0)
        plan_masse.hinzufügen(500.0)
        plan_radius.hinzufügen(25)
        plan_x.hinzufügen(400.0)
        plan_y.hinzufügen(400.0)
        plan_masse.hinzufügen(800.0)
        plan_radius.hinzufügen(38)
        plan_x.hinzufügen(600.0)
        plan_y.hinzufügen(200.0)
        plan_masse.hinzufügen(500.0)
        plan_radius.hinzufügen(25)
        ziel_x.hinzufügen(720.0)
        ziel_y.hinzufügen(50.0)
        ziel_aktiv.hinzufügen(wahr)
        ziel_x.hinzufügen(720.0)
        ziel_y.hinzufügen(350.0)
        ziel_aktiv.hinzufügen(wahr)
        ziel_x.hinzufügen(720.0)
        ziel_y.hinzufügen(550.0)
        ziel_aktiv.hinzufügen(wahr)
        ziel_x.hinzufügen(400.0)
        ziel_y.hinzufügen(550.0)
        ziel_aktiv.hinzufügen(wahr)
        setze schuesse auf 6

    wenn lvl >= 4:
        # 4 Planeten, 5 Ziele
        plan_x.hinzufügen(200.0)
        plan_y.hinzufügen(150.0)
        plan_masse.hinzufügen(600.0)
        plan_radius.hinzufügen(28)
        plan_x.hinzufügen(350.0)
        plan_y.hinzufügen(450.0)
        plan_masse.hinzufügen(700.0)
        plan_radius.hinzufügen(30)
        plan_x.hinzufügen(550.0)
        plan_y.hinzufügen(150.0)
        plan_masse.hinzufügen(700.0)
        plan_radius.hinzufügen(30)
        plan_x.hinzufügen(650.0)
        plan_y.hinzufügen(450.0)
        plan_masse.hinzufügen(500.0)
        plan_radius.hinzufügen(25)
        ziel_x.hinzufügen(720.0)
        ziel_y.hinzufügen(100.0)
        ziel_aktiv.hinzufügen(wahr)
        ziel_x.hinzufügen(720.0)
        ziel_y.hinzufügen(300.0)
        ziel_aktiv.hinzufügen(wahr)
        ziel_x.hinzufügen(720.0)
        ziel_y.hinzufügen(500.0)
        ziel_aktiv.hinzufügen(wahr)
        ziel_x.hinzufügen(400.0)
        ziel_y.hinzufügen(50.0)
        ziel_aktiv.hinzufügen(wahr)
        ziel_x.hinzufügen(400.0)
        ziel_y.hinzufügen(550.0)
        ziel_aktiv.hinzufügen(wahr)
        setze schuesse auf 7

level_laden(level)

funktion projektil_werfen(vx, vy):
    setze pi auf 0
    solange pi < MAX_PROJ:
        wenn nicht proj_aktiv[pi]:
            proj_x[pi] = start_x
            proj_y[pi] = start_y
            proj_vx[pi] = vx
            proj_vy[pi] = vy
            proj_aktiv[pi] = wahr
            proj_leben[pi] = 500
            gib_zurück nichts
        setze pi auf pi + 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Gravity Well", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn taste_gedrückt("r"):
        level_laden(level)
        setze score auf 0

    # Alle Ziele getroffen → naechstes Level
    setze alle_weg auf wahr
    setze zi auf 0
    solange zi < länge(ziel_x):
        wenn ziel_aktiv[zi]:
            setze alle_weg auf falsch
        setze zi auf zi + 1
    wenn alle_weg:
        setze level auf level + 1
        setze score auf score + 500
        wenn level > 4:
            stopp
        level_laden(level)

    # Keine Schuesse mehr und keine aktiven Projektile → verloren
    setze aktiv_count auf 0
    setze pi auf 0
    solange pi < MAX_PROJ:
        wenn proj_aktiv[pi]:
            setze aktiv_count auf aktiv_count + 1
        setze pi auf pi + 1
    wenn schuesse <= 0 und aktiv_count == 0 und nicht alle_weg:
        level_laden(level)
        setze score auf score - 100

    # === MAUS-EINGABE ===
    setze mx auf maus_x(win)
    setze my auf maus_y(win)
    wenn maus_gedrückt(win):
        wenn nicht ziehen und schuesse > 0:
            setze ziehen auf wahr
        setze ziel_mx auf mx
        setze ziel_my auf my
    sonst:
        wenn ziehen:
            setze dx auf start_x - ziel_mx
            setze dy auf start_y - ziel_my
            setze mag auf wurzel(dx * dx + dy * dy)
            wenn mag > 10:
                setze power auf mag / 20.0
                wenn power > 12:
                    setze power auf 12.0
                projektil_werfen(dx / mag * power, dy / mag * power)
                setze schuesse auf schuesse - 1
            setze ziehen auf falsch

    # === PROJEKTILE UPDATEN ===
    setze pi auf 0
    solange pi < MAX_PROJ:
        wenn proj_aktiv[pi]:
            # Gravitation von allen Planeten
            setze pli auf 0
            solange pli < länge(plan_x):
                setze dx auf plan_x[pli] - proj_x[pi]
                setze dy auf plan_y[pli] - proj_y[pi]
                setze dist2 auf dx * dx + dy * dy
                setze dist auf wurzel(dist2)
                wenn dist > 1:
                    setze kraft auf plan_masse[pli] / dist2
                    proj_vx[pi] = proj_vx[pi] + dx / dist * kraft
                    proj_vy[pi] = proj_vy[pi] + dy / dist * kraft
                # Kollision mit Planet
                wenn dist < plan_radius[pli] + 5:
                    proj_aktiv[pi] = falsch
                setze pli auf pli + 1

            # Position update
            proj_x[pi] = proj_x[pi] + proj_vx[pi]
            proj_y[pi] = proj_y[pi] + proj_vy[pi]
            proj_leben[pi] = proj_leben[pi] - 1

            # Aus dem Bildschirm / Zeit aus
            wenn proj_x[pi] < -50 oder proj_x[pi] > BREITE + 50:
                proj_aktiv[pi] = falsch
            wenn proj_y[pi] < -50 oder proj_y[pi] > HOEHE + 50:
                proj_aktiv[pi] = falsch
            wenn proj_leben[pi] <= 0:
                proj_aktiv[pi] = falsch

            # Ziel getroffen?
            setze zi auf 0
            solange zi < länge(ziel_x):
                wenn ziel_aktiv[zi]:
                    setze dx auf ziel_x[zi] - proj_x[pi]
                    setze dy auf ziel_y[zi] - proj_y[pi]
                    wenn dx * dx + dy * dy < 200:
                        ziel_aktiv[zi] = falsch
                        proj_aktiv[pi] = falsch
                        setze score auf score + 100
                setze zi auf zi + 1
        setze pi auf pi + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#000011")

    # Sterne
    setze star_i auf 0
    solange star_i < 40:
        setze sx auf (star_i * 347 + 89) % BREITE
        setze sy auf (star_i * 571 + 23) % HOEHE
        zeichne_pixel(win, sx, sy, "#FFFFFF")
        setze star_i auf star_i + 1

    # Planeten (mit Gravitations-Feld)
    setze pli auf 0
    solange pli < länge(plan_x):
        setze px auf plan_x[pli]
        setze py auf plan_y[pli]
        setze pr auf plan_radius[pli]
        # Gravitations-Feld (Kreise)
        zeichne_kreis(win, px, py, pr + 30, "#112244")
        zeichne_kreis(win, px, py, pr + 15, "#223366")
        # Planet
        zeichne_kreis(win, px, py, pr, "#FF9800")
        setze pr2 auf pr
        setze pr2 auf pr2 - 5
        zeichne_kreis(win, px, py, pr2, "#FFB74D")
        setze pr3 auf pr / 3
        setze pr4 auf pr / 4
        zeichne_kreis(win, px - pr3, py - pr3, pr4, "#FFE0B2")
        setze pli auf pli + 1

    # Ziele
    setze zi auf 0
    solange zi < länge(ziel_x):
        wenn ziel_aktiv[zi]:
            zeichne_kreis(win, ziel_x[zi], ziel_y[zi], 15, "#F44336")
            zeichne_kreis(win, ziel_x[zi], ziel_y[zi], 10, "#FFFFFF")
            zeichne_kreis(win, ziel_x[zi], ziel_y[zi], 5, "#F44336")
        setze zi auf zi + 1

    # Projektile + Trail
    setze pi auf 0
    solange pi < MAX_PROJ:
        wenn proj_aktiv[pi]:
            zeichne_kreis(win, proj_x[pi], proj_y[pi], 4, "#FFEB3B")
            zeichne_kreis(win, proj_x[pi], proj_y[pi], 2, "#FFFFFF")
        setze pi auf pi + 1

    # Kanone (Startpunkt)
    zeichne_rechteck(win, start_x - 15, start_y - 15, 30, 30, "#546E7A")
    zeichne_rechteck(win, start_x - 10, start_y - 10, 20, 20, "#78909C")
    zeichne_kreis(win, start_x, start_y, 6, "#FFEB3B")

    # Ziel-Linie (wenn gezogen)
    wenn ziehen:
        setze dx auf start_x - ziel_mx
        setze dy auf start_y - ziel_my
        setze mag auf wurzel(dx * dx + dy * dy)
        wenn mag > 10:
            # Vorhersage-Punkte
            setze vx auf dx / mag * (mag / 20.0)
            setze vy auf dy / mag * (mag / 20.0)
            wenn mag / 20.0 > 12:
                setze vx auf dx / mag * 12
                setze vy auf dy / mag * 12
            # Simulation ein paar Schritte
            setze sim_x auf start_x
            setze sim_y auf start_y
            setze sim_vx auf vx
            setze sim_vy auf vy
            setze step auf 0
            solange step < 30:
                setze pli auf 0
                solange pli < länge(plan_x):
                    setze gdx auf plan_x[pli] - sim_x
                    setze gdy auf plan_y[pli] - sim_y
                    setze gd2 auf gdx * gdx + gdy * gdy
                    setze gd auf wurzel(gd2)
                    wenn gd > 1:
                        setze kraft auf plan_masse[pli] / gd2
                        setze sim_vx auf sim_vx + gdx / gd * kraft
                        setze sim_vy auf sim_vy + gdy / gd * kraft
                    setze pli auf pli + 1
                setze sim_x auf sim_x + sim_vx
                setze sim_y auf sim_y + sim_vy
                wenn step % 3 == 0:
                    zeichne_pixel(win, sim_x, sim_y, "#FFFFFF")
                setze step auf step + 1
        # Zug-Linie
        zeichne_linie(win, start_x, start_y, ziel_mx, ziel_my, "#FFEB3B")

    # HUD
    zeichne_rechteck(win, 0, 0, BREITE, 25, "#0D1B2A")
    # Schuesse
    setze si auf 0
    solange si < schuesse:
        zeichne_kreis(win, 15 + si * 14, 12, 5, "#FFEB3B")
        setze si auf si + 1
    # Score
    setze si auf 0
    solange si < score / 50 und si < 25:
        zeichne_kreis(win, 200 + si * 12, 12, 4, "#FFD700")
        setze si auf si + 1
    # Level
    setze li auf 0
    solange li < level und li < 5:
        zeichne_rechteck(win, BREITE - 60 + li * 12, 8, 8, 8, "#FF9800")
        setze li auf li + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn level > 4:
    zeige "ALLE LEVEL GESCHAFFT! Score: " + text(score)
sonst:
    zeige "Game Over! Level: " + text(level) + " | Score: " + text(score)

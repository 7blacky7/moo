# ============================================================
# moo Lemmings — Rette die Lemminge!
#
# Kompilieren: moo-compiler compile beispiele/lemmings.moo -o beispiele/lemmings
# Starten:     ./beispiele/lemmings
#
# Maus = Lemming anklicken + Aktion zuweisen
# 1=Blocker 2=Graber 3=Bruecke 4=Fallschirm
# Escape = Beenden
# ============================================================

setze BREITE auf 800
setze HOEHE auf 500
setze MAX_LEM auf 30
setze TILE auf 4
setze MAP_W auf 200
setze MAP_H auf 125

# Terrain-Grid
setze karte auf []
setze mi auf 0
solange mi < MAP_W * MAP_H:
    karte.hinzufügen(0)
    setze mi auf mi + 1

# Terrain bauen — Plattformen
funktion plattform_setzen(px, py, pw):
    setze xi auf 0
    solange xi < pw:
        wenn px + xi >= 0 und px + xi < MAP_W und py >= 0 und py < MAP_H:
            setze karte[(py) * MAP_W + px + xi] auf 1
            wenn py + 1 < MAP_H:
                setze karte[(py + 1) * MAP_W + px + xi] auf 1
        setze xi auf xi + 1

# Level-Design
plattform_setzen(0, 120, 200)
plattform_setzen(20, 50, 40)
plattform_setzen(80, 70, 50)
plattform_setzen(40, 90, 30)
plattform_setzen(120, 60, 35)
plattform_setzen(140, 85, 40)
plattform_setzen(10, 75, 20)

# Eingang + Ausgang
setze eingang_x auf 30
setze eingang_y auf 45
setze ausgang_x auf 170
setze ausgang_y auf 115

# Lemminge
setze lem_x auf []
setze lem_y auf []
setze lem_vx auf []
setze lem_vy auf []
setze lem_aktion auf []
setze lem_aktiv auf []
setze lem_gerettet auf []

setze li auf 0
solange li < MAX_LEM:
    lem_x.hinzufügen(0.0)
    lem_y.hinzufügen(0.0)
    lem_vx.hinzufügen(1.0)
    lem_vy.hinzufügen(0.0)
    lem_aktion.hinzufügen(0)
    lem_aktiv.hinzufügen(falsch)
    lem_gerettet.hinzufügen(falsch)
    setze li auf li + 1

setze naechster_lem auf 0
setze spawn_timer auf 0
setze gerettet auf 0
setze verloren auf 0
setze aktion_typ auf 1
setze eingabe_cd auf 0

funktion terrain_fest(tx, ty):
    wenn tx < 0 oder tx >= MAP_W oder ty < 0 oder ty >= MAP_H:
        gib_zurück falsch
    gib_zurück karte[ty * MAP_W + tx] == 1

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Lemmings", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Aktion waehlen
    wenn eingabe_cd <= 0:
        wenn taste_gedrückt("1"):
            setze aktion_typ auf 1
            setze eingabe_cd auf 10
        wenn taste_gedrückt("2"):
            setze aktion_typ auf 2
            setze eingabe_cd auf 10
        wenn taste_gedrückt("3"):
            setze aktion_typ auf 3
            setze eingabe_cd auf 10
        wenn taste_gedrückt("4"):
            setze aktion_typ auf 4
            setze eingabe_cd auf 10

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    # Maus-Klick: Lemming Aktion zuweisen
    wenn maus_gedrückt(win) und eingabe_cd <= 0:
        setze mx auf maus_x(win) / TILE
        setze my auf maus_y(win) / TILE
        setze li auf 0
        solange li < MAX_LEM:
            wenn lem_aktiv[li] und nicht lem_gerettet[li]:
                setze ddx auf lem_x[li] - mx
                setze ddy auf lem_y[li] - my
                wenn ddx < 0:
                    setze ddx auf 0 - ddx
                wenn ddy < 0:
                    setze ddy auf 0 - ddy
                wenn ddx < 3 und ddy < 3:
                    setze lem_aktion[li] auf aktion_typ
                    setze li auf MAX_LEM
            setze li auf li + 1
        setze eingabe_cd auf 15

    # Spawn
    setze spawn_timer auf spawn_timer + 1
    wenn spawn_timer >= 30 und naechster_lem < MAX_LEM:
        setze lem_x[naechster_lem] auf eingang_x * 1.0
        setze lem_y[naechster_lem] auf eingang_y * 1.0
        setze lem_vx[naechster_lem] auf 0.5
        setze lem_vy[naechster_lem] auf 0.0
        setze lem_aktiv[naechster_lem] auf wahr
        setze naechster_lem auf naechster_lem + 1
        setze spawn_timer auf 0

    # Lemminge bewegen
    setze li auf 0
    solange li < MAX_LEM:
        wenn lem_aktiv[li] und nicht lem_gerettet[li]:
            setze lx auf lem_x[li]
            setze ly auf lem_y[li]
            setze la auf lem_aktion[li]

            # Blocker: steht still
            wenn la == 1:
                setze lem_vx[li] auf 0.0
            sonst:
                # Schwerkraft
                wenn nicht terrain_fest(lx, ly + 1):
                    setze lem_vy[li] auf lem_vy[li] + 0.2
                    # Fallschirm verlangsamt
                    wenn la == 4 und lem_vy[li] > 0.5:
                        setze lem_vy[li] auf 0.5
                sonst:
                    setze lem_vy[li] auf 0.0
                    # Graber: grabt nach unten
                    wenn la == 2:
                        wenn terrain_fest(lx, ly + 1):
                            setze karte[(ly + 1) * MAP_W + lx] auf 0

                # Horizontale Bewegung
                setze nx auf lx + lem_vx[li]
                wenn terrain_fest(nx, ly):
                    # Wand → umdrehen
                    setze lem_vx[li] auf 0 - lem_vx[li]
                sonst:
                    setze lem_x[li] auf nx

            # Vertikale Bewegung
            setze ny auf ly + lem_vy[li]
            wenn terrain_fest(lx, ny):
                setze lem_vy[li] auf 0.0
            sonst:
                setze lem_y[li] auf ny

            # Ausgang erreicht?
            setze ddx auf lem_x[li] - ausgang_x
            setze ddy auf lem_y[li] - ausgang_y
            wenn ddx < 0:
                setze ddx auf 0 - ddx
            wenn ddy < 0:
                setze ddy auf 0 - ddy
            wenn ddx < 3 und ddy < 3:
                setze lem_gerettet[li] auf wahr
                setze gerettet auf gerettet + 1

            # Tod (aus dem Bild gefallen)
            wenn lem_y[li] > MAP_H + 10:
                setze lem_aktiv[li] auf falsch
                setze verloren auf verloren + 1

            # Blocker: andere Lemminge abprallen
            wenn la == 1:
                setze lj auf 0
                solange lj < MAX_LEM:
                    wenn lj != li und lem_aktiv[lj] und nicht lem_gerettet[lj]:
                        setze ddx auf lem_x[lj] - lx
                        setze ddy auf lem_y[lj] - ly
                        wenn ddx < 0:
                            setze ddx auf 0 - ddx
                        wenn ddy < 0:
                            setze ddy auf 0 - ddy
                        wenn ddx < 2 und ddy < 2:
                            setze lem_vx[lj] auf 0 - lem_vx[lj]
                    setze lj auf lj + 1

        setze li auf li + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#87CEEB")

    # Terrain
    setze ty auf 0
    solange ty < MAP_H:
        setze tx auf 0
        solange tx < MAP_W:
            wenn karte[ty * MAP_W + tx] == 1:
                zeichne_rechteck(win, tx * TILE, ty * TILE, TILE, TILE, "#8D6E63")
            setze tx auf tx + 1
        setze ty auf ty + 1

    # Eingang
    zeichne_rechteck(win, eingang_x * TILE - 6, eingang_y * TILE - 12, 16, 12, "#4CAF50")
    # Ausgang
    zeichne_rechteck(win, ausgang_x * TILE - 6, ausgang_y * TILE - 12, 16, 12, "#F44336")

    # Lemminge
    setze li auf 0
    solange li < MAX_LEM:
        wenn lem_aktiv[li] und nicht lem_gerettet[li]:
            setze lx auf lem_x[li] * TILE
            setze ly auf lem_y[li] * TILE
            # Koerper
            zeichne_kreis(win, lx, ly - 3, 3, "#2196F3")
            zeichne_rechteck(win, lx - 2, ly, 4, 4, "#4CAF50")
            # Aktion-Marker
            wenn lem_aktion[li] == 1:
                zeichne_kreis(win, lx, ly - 8, 2, "#F44336")
            wenn lem_aktion[li] == 2:
                zeichne_rechteck(win, lx - 1, ly + 4, 2, 3, "#795548")
            wenn lem_aktion[li] == 4:
                zeichne_linie(win, lx - 4, ly - 7, lx + 4, ly - 7, "#FFFFFF")
        setze li auf li + 1

    # HUD
    zeichne_rechteck(win, 0, HOEHE - 30, BREITE, 30, "#333333")
    # Gerettet
    setze si auf 0
    solange si < gerettet und si < 30:
        zeichne_kreis(win, 20 + si * 10, HOEHE - 15, 3, "#4CAF50")
        setze si auf si + 1

    # Verloren
    setze si auf 0
    solange si < verloren und si < 15:
        zeichne_kreis(win, BREITE - 20 - si * 10, HOEHE - 15, 3, "#F44336")
        setze si auf si + 1

    # Aktions-Auswahl
    wenn aktion_typ == 1:
        zeichne_rechteck(win, 300, HOEHE - 28, 20, 20, "#FFFFFF")
    zeichne_rechteck(win, 302, HOEHE - 26, 16, 16, "#F44336")

    wenn aktion_typ == 2:
        zeichne_rechteck(win, 325, HOEHE - 28, 20, 20, "#FFFFFF")
    zeichne_rechteck(win, 327, HOEHE - 26, 16, 16, "#795548")

    wenn aktion_typ == 3:
        zeichne_rechteck(win, 350, HOEHE - 28, 20, 20, "#FFFFFF")
    zeichne_rechteck(win, 352, HOEHE - 26, 16, 16, "#FF9800")

    wenn aktion_typ == 4:
        zeichne_rechteck(win, 375, HOEHE - 28, 20, 20, "#FFFFFF")
    zeichne_rechteck(win, 377, HOEHE - 26, 16, 16, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Gerettet: " + text(gerettet) + " Verloren: " + text(verloren)

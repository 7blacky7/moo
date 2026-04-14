# ============================================================
# moo Siedler — Aufbauspiel (Anno/Siedler Style)
#
# Kompilieren: moo-compiler compile beispiele/siedler.moo -o beispiele/siedler
# Starten:     ./beispiele/siedler
#
# WASD = Karte scrollen
# Maus = Gebaeude platzieren
# 1=Holzfaeller 2=Steinbruch 3=Bauernhof 4=Muehle
# 5=Baeckerei 6=Wohnhaus 7=Schmiede
# Escape = Beenden
# ============================================================

setze BREITE auf 800
setze HOEHE auf 600
setze TILE auf 20
setze MAP_W auf 120
setze MAP_H auf 100

# Kamera
setze cam_x auf 5
setze cam_y auf 5

# === KARTE (0=Wasser, 1=Gras, 2=Wald, 3=Berg, 4=Getreidefeld) ===
setze karte auf []

funktion noise(px, py, seed):
    setze n auf (px * 374761 + py * 668265 + seed * 31337) % 1000
    wenn n < 0:
        setze n auf 0 - n
    gib_zurück n / 1000.0

setze my auf 0
solange my < MAP_H:
    setze mx auf 0
    solange mx < MAP_W:
        setze hoehe auf noise(mx, my, 42)
        setze h2 auf noise(mx + 1, my, 42)
        setze h3 auf noise(mx, my + 1, 42)
        setze hoehe auf (hoehe * 2 + h2 + h3) / 4.0
        setze feuchte auf noise(mx, my, 99)
        wenn hoehe < 0.25:
            karte.hinzufügen(0)
        sonst:
            wenn hoehe > 0.72:
                karte.hinzufügen(3)
            sonst:
                wenn feuchte > 0.55 und hoehe > 0.35:
                    karte.hinzufügen(2)
                sonst:
                    karte.hinzufügen(1)
        setze mx auf mx + 1
    setze my auf my + 1

# === GEBAEUDE ===
# Typ: 0=leer, 1=Holzfaeller, 2=Steinbruch, 3=Bauernhof, 4=Muehle,
#      5=Baeckerei, 6=Wohnhaus, 7=Schmiede
setze MAX_GEB auf 50
setze geb_x auf []
setze geb_y auf []
setze geb_typ auf []
setze geb_aktiv auf []
setze geb_timer auf []

setze gi auf 0
solange gi < MAX_GEB:
    geb_x.hinzufügen(0)
    geb_y.hinzufügen(0)
    geb_typ.hinzufügen(0)
    geb_aktiv.hinzufügen(falsch)
    geb_timer.hinzufügen(0)
    setze gi auf gi + 1

setze geb_anzahl auf 0

# === TRAEGER ===
setze MAX_TRAEGER auf 30
setze tr_x auf []
setze tr_y auf []
setze tr_ziel_x auf []
setze tr_ziel_y auf []
setze tr_ware auf []
setze tr_aktiv auf []

setze ti auf 0
solange ti < MAX_TRAEGER:
    tr_x.hinzufügen(0.0)
    tr_y.hinzufügen(0.0)
    tr_ziel_x.hinzufügen(0)
    tr_ziel_y.hinzufügen(0)
    tr_ware.hinzufügen(0)
    tr_aktiv.hinzufügen(falsch)
    setze ti auf ti + 1

# === RESSOURCEN ===
setze holz auf 20
setze stein auf 10
setze eisen auf 0
setze getreide auf 0
setze mehl auf 0
setze brot auf 0
setze werkzeug auf 0
setze bevoelkerung auf 5
setze max_bev auf 5

# Baukosten [holz, stein]
funktion bau_holz(typ):
    wenn typ == 1:
        gib_zurück 5
    wenn typ == 2:
        gib_zurück 3
    wenn typ == 3:
        gib_zurück 4
    wenn typ == 4:
        gib_zurück 6
    wenn typ == 5:
        gib_zurück 5
    wenn typ == 6:
        gib_zurück 8
    wenn typ == 7:
        gib_zurück 6
    gib_zurück 99

funktion bau_stein(typ):
    wenn typ == 2:
        gib_zurück 2
    wenn typ == 4:
        gib_zurück 3
    wenn typ == 5:
        gib_zurück 2
    wenn typ == 6:
        gib_zurück 5
    wenn typ == 7:
        gib_zurück 5
    gib_zurück 0

funktion gebaeude_platzieren(gx, gy, gtyp):
    # Pruefen ob Platz frei + richtiges Terrain
    setze terrain auf karte[gy * MAP_W + gx]
    wenn terrain == 0 oder terrain == 3:
        gib_zurück falsch
    # Holzfaeller braucht Wald nebenan
    wenn gtyp == 1:
        setze wald_nah auf falsch
        wenn gx > 0 und karte[gy * MAP_W + gx - 1] == 2:
            setze wald_nah auf wahr
        wenn gx < MAP_W - 1 und karte[gy * MAP_W + gx + 1] == 2:
            setze wald_nah auf wahr
        wenn gy > 0 und karte[(gy - 1) * MAP_W + gx] == 2:
            setze wald_nah auf wahr
        wenn gy < MAP_H - 1 und karte[(gy + 1) * MAP_W + gx] == 2:
            setze wald_nah auf wahr
        wenn nicht wald_nah:
            gib_zurück falsch
    # Steinbruch braucht Berg nebenan
    wenn gtyp == 2:
        setze berg_nah auf falsch
        wenn gx > 0 und karte[gy * MAP_W + gx - 1] == 3:
            setze berg_nah auf wahr
        wenn gx < MAP_W - 1 und karte[gy * MAP_W + gx + 1] == 3:
            setze berg_nah auf wahr
        wenn gy > 0 und karte[(gy - 1) * MAP_W + gx] == 3:
            setze berg_nah auf wahr
        wenn gy < MAP_H - 1 und karte[(gy + 1) * MAP_W + gx] == 3:
            setze berg_nah auf wahr
        wenn nicht berg_nah:
            gib_zurück falsch
    # Bauernhof: Gras drumherum wird Getreidefeld
    # Kosten pruefen
    setze kh auf bau_holz(gtyp)
    setze ks auf bau_stein(gtyp)
    wenn holz < kh oder stein < ks:
        gib_zurück falsch
    # Freien Slot finden
    setze slot auf -1
    setze gi auf 0
    solange gi < MAX_GEB:
        wenn nicht geb_aktiv[gi]:
            setze slot auf gi
            setze gi auf MAX_GEB
        setze gi auf gi + 1
    wenn slot < 0:
        gib_zurück falsch
    # Bauen
    setze holz auf holz - kh
    setze stein auf stein - ks
    setze geb_x[slot] auf gx
    setze geb_y[slot] auf gy
    setze geb_typ[slot] auf gtyp
    setze geb_aktiv[slot] auf wahr
    setze geb_timer[slot] auf 0
    setze geb_anzahl auf geb_anzahl + 1
    # Bauernhof: Nachbar-Gras → Getreidefeld
    wenn gtyp == 3:
        wenn gx > 0 und karte[gy * MAP_W + gx - 1] == 1:
            setze karte[gy * MAP_W + gx - 1] auf 4
        wenn gx < MAP_W - 1 und karte[gy * MAP_W + gx + 1] == 1:
            setze karte[gy * MAP_W + gx + 1] auf 4
        wenn gy > 0 und karte[(gy - 1) * MAP_W + gx] == 1:
            setze karte[(gy - 1) * MAP_W + gx] auf 4
        wenn gy < MAP_H - 1 und karte[(gy + 1) * MAP_W + gx] == 1:
            setze karte[(gy + 1) * MAP_W + gx] auf 4
    # Wohnhaus: erhoehe max_bev
    wenn gtyp == 6:
        setze max_bev auf max_bev + 4
    gib_zurück wahr

setze bau_typ auf 1
setze eingabe_cd auf 0
setze prod_timer auf 0

funktion tile_farbe(typ):
    wenn typ == 0:
        gib_zurück "#1565C0"
    wenn typ == 1:
        gib_zurück "#66BB6A"
    wenn typ == 2:
        gib_zurück "#2E7D32"
    wenn typ == 3:
        gib_zurück "#78909C"
    wenn typ == 4:
        gib_zurück "#FFD54F"
    gib_zurück "#000000"

funktion geb_farbe(typ):
    wenn typ == 1:
        gib_zurück "#5D4037"
    wenn typ == 2:
        gib_zurück "#607D8B"
    wenn typ == 3:
        gib_zurück "#FFA726"
    wenn typ == 4:
        gib_zurück "#BCAAA4"
    wenn typ == 5:
        gib_zurück "#D7CCC8"
    wenn typ == 6:
        gib_zurück "#EF5350"
    wenn typ == 7:
        gib_zurück "#37474F"
    gib_zurück "#000000"

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Siedler", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Kamera
    wenn eingabe_cd <= 0:
        wenn taste_gedrückt("w") und cam_y > 0:
            setze cam_y auf cam_y - 1
            setze eingabe_cd auf 3
        wenn taste_gedrückt("s") und cam_y < MAP_H - 20:
            setze cam_y auf cam_y + 1
            setze eingabe_cd auf 3
        wenn taste_gedrückt("a") und cam_x > 0:
            setze cam_x auf cam_x - 1
            setze eingabe_cd auf 3
        wenn taste_gedrückt("d") und cam_x < MAP_W - 30:
            setze cam_x auf cam_x + 1
            setze eingabe_cd auf 3

        # Bau-Typ waehlen
        wenn taste_gedrückt("1"):
            setze bau_typ auf 1
            setze eingabe_cd auf 10
        wenn taste_gedrückt("2"):
            setze bau_typ auf 2
            setze eingabe_cd auf 10
        wenn taste_gedrückt("3"):
            setze bau_typ auf 3
            setze eingabe_cd auf 10
        wenn taste_gedrückt("4"):
            setze bau_typ auf 4
            setze eingabe_cd auf 10
        wenn taste_gedrückt("5"):
            setze bau_typ auf 5
            setze eingabe_cd auf 10
        wenn taste_gedrückt("6"):
            setze bau_typ auf 6
            setze eingabe_cd auf 10
        wenn taste_gedrückt("7"):
            setze bau_typ auf 7
            setze eingabe_cd auf 10

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    # Maus-Platzierung
    setze mx auf maus_x(win)
    setze my auf maus_y(win)
    setze cursor_mx auf cam_x + mx / TILE
    setze cursor_my auf cam_y + my / TILE

    wenn maus_gedrückt(win) und my < HOEHE - 80 und eingabe_cd <= 0:
        wenn cursor_mx >= 0 und cursor_mx < MAP_W und cursor_my >= 0 und cursor_my < MAP_H:
            gebaeude_platzieren(cursor_mx, cursor_my, bau_typ)
            setze eingabe_cd auf 15

    # === PRODUKTION (alle 60 Frames) ===
    setze prod_timer auf prod_timer + 1
    wenn prod_timer >= 60:
        setze prod_timer auf 0
        setze gi auf 0
        solange gi < MAX_GEB:
            wenn geb_aktiv[gi]:
                # Holzfaeller → Holz
                wenn geb_typ[gi] == 1:
                    setze holz auf holz + 2
                # Steinbruch → Stein
                wenn geb_typ[gi] == 2:
                    setze stein auf stein + 1
                # Bauernhof → Getreide
                wenn geb_typ[gi] == 3:
                    setze getreide auf getreide + 2
                # Muehle: Getreide → Mehl
                wenn geb_typ[gi] == 4:
                    wenn getreide >= 2:
                        setze getreide auf getreide - 2
                        setze mehl auf mehl + 2
                # Baeckerei: Mehl → Brot
                wenn geb_typ[gi] == 5:
                    wenn mehl >= 2:
                        setze mehl auf mehl - 2
                        setze brot auf brot + 2
                # Schmiede: Eisen+Holz → Werkzeug
                wenn geb_typ[gi] == 7:
                    wenn eisen >= 1 und holz >= 1:
                        setze eisen auf eisen - 1
                        setze holz auf holz - 1
                        setze werkzeug auf werkzeug + 1
            setze gi auf gi + 1

        # Bevoelkerung waechst mit Brot
        wenn brot >= 2 und bevoelkerung < max_bev:
            setze brot auf brot - 2
            setze bevoelkerung auf bevoelkerung + 1

        # Traeger spawnen (visuell)
        setze gi auf 0
        solange gi < MAX_GEB:
            wenn geb_aktiv[gi]:
                setze geb_timer[gi] auf geb_timer[gi] + 1
                wenn geb_timer[gi] > 3:
                    setze geb_timer[gi] auf 0
                    # Traeger spawnen
                    setze ti auf 0
                    solange ti < MAX_TRAEGER:
                        wenn nicht tr_aktiv[ti]:
                            setze tr_x[ti] auf geb_x[gi] * TILE + TILE / 2.0
                            setze tr_y[ti] auf geb_y[gi] * TILE + TILE / 2.0
                            # Zufaelliges Nachbar-Gebaeude als Ziel
                            setze zg auf (gi + geb_timer[gi] * 3 + 1) % MAX_GEB
                            wenn geb_aktiv[zg]:
                                setze tr_ziel_x[ti] auf geb_x[zg] * TILE + TILE / 2
                                setze tr_ziel_y[ti] auf geb_y[zg] * TILE + TILE / 2
                                setze tr_aktiv[ti] auf wahr
                            setze ti auf MAX_TRAEGER
                        setze ti auf ti + 1
            setze gi auf gi + 1

    # Traeger bewegen
    setze ti auf 0
    solange ti < MAX_TRAEGER:
        wenn tr_aktiv[ti]:
            setze tdx auf tr_ziel_x[ti] - tr_x[ti]
            setze tdy auf tr_ziel_y[ti] - tr_y[ti]
            setze tdist auf tdx * tdx + tdy * tdy
            wenn tdist < 25:
                setze tr_aktiv[ti] auf falsch
            sonst:
                wenn tdx > 1:
                    setze tr_x[ti] auf tr_x[ti] + 0.8
                wenn tdx < -1:
                    setze tr_x[ti] auf tr_x[ti] - 0.8
                wenn tdy > 1:
                    setze tr_y[ti] auf tr_y[ti] + 0.8
                wenn tdy < -1:
                    setze tr_y[ti] auf tr_y[ti] - 0.8
        setze ti auf ti + 1

    # === ZEICHNEN ===
    fenster_löschen(win, "#1B5E20")

    setze draw_h auf (HOEHE - 80) / TILE
    setze draw_w auf BREITE / TILE

    # Terrain
    setze ty auf 0
    solange ty < draw_h:
        setze tx auf 0
        solange tx < draw_w:
            setze map_x auf cam_x + tx
            setze map_y auf cam_y + ty
            wenn map_x >= 0 und map_x < MAP_W und map_y >= 0 und map_y < MAP_H:
                setze tf auf tile_farbe(karte[map_y * MAP_W + map_x])
                zeichne_rechteck(win, tx * TILE, ty * TILE, TILE - 1, TILE - 1, tf)
            setze tx auf tx + 1
        setze ty auf ty + 1

    # Gebaeude
    setze gi auf 0
    solange gi < MAX_GEB:
        wenn geb_aktiv[gi]:
            setze gx auf (geb_x[gi] - cam_x) * TILE
            setze gy auf (geb_y[gi] - cam_y) * TILE
            wenn gx > -TILE und gx < BREITE und gy > -TILE und gy < HOEHE - 80:
                setze gf auf geb_farbe(geb_typ[gi])
                zeichne_rechteck(win, gx + 2, gy + 2, TILE - 4, TILE - 4, gf)
                # Dach
                zeichne_rechteck(win, gx + 4, gy, TILE - 8, 4, "#8D6E63")
        setze gi auf gi + 1

    # Traeger
    setze ti auf 0
    solange ti < MAX_TRAEGER:
        wenn tr_aktiv[ti]:
            setze tx auf tr_x[ti] - cam_x * TILE
            setze ty auf tr_y[ti] - cam_y * TILE
            wenn tx > -5 und tx < BREITE + 5 und ty > -5 und ty < HOEHE - 75:
                zeichne_kreis(win, tx, ty, 3, "#FFE0B2")
                zeichne_rechteck(win, tx - 1, ty + 3, 2, 4, "#5D4037")
        setze ti auf ti + 1

    # Cursor
    setze cx auf (cursor_mx - cam_x) * TILE
    setze cy auf (cursor_my - cam_y) * TILE
    wenn my < HOEHE - 80:
        setze gf auf geb_farbe(bau_typ)
        zeichne_rechteck(win, cx + 3, cy + 3, TILE - 6, TILE - 6, gf)

    # === HUD ===
    zeichne_rechteck(win, 0, HOEHE - 80, BREITE, 80, "#3E2723")

    # Ressourcen-Balken
    # Holz
    zeichne_rechteck(win, 10, HOEHE - 75, 8, 8, "#5D4037")
    setze rw auf holz
    wenn rw > 30:
        setze rw auf 30
    zeichne_rechteck(win, 22, HOEHE - 73, rw * 3, 4, "#8D6E63")

    # Stein
    zeichne_rechteck(win, 10, HOEHE - 62, 8, 8, "#78909C")
    setze rw auf stein
    wenn rw > 30:
        setze rw auf 30
    zeichne_rechteck(win, 22, HOEHE - 60, rw * 3, 4, "#90A4AE")

    # Getreide
    zeichne_rechteck(win, 120, HOEHE - 75, 8, 8, "#FFD54F")
    setze rw auf getreide
    wenn rw > 30:
        setze rw auf 30
    zeichne_rechteck(win, 132, HOEHE - 73, rw * 3, 4, "#FFF176")

    # Mehl
    zeichne_rechteck(win, 120, HOEHE - 62, 8, 8, "#EFEBE9")
    setze rw auf mehl
    wenn rw > 30:
        setze rw auf 30
    zeichne_rechteck(win, 132, HOEHE - 60, rw * 3, 4, "#D7CCC8")

    # Brot
    zeichne_rechteck(win, 240, HOEHE - 75, 8, 8, "#FF8F00")
    setze rw auf brot
    wenn rw > 30:
        setze rw auf 30
    zeichne_rechteck(win, 252, HOEHE - 73, rw * 3, 4, "#FFB300")

    # Bevoelkerung
    setze pi auf 0
    solange pi < bevoelkerung und pi < 20:
        zeichne_kreis(win, 380 + pi * 10, HOEHE - 68, 3, "#FFE0B2")
        setze pi auf pi + 1

    # Max Bev Marker
    zeichne_rechteck(win, 380 + max_bev * 10, HOEHE - 72, 2, 8, "#F44336")

    # Bau-Auswahl (unten rechts)
    setze bi auf 1
    solange bi <= 7:
        setze bx auf BREITE - 180 + (bi - 1) * 24
        wenn bi == bau_typ:
            zeichne_rechteck(win, bx, HOEHE - 45, 20, 20, "#FFFFFF")
        setze bf auf geb_farbe(bi)
        zeichne_rechteck(win, bx + 2, HOEHE - 43, 16, 16, bf)
        setze bi auf bi + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Siedler beendet! Bevoelkerung: " + text(bevoelkerung) + " Gebaeude: " + text(geb_anzahl)

# ============================================================
# moo Farm — Harvest Moon Style
#
# Kompilieren: moo-compiler compile beispiele/farm.moo -o beispiele/farm
# Starten:     ./beispiele/farm
#
# WASD = Bewegen, Leertaste = Pflanzen/Ernten/Interagieren
# 1=Karotten 2=Weizen 3=Tomaten waehlen
# E = Giessen
# Escape = Beenden
# ============================================================

setze BREITE auf 640
setze HOEHE auf 480
setze TILE auf 32
setze MAP_W auf 30
setze MAP_H auf 25

# Karte (0=Gras, 1=Acker, 2=Wasser, 3=Zaun, 4=Haus, 5=Scheune)
setze karte auf []
setze my auf 0
solange my < MAP_H:
    setze mx auf 0
    solange mx < MAP_W:
        # Rand = Zaun
        wenn mx == 0 oder mx == MAP_W - 1 oder my == 0 oder my == MAP_H - 1:
            karte.hinzufügen(3)
        sonst:
            # Teich
            wenn mx > 22 und mx < 27 und my > 3 und my < 8:
                karte.hinzufügen(2)
            sonst:
                # Haus
                wenn mx > 2 und mx < 6 und my > 1 und my < 4:
                    karte.hinzufügen(4)
                sonst:
                    # Scheune
                    wenn mx > 7 und mx < 11 und my > 1 und my < 4:
                        karte.hinzufügen(5)
                    sonst:
                        # Acker-Bereich
                        wenn mx > 3 und mx < 20 und my > 5 und my < 20:
                            karte.hinzufügen(1)
                        sonst:
                            karte.hinzufügen(0)
        setze mx auf mx + 1
    setze my auf my + 1

# Felder (Pflanzen auf Acker)
# Status: 0=leer, 1=gesaet, 2=wachsend, 3=reif
setze feld_status auf []
setze feld_typ auf []
setze feld_wasser auf []
setze feld_timer auf []

setze fi auf 0
solange fi < MAP_W * MAP_H:
    feld_status.hinzufügen(0)
    feld_typ.hinzufügen(0)
    feld_wasser.hinzufügen(0)
    feld_timer.hinzufügen(0)
    setze fi auf fi + 1

# Spieler
setze spieler_x auf 4
setze spieler_y auf 8
setze spieler_richtung auf 0

# Inventar
setze gold auf 50
setze karotten auf 0
setze weizen_inv auf 0
setze tomaten auf 0
setze saat_typ auf 1
setze tag auf 1
setze stunde auf 6

setze eingabe_cd auf 0
setze wachstum_timer auf 0

# Kamera
setze cam_x auf 0
setze cam_y auf 0

funktion tile_farbe(typ):
    wenn typ == 0:
        gib_zurück "#66BB6A"
    wenn typ == 1:
        gib_zurück "#8D6E63"
    wenn typ == 2:
        gib_zurück "#42A5F5"
    wenn typ == 3:
        gib_zurück "#795548"
    wenn typ == 4:
        gib_zurück "#EF5350"
    wenn typ == 5:
        gib_zurück "#FF8F00"
    gib_zurück "#000000"

funktion pflanze_farbe(typ, status_val):
    wenn status_val == 0:
        gib_zurück "#000000"
    wenn status_val == 1:
        gib_zurück "#A5D6A7"
    wenn status_val == 2:
        wenn typ == 1:
            gib_zurück "#FF8A65"
        wenn typ == 2:
            gib_zurück "#FFF176"
        wenn typ == 3:
            gib_zurück "#EF5350"
        gib_zurück "#4CAF50"
    wenn status_val == 3:
        wenn typ == 1:
            gib_zurück "#E65100"
        wenn typ == 2:
            gib_zurück "#F9A825"
        wenn typ == 3:
            gib_zurück "#C62828"
        gib_zurück "#2E7D32"
    gib_zurück "#000000"

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Farm", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn eingabe_cd <= 0:
        # Bewegung
        wenn taste_gedrückt("w") und spieler_y > 1:
            setze ziel auf karte[(spieler_y - 1) * MAP_W + spieler_x]
            wenn ziel != 2 und ziel != 3:
                setze spieler_y auf spieler_y - 1
            setze spieler_richtung auf 3
            setze eingabe_cd auf 6
        wenn taste_gedrückt("s") und spieler_y < MAP_H - 2:
            setze ziel auf karte[(spieler_y + 1) * MAP_W + spieler_x]
            wenn ziel != 2 und ziel != 3:
                setze spieler_y auf spieler_y + 1
            setze spieler_richtung auf 0
            setze eingabe_cd auf 6
        wenn taste_gedrückt("a") und spieler_x > 1:
            setze ziel auf karte[spieler_y * MAP_W + spieler_x - 1]
            wenn ziel != 2 und ziel != 3:
                setze spieler_x auf spieler_x - 1
            setze spieler_richtung auf 1
            setze eingabe_cd auf 6
        wenn taste_gedrückt("d") und spieler_x < MAP_W - 2:
            setze ziel auf karte[spieler_y * MAP_W + spieler_x + 1]
            wenn ziel != 2 und ziel != 3:
                setze spieler_x auf spieler_x + 1
            setze spieler_richtung auf 2
            setze eingabe_cd auf 6

        # Saat waehlen
        wenn taste_gedrückt("1"):
            setze saat_typ auf 1
            setze eingabe_cd auf 10
        wenn taste_gedrückt("2"):
            setze saat_typ auf 2
            setze eingabe_cd auf 10
        wenn taste_gedrückt("3"):
            setze saat_typ auf 3
            setze eingabe_cd auf 10

        # Pflanzen / Ernten
        wenn taste_gedrückt("leertaste"):
            setze fidx auf spieler_y * MAP_W + spieler_x
            wenn karte[fidx] == 1:
                wenn feld_status[fidx] == 0 und gold >= 5:
                    # Saeen
                    setze feld_status[fidx] auf 1
                    setze feld_typ[fidx] auf saat_typ
                    setze feld_timer[fidx] auf 0
                    setze gold auf gold - 5
                wenn feld_status[fidx] == 3:
                    # Ernten
                    setze feld_status[fidx] auf 0
                    wenn feld_typ[fidx] == 1:
                        setze karotten auf karotten + 3
                        setze gold auf gold + 15
                    wenn feld_typ[fidx] == 2:
                        setze weizen_inv auf weizen_inv + 3
                        setze gold auf gold + 10
                    wenn feld_typ[fidx] == 3:
                        setze tomaten auf tomaten + 3
                        setze gold auf gold + 20
            setze eingabe_cd auf 10

        # Giessen
        wenn taste_gedrückt("e"):
            setze fidx auf spieler_y * MAP_W + spieler_x
            wenn karte[fidx] == 1 und feld_status[fidx] > 0:
                setze feld_wasser[fidx] auf 3
            setze eingabe_cd auf 8

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    # Kamera folgt Spieler
    setze cam_x auf spieler_x - BREITE / TILE / 2
    setze cam_y auf spieler_y - (HOEHE - 60) / TILE / 2
    wenn cam_x < 0:
        setze cam_x auf 0
    wenn cam_y < 0:
        setze cam_y auf 0
    wenn cam_x > MAP_W - BREITE / TILE:
        setze cam_x auf MAP_W - BREITE / TILE
    wenn cam_y > MAP_H - (HOEHE - 60) / TILE:
        setze cam_y auf MAP_H - (HOEHE - 60) / TILE

    # Wachstum
    setze wachstum_timer auf wachstum_timer + 1
    wenn wachstum_timer >= 120:
        setze wachstum_timer auf 0
        setze stunde auf stunde + 1
        wenn stunde >= 24:
            setze stunde auf 6
            setze tag auf tag + 1
        setze fi auf 0
        solange fi < MAP_W * MAP_H:
            wenn feld_status[fi] > 0 und feld_status[fi] < 3:
                setze feld_timer[fi] auf feld_timer[fi] + 1
                # Wasser beschleunigt
                wenn feld_wasser[fi] > 0:
                    setze feld_timer[fi] auf feld_timer[fi] + 1
                    setze feld_wasser[fi] auf feld_wasser[fi] - 1
                # Wachstumsstufen
                wenn feld_status[fi] == 1 und feld_timer[fi] > 3:
                    setze feld_status[fi] auf 2
                wenn feld_status[fi] == 2 und feld_timer[fi] > 8:
                    setze feld_status[fi] auf 3
            setze fi auf fi + 1

    # === ZEICHNEN ===
    # Himmelfarbe basierend auf Stunde
    wenn stunde > 6 und stunde < 18:
        fenster_löschen(win, "#87CEEB")
    sonst:
        fenster_löschen(win, "#1A237E")

    setze draw_h auf (HOEHE - 60) / TILE
    setze draw_w auf BREITE / TILE

    setze ty auf 0
    solange ty < draw_h:
        setze tx auf 0
        solange tx < draw_w:
            setze map_x auf cam_x + tx
            setze map_y auf cam_y + ty
            wenn map_x >= 0 und map_x < MAP_W und map_y >= 0 und map_y < MAP_H:
                setze tidx auf map_y * MAP_W + map_x
                setze tf auf tile_farbe(karte[tidx])
                zeichne_rechteck(win, tx * TILE, ty * TILE, TILE - 1, TILE - 1, tf)

                # Pflanzen
                wenn feld_status[tidx] > 0:
                    setze pf auf pflanze_farbe(feld_typ[tidx], feld_status[tidx])
                    setze ph auf 4 + feld_status[tidx] * 6
                    zeichne_rechteck(win, tx * TILE + 8, ty * TILE + TILE - ph - 2, 4, ph, "#4CAF50")
                    zeichne_kreis(win, tx * TILE + 10, ty * TILE + TILE - ph - 2, 5, pf)
                    # Wasser-Tropfen
                    wenn feld_wasser[tidx] > 0:
                        zeichne_kreis(win, tx * TILE + 22, ty * TILE + 6, 3, "#42A5F5")

            setze tx auf tx + 1
        setze ty auf ty + 1

    # Spieler
    setze px auf (spieler_x - cam_x) * TILE + TILE / 2
    setze py auf (spieler_y - cam_y) * TILE + TILE / 2
    zeichne_kreis(win, px, py, 10, "#FFE0B2")
    zeichne_rechteck(win, px - 6, py + 10, 12, 10, "#5D4037")
    # Hut
    zeichne_rechteck(win, px - 8, py - 14, 16, 6, "#FFA726")
    zeichne_rechteck(win, px - 5, py - 18, 10, 6, "#FFA726")

    # === HUD ===
    zeichne_rechteck(win, 0, HOEHE - 60, BREITE, 60, "#3E2723")

    # Gold
    zeichne_kreis(win, 25, HOEHE - 40, 8, "#FFD700")
    setze gw auf gold
    wenn gw > 50:
        setze gw auf 50
    zeichne_rechteck(win, 40, HOEHE - 44, gw * 2, 8, "#FFF176")

    # Inventar
    # Karotten
    zeichne_kreis(win, 170, HOEHE - 48, 5, "#E65100")
    setze rw auf karotten
    wenn rw > 15:
        setze rw auf 15
    zeichne_rechteck(win, 180, HOEHE - 50, rw * 4, 4, "#FF8A65")

    # Weizen
    zeichne_kreis(win, 170, HOEHE - 33, 5, "#F9A825")
    setze rw auf weizen_inv
    wenn rw > 15:
        setze rw auf 15
    zeichne_rechteck(win, 180, HOEHE - 35, rw * 4, 4, "#FFF176")

    # Tomaten
    zeichne_kreis(win, 170, HOEHE - 18, 5, "#C62828")
    setze rw auf tomaten
    wenn rw > 15:
        setze rw auf 15
    zeichne_rechteck(win, 180, HOEHE - 20, rw * 4, 4, "#EF5350")

    # Saat-Auswahl
    wenn saat_typ == 1:
        zeichne_rechteck(win, 320, HOEHE - 50, 24, 24, "#FFFFFF")
    zeichne_rechteck(win, 322, HOEHE - 48, 20, 20, "#E65100")

    wenn saat_typ == 2:
        zeichne_rechteck(win, 350, HOEHE - 50, 24, 24, "#FFFFFF")
    zeichne_rechteck(win, 352, HOEHE - 48, 20, 20, "#F9A825")

    wenn saat_typ == 3:
        zeichne_rechteck(win, 380, HOEHE - 50, 24, 24, "#FFFFFF")
    zeichne_rechteck(win, 382, HOEHE - 48, 20, 20, "#C62828")

    # Tag/Stunde
    setze di auf 0
    solange di < tag und di < 15:
        zeichne_pixel(win, BREITE - 20 - di * 5, HOEHE - 20, "#FFFFFF")
        zeichne_pixel(win, BREITE - 20 - di * 5, HOEHE - 21, "#FFFFFF")
        setze di auf di + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Farm beendet! Tag: " + text(tag) + " Gold: " + text(gold)

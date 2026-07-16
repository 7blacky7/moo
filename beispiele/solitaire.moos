# ============================================================
# moo Solitaire — Klondike Kartenspiel
#
# Kompilieren: moo-compiler compile beispiele/solitaire.moo -o beispiele/solitaire
# Starten:     ./beispiele/solitaire
#
# Maus = Karte waehlen + ablegen
# Leertaste = Karte vom Stapel ziehen
# R = Neustart, Escape = Beenden
# ============================================================

setze BREITE auf 700
setze HOEHE auf 600
setze KARTEN_W auf 60
setze KARTEN_H auf 80
setze TOTAL auf 52

# Karten: 0-12 = Herz, 13-25 = Karo, 26-38 = Pik, 39-51 = Kreuz
# Wert 0-12 (Ass bis Koenig)
setze karten auf []
setze karten_offen auf []
setze karten_pos auf []

# Mischen
setze ki auf 0
solange ki < TOTAL:
    karten.hinzufügen(ki)
    karten_offen.hinzufügen(falsch)
    karten_pos.hinzufügen(0)
    setze ki auf ki + 1

# Fisher-Yates Shuffle (vereinfacht)
setze ki auf TOTAL - 1
solange ki > 0:
    setze ji auf (ki * 7 + 13) % (ki + 1)
    setze temp auf karten[ki]
    setze karten[ki] auf karten[ji]
    setze karten[ji] auf temp
    setze ki auf ki - 1

# 7 Tableau-Spalten
setze tab auf []
setze tab_len auf []
setze ti auf 0
solange ti < 7:
    tab_len.hinzufügen(0)
    setze ti auf ti + 1

# 49 Slots fuer Tableau (7 Spalten x max 7 Karten)
setze ti auf 0
solange ti < 49:
    tab.hinzufügen(-1)
    setze ti auf ti + 1

# Karten verteilen
setze ki auf 0
setze col auf 0
solange col < 7:
    setze row auf 0
    solange row <= col:
        setze tab[col * 7 + row] auf karten[ki]
        wenn row == col:
            setze karten_offen[karten[ki]] auf wahr
        setze tab_len[col] auf row + 1
        setze ki auf ki + 1
        setze row auf row + 1
    setze col auf col + 1

# Reststapel
setze stapel auf []
setze stapel_idx auf 0
solange ki < TOTAL:
    stapel.hinzufügen(karten[ki])
    setze ki auf ki + 1
setze stapel_len auf länge(stapel)
setze stapel_top auf -1

# Foundation (4 Stapel, je Farbe)
setze found auf []
setze fi auf 0
solange fi < 4:
    found.hinzufügen(-1)
    setze fi auf fi + 1

setze auswahl auf -1
setze auswahl_src auf -1
setze eingabe_cd auf 0
setze gewonnen auf falsch

funktion karten_farbe_idx(karte):
    gib_zurück karte / 13

funktion karten_wert(karte):
    gib_zurück karte % 13

funktion ist_rot(karte):
    setze farbe auf karte / 13
    gib_zurück farbe == 0 oder farbe == 1

funktion karte_farbe(karte):
    wenn karte < 0:
        gib_zurück "#1B5E20"
    wenn nicht karten_offen[karte]:
        gib_zurück "#1565C0"
    wenn ist_rot(karte):
        gib_zurück "#F44336"
    gib_zurück "#212121"

funktion farbe_symbol_farbe(karte):
    wenn ist_rot(karte):
        gib_zurück "#F44336"
    gib_zurück "#212121"

# === HAUPTPROGRAMM ===
setze win auf fenster_erstelle("moo Solitaire", BREITE, HOEHE)

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    # Karte vom Stapel
    wenn taste_gedrückt("leertaste") und eingabe_cd <= 0:
        wenn stapel_idx < stapel_len:
            setze stapel_top auf stapel[stapel_idx]
            setze karten_offen[stapel_top] auf wahr
            setze stapel_idx auf stapel_idx + 1
        sonst:
            setze stapel_idx auf 0
            setze stapel_top auf -1
        setze eingabe_cd auf 15

    wenn eingabe_cd > 0:
        setze eingabe_cd auf eingabe_cd - 1

    # Gewinn-Check
    setze alle_voll auf wahr
    setze fi auf 0
    solange fi < 4:
        wenn found[fi] < 12:
            setze alle_voll auf falsch
        setze fi auf fi + 1
    wenn alle_voll:
        setze gewonnen auf wahr

    # === ZEICHNEN ===
    fenster_löschen(win, "#1B5E20")

    # Stapel (oben links)
    zeichne_rechteck(win, 20, 20, KARTEN_W, KARTEN_H, "#1565C0")
    zeichne_rechteck(win, 22, 22, KARTEN_W - 4, KARTEN_H - 4, "#1E88E5")

    # Aufgedeckte Stapel-Karte
    wenn stapel_top >= 0:
        setze kf auf karte_farbe(stapel_top)
        zeichne_rechteck(win, 90, 20, KARTEN_W, KARTEN_H, "#FFFFFF")
        zeichne_rechteck(win, 92, 22, KARTEN_W - 4, KARTEN_H - 4, "#FAFAFA")
        # Wert
        setze wert auf karten_wert(stapel_top)
        setze sf auf farbe_symbol_farbe(stapel_top)
        zeichne_kreis(win, 120, 55, 8, sf)
        # Wert als Balken
        setze wert_i auf 0
        solange wert_i <= wert und wert_i < 13:
            zeichne_rechteck(win, 95 + wert_i * 4, 80, 3, 5, sf)
            setze wert_i auf wert_i + 1

    # Foundation (oben rechts)
    setze fi auf 0
    solange fi < 4:
        setze fx auf 350 + fi * 80
        zeichne_rechteck(win, fx, 20, KARTEN_W, KARTEN_H, "#2E7D32")
        wenn found[fi] >= 0:
            zeichne_rechteck(win, fx + 2, 22, KARTEN_W - 4, KARTEN_H - 4, "#FFFFFF")
            setze wert auf found[fi]
            setze wert_i auf 0
            solange wert_i <= wert und wert_i < 13:
                zeichne_rechteck(win, fx + 5 + wert_i * 4, 80, 3, 5, "#4CAF50")
                setze wert_i auf wert_i + 1
        setze fi auf fi + 1

    # Tableau (7 Spalten)
    setze col auf 0
    solange col < 7:
        setze tx auf 20 + col * 95
        setze row auf 0
        solange row < tab_len[col]:
            setze karte auf tab[col * 7 + row]
            wenn karte >= 0:
                setze ty auf 130 + row * 22
                wenn karten_offen[karte]:
                    zeichne_rechteck(win, tx, ty, KARTEN_W, KARTEN_H, "#FFFFFF")
                    setze sf auf farbe_symbol_farbe(karte)
                    zeichne_kreis(win, tx + 30, ty + 35, 8, sf)
                    setze wert auf karten_wert(karte)
                    setze wert_i auf 0
                    solange wert_i <= wert und wert_i < 13:
                        zeichne_rechteck(win, tx + 5 + wert_i * 4, ty + 60, 3, 5, sf)
                        setze wert_i auf wert_i + 1
                sonst:
                    zeichne_rechteck(win, tx, ty, KARTEN_W, KARTEN_H, "#1565C0")
                    zeichne_rechteck(win, tx + 5, ty + 5, KARTEN_W - 10, KARTEN_H - 10, "#1E88E5")
            setze row auf row + 1
        setze col auf col + 1

    # Gewonnen
    wenn gewonnen:
        zeichne_rechteck(win, BREITE / 2 - 120, HOEHE / 2 - 30, 240, 60, "#FFD700")
        zeichne_rechteck(win, BREITE / 2 - 110, HOEHE / 2 - 20, 220, 40, "#FFF176")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
wenn gewonnen:
    zeige "Gewonnen!"
sonst:
    zeige "Solitaire beendet."

# ============================================================
# simon.moo — Simon Says Memory-Spiel
#
# Kompilieren: moo-compiler compile beispiele/simon.moo -o beispiele/simon
# Starten:     ./beispiele/simon
#
# Steuerung: Mausklick auf Farbe nachdem Sequenz gezeigt wurde
# Ziel: Merke dir die Farbsequenz und klicke sie nach
# ============================================================

konstante WIN_W auf 500
konstante WIN_H auf 500
konstante CX auf 250
konstante CY auf 250
konstante R_AUSSEN auf 200
konstante R_INNEN auf 80

# Farben: 0=rot(oben), 1=gruen(rechts), 2=blau(unten), 3=gelb(links)
funktion farbe_normal(f):
    wenn f == 0: gib_zurück "#B71C1C"
    wenn f == 1: gib_zurück "#1B5E20"
    wenn f == 2: gib_zurück "#0D47A1"
    wenn f == 3: gib_zurück "#F57F17"
    gib_zurück "#000000"

funktion farbe_hell(f):
    wenn f == 0: gib_zurück "#FF5252"
    wenn f == 1: gib_zurück "#69F0AE"
    wenn f == 2: gib_zurück "#448AFF"
    wenn f == 3: gib_zurück "#FFEE58"
    gib_zurück "#FFFFFF"

# PRNG
setze si_seed auf 42

funktion si_rand(max_val):
    setze si_seed auf (si_seed * 1103515245 + 12345) % 2147483648
    gib_zurück si_seed % max_val

# State
setze sequenz auf []
setze spieler_idx auf 0
setze phase auf 0
setze zeige_idx auf 0
setze zeige_timer auf 0
setze zeige_licht auf -1
setze round_val auf 0
setze game_over auf falsch
setze klick_cd auf 0

# Phasen: 0=zeigen, 1=spielen, 2=ok, 3=fail

funktion neue_runde():
    sequenz.hinzufügen(si_rand(4))
    setze spieler_idx auf 0
    setze phase auf 0
    setze zeige_idx auf 0
    setze zeige_timer auf 0
    setze zeige_licht auf -1
    setze round_val auf round_val + 1

funktion neu_starten():
    setze sequenz auf []
    setze spieler_idx auf 0
    setze phase auf 0
    setze round_val auf 0
    setze game_over auf falsch
    neue_runde()

# Welches Quadrant? (Maus-Klick)
funktion welche_farbe(mx, my):
    setze dx auf mx - CX
    setze dy auf my - CY
    setze dist_sq auf dx * dx + dy * dy
    wenn dist_sq < R_INNEN * R_INNEN oder dist_sq > R_AUSSEN * R_AUSSEN:
        gib_zurück -1
    # Quadrant: oben=0, rechts=1, unten=2, links=3
    wenn abs(dy) > abs(dx):
        wenn dy < 0: gib_zurück 0
        gib_zurück 2
    sonst:
        wenn dx > 0: gib_zurück 1
        gib_zurück 3

# === Hauptprogramm ===
zeige "=== moo Simon Says ==="
zeige "Schaue die Sequenz. Klicke sie nach!"

setze win auf fenster_erstelle("moo Simon", WIN_W, WIN_H)
setze si_seed auf zeit_ms() % 99991
neu_starten()

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn klick_cd > 0:
        setze klick_cd auf klick_cd - 1

    wenn taste_gedrückt("r") und klick_cd == 0:
        setze si_seed auf zeit_ms() % 99991
        neu_starten()
        setze klick_cd auf 15

    # Phase 0: Sequenz zeigen
    wenn phase == 0:
        setze zeige_timer auf zeige_timer + 1
        wenn zeige_licht == -1:
            # Licht an
            wenn zeige_timer >= 30:
                wenn zeige_idx < länge(sequenz):
                    setze zeige_licht auf sequenz[zeige_idx]
                    setze zeige_timer auf 0
                sonst:
                    setze phase auf 1
                    setze spieler_idx auf 0
                    setze zeige_timer auf 0
        sonst:
            # Licht aus
            wenn zeige_timer >= 40:
                setze zeige_licht auf -1
                setze zeige_idx auf zeige_idx + 1
                setze zeige_timer auf 0

    # Phase 1: Spieler-Eingabe
    wenn phase == 1 und maus_gedrückt(win) und klick_cd == 0:
        setze mx auf maus_x(win)
        setze my auf maus_y(win)
        setze geklickt auf welche_farbe(mx, my)
        wenn geklickt >= 0:
            # Licht kurz anzeigen
            setze zeige_licht auf geklickt
            setze zeige_timer auf 0
            setze klick_cd auf 15
            # Richtig?
            wenn geklickt == sequenz[spieler_idx]:
                setze spieler_idx auf spieler_idx + 1
                wenn spieler_idx >= länge(sequenz):
                    setze phase auf 2
                    setze zeige_timer auf 0
            sonst:
                setze phase auf 3
                setze game_over auf wahr

    # Licht zurücksetzen nach Klick in Phase 1
    wenn phase == 1 und zeige_licht != -1:
        setze zeige_timer auf zeige_timer + 1
        wenn zeige_timer >= 20:
            setze zeige_licht auf -1

    # Phase 2: OK, nächste Runde
    wenn phase == 2:
        setze zeige_timer auf zeige_timer + 1
        wenn zeige_timer >= 40:
            neue_runde()

    # === Zeichnen ===
    fenster_löschen(win, "#212121")

    # 4 Quadranten
    setze qi auf 0
    solange qi < 4:
        setze aktiv auf zeige_licht == qi
        setze farbe auf farbe_normal(qi)
        wenn aktiv:
            setze farbe auf farbe_hell(qi)

        # Rechteckiger Quadrant (angenähert)
        wenn qi == 0:
            # Oben
            zeichne_rechteck(win, CX - R_AUSSEN, CY - R_AUSSEN, R_AUSSEN * 2, R_AUSSEN - R_INNEN / 2, farbe)
        wenn qi == 1:
            # Rechts
            zeichne_rechteck(win, CX + R_INNEN / 2, CY - R_AUSSEN, R_AUSSEN - R_INNEN / 2, R_AUSSEN * 2, farbe)
        wenn qi == 2:
            # Unten
            zeichne_rechteck(win, CX - R_AUSSEN, CY + R_INNEN / 2, R_AUSSEN * 2, R_AUSSEN - R_INNEN / 2, farbe)
        wenn qi == 3:
            # Links
            zeichne_rechteck(win, CX - R_AUSSEN, CY - R_AUSSEN, R_AUSSEN - R_INNEN / 2, R_AUSSEN * 2, farbe)

        setze qi auf qi + 1

    # Mitte (schwarzer Kreis)
    zeichne_kreis(win, CX, CY, R_INNEN, "#212121")
    zeichne_kreis(win, CX, CY, R_INNEN - 4, "#424242")

    # Runden-Anzeige in der Mitte
    setze ri auf 0
    solange ri < round_val und ri < 10:
        setze angle auf ri * 6.28 / 10
        setze dx auf CX + sinus(angle) * (R_INNEN - 20)
        setze dy auf CY - cosinus(angle) * (R_INNEN - 20)
        zeichne_kreis(win, dx, dy, 6, "#FFD700")
        setze ri auf ri + 1

    # Game Over
    wenn game_over:
        zeichne_rechteck(win, CX - 60, CY - 20, 120, 40, "#B71C1C")
        zeichne_rechteck(win, CX - 58, CY - 18, 116, 36, "#D32F2F")
        zeichne_linie(win, CX - 15, CY - 10, CX + 15, CY + 10, "#FFFFFF")
        zeichne_linie(win, CX + 15, CY - 10, CX - 15, CY + 10, "#FFFFFF")

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Simon beendet. Runde: " + text(round_val)

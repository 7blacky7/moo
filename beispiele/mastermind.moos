# ============================================================
# moo Mastermind-Solver mit SDL2 GUI
#
# Kompilieren: moo-compiler compile mastermind.moo -o mastermind
# Starten:     ./mastermind
#
# Bedienung:
#   1. Klick auf die 4 Slots oben, um den geheimen Code zu setzen
#      (jeder Klick rotiert durch 6 Farben)
#   2. Klick auf START → KI loest per Knuth-basiertem Minimax
#   3. Klick auf NEU   → Code wird zurueckgesetzt
#
# Algorithmus:
#   Knuth's Minimax-Strategie. Initialer Guess: 1 1 2 2.
#   Nach jedem Feedback werden alle inkompatiblen Kandidaten
#   aus S entfernt. Der naechste Guess wird aus allen 1296
#   Kombis gewaehlt (wenn moeglich aus S), sodass die groesste
#   resultierende Partition im schlimmsten Fall minimal ist.
#   Garantiert Loesung in max 5 Versuchen.
# ============================================================

konstante SLOTS auf 4
konstante ANZ_FARBEN auf 6
konstante MAX_RUNDEN auf 10
konstante FENSTER_B auf 520
konstante FENSTER_H auf 740
konstante PEG_R auf 18
konstante ROW_H auf 50
konstante ROW_Y0 auf 140
konstante SLOT_X0 auf 50
konstante SLOT_DX auf 60
konstante FEEDBACK_X auf 300
konstante FEEDBACK_DX auf 22

setze FARBEN auf ["rot", "gruen", "blau", "gelb", "magenta", "orange"]

# --- 3x5 Mini-Font (nur was wir anzeigen) ---
setze FONT auf {}
FONT["0"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["1"] = [0,1,0, 1,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["2"] = [1,1,1, 0,0,1, 1,1,1, 1,0,0, 1,1,1]
FONT["3"] = [1,1,1, 0,0,1, 1,1,1, 0,0,1, 1,1,1]
FONT["4"] = [1,0,1, 1,0,1, 1,1,1, 0,0,1, 0,0,1]
FONT["5"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
FONT["6"] = [1,1,1, 1,0,0, 1,1,1, 1,0,1, 1,1,1]
FONT["7"] = [1,1,1, 0,0,1, 0,1,0, 0,1,0, 0,1,0]
FONT["8"] = [1,1,1, 1,0,1, 1,1,1, 1,0,1, 1,1,1]
FONT["9"] = [1,1,1, 1,0,1, 1,1,1, 0,0,1, 1,1,1]
FONT[" "] = [0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0]
FONT[":"] = [0,0,0, 0,1,0, 0,0,0, 0,1,0, 0,0,0]
FONT["S"] = [1,1,1, 1,0,0, 1,1,1, 0,0,1, 1,1,1]
FONT["T"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 0,1,0]
FONT["A"] = [0,1,0, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
FONT["R"] = [1,1,1, 1,0,1, 1,1,0, 1,0,1, 1,0,1]
FONT["N"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]
FONT["E"] = [1,1,1, 1,0,0, 1,1,0, 1,0,0, 1,1,1]
FONT["U"] = [1,0,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["W"] = [1,0,1, 1,0,1, 1,1,1, 1,1,1, 1,0,1]
FONT["I"] = [1,1,1, 0,1,0, 0,1,0, 0,1,0, 1,1,1]
FONT["L"] = [1,0,0, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["O"] = [1,1,1, 1,0,1, 1,0,1, 1,0,1, 1,1,1]
FONT["G"] = [1,1,1, 1,0,0, 1,0,1, 1,0,1, 1,1,1]
FONT["C"] = [1,1,1, 1,0,0, 1,0,0, 1,0,0, 1,1,1]
FONT["H"] = [1,0,1, 1,0,1, 1,1,1, 1,0,1, 1,0,1]
FONT["P"] = [1,1,1, 1,0,1, 1,1,1, 1,0,0, 1,0,0]
FONT["D"] = [1,1,0, 1,0,1, 1,0,1, 1,0,1, 1,1,0]
FONT["M"] = [1,0,1, 1,1,1, 1,1,1, 1,0,1, 1,0,1]

funktion zeichne_bitmap(fenster, bits, x, y, px, farbe):
    setze zy auf 0
    solange zy < 5:
        setze zx auf 0
        solange zx < 3:
            wenn bits[zy * 3 + zx] == 1:
                zeichne_rechteck(fenster, x + zx * px, y + zy * px, px, px, farbe)
            setze zx auf zx + 1
        setze zy auf zy + 1

funktion zeichne_text(fenster, s, x, y, px, farbe):
    setze i auf 0
    setze cx auf x
    solange i < länge(s):
        setze ch auf s[i]
        wenn FONT.enthält(ch):
            zeichne_bitmap(fenster, FONT[ch], cx, y, px, farbe)
        setze cx auf cx + 4 * px
        setze i auf i + 1

# --- Mastermind-Logik ---

# Kodiere Kombi als Zahl: c0 + c1*6 + c2*36 + c3*216. 0..1295.
funktion dekodiere(idx):
    setze a auf []
    a.hinzufügen(idx % 6)
    a.hinzufügen(boden(idx / 6) % 6)
    a.hinzufügen(boden(idx / 36) % 6)
    a.hinzufügen(boden(idx / 216) % 6)
    gib_zurück a

funktion kodiere(kombi):
    gib_zurück kombi[0] + kombi[1] * 6 + kombi[2] * 36 + kombi[3] * 216

# Vergleiche Guess vs Code. Gibt [schwarz, weiss] zurueck.
# schwarz: gleiche Farbe, gleiche Position.
# weiss: Farbe kommt vor, aber nicht an der Position, nicht doppelt gezaehlt.
funktion feedback(guess, code):
    setze b auf 0
    setze i auf 0
    solange i < 4:
        wenn guess[i] == code[i]:
            setze b auf b + 1
        setze i auf i + 1
    # Farb-Histogramm ohne die schwarz-Stellen
    setze hg auf [0, 0, 0, 0, 0, 0]
    setze hc auf [0, 0, 0, 0, 0, 0]
    setze i auf 0
    solange i < 4:
        wenn guess[i] != code[i]:
            hg[guess[i]] = hg[guess[i]] + 1
            hc[code[i]] = hc[code[i]] + 1
        setze i auf i + 1
    setze w auf 0
    setze k auf 0
    solange k < 6:
        wenn hg[k] < hc[k]:
            setze w auf w + hg[k]
        sonst:
            setze w auf w + hc[k]
        setze k auf k + 1
    gib_zurück [b, w]

# Pack (b, w) als kleine Zahl fuer schnelles Gruppieren.
funktion fb_key(fb):
    gib_zurück fb[0] * 5 + fb[1]

funktion alle_kombis():
    setze liste auf []
    setze i auf 0
    solange i < 1296:
        liste.hinzufügen(i)
        setze i auf i + 1
    gib_zurück liste

# Minimax-Auswahl nach Knuth.
# kandidaten = Indizes der verbleibenden S.
# alle = alle 1296 Indizes (oder kleinere Obermenge).
funktion minimax_pick(kandidaten, alle):
    # Spezialfall: |S| = 1 oder |S| = 2 → nimm ersten
    wenn kandidaten.länge() <= 2:
        gib_zurück kandidaten[0]

    setze best_score auf 9999
    setze best_idx auf alle[0]
    setze best_in_s auf falsch

    setze i auf 0
    setze n auf alle.länge()
    solange i < n:
        setze cand auf alle[i]
        setze cand_dec auf dekodiere(cand)

        # Zaehle Partitionsgroessen via Feedback-Histogramm
        setze buckets auf []
        setze k auf 0
        solange k < 30:
            buckets.hinzufügen(0)
            setze k auf k + 1

        setze max_bucket auf 0
        setze j auf 0
        setze ns auf kandidaten.länge()
        solange j < ns:
            setze s_dec auf dekodiere(kandidaten[j])
            setze fb auf feedback(cand_dec, s_dec)
            setze key auf fb_key(fb)
            buckets[key] = buckets[key] + 1
            wenn buckets[key] > max_bucket:
                setze max_bucket auf buckets[key]
            setze j auf j + 1

        # Ist cand in S?
        setze in_s auf falsch
        setze m auf 0
        solange m < ns:
            wenn kandidaten[m] == cand:
                setze in_s auf wahr
                setze m auf ns
            sonst:
                setze m auf m + 1

        # Bevorzuge kleineren score; bei Gleichstand: S-Mitglied vor Nicht-S-Mitglied
        wenn max_bucket < best_score:
            setze best_score auf max_bucket
            setze best_idx auf cand
            setze best_in_s auf in_s
        sonst:
            wenn max_bucket == best_score und in_s und nicht best_in_s:
                setze best_idx auf cand
                setze best_in_s auf wahr

        setze i auf i + 1
    gib_zurück best_idx

# Filter S: nur noch Kandidaten, die zum letzten Feedback passen.
funktion filter_s(s, guess_dec, fb):
    setze next auf []
    setze i auf 0
    setze n auf s.länge()
    solange i < n:
        setze c_dec auf dekodiere(s[i])
        setze fb2 auf feedback(guess_dec, c_dec)
        wenn fb2[0] == fb[0] und fb2[1] == fb[1]:
            next.hinzufügen(s[i])
        setze i auf i + 1
    gib_zurück next

# --- Zeichnen ---

funktion zeichne_peg(fenster, x, y, farbe_idx):
    setze f auf FARBEN[farbe_idx]
    zeichne_kreis(fenster, x, y, PEG_R, f)
    zeichne_kreis(fenster, x, y, PEG_R - 3, f)

funktion zeichne_leer_peg(fenster, x, y):
    zeichne_kreis(fenster, x, y, PEG_R, "dunkelgrau")
    zeichne_kreis(fenster, x, y, PEG_R - 3, "schwarz")

funktion zeichne_feedback(fenster, x, y, b, w):
    setze i auf 0
    solange i < 4:
        setze fx auf x + i * FEEDBACK_DX
        wenn i < b:
            zeichne_kreis(fenster, fx, y, 6, "schwarz")
        sonst:
            wenn i < b + w:
                zeichne_kreis(fenster, fx, y, 6, "weiss")
            sonst:
                zeichne_kreis(fenster, fx, y, 5, "dunkelgrau")
        setze i auf i + 1

funktion zeichne_geheim(fenster, code, zeige_sichtbar):
    zeichne_text(fenster, "GEHEIM:", SLOT_X0, 60, 3, "weiss")
    setze i auf 0
    solange i < SLOTS:
        setze x auf SLOT_X0 + i * SLOT_DX + 20
        setze y auf 100
        wenn zeige_sichtbar:
            zeichne_peg(fenster, x, y, code[i])
        sonst:
            zeichne_leer_peg(fenster, x, y)
            zeichne_text(fenster, "?", x - 3, y - 7, 3, "weiss")
        setze i auf i + 1

funktion zeichne_versuche(fenster, versuche, feedbacks):
    setze i auf 0
    setze n auf versuche.länge()
    solange i < n:
        setze y auf ROW_Y0 + i * ROW_H
        setze g auf versuche[i]
        setze j auf 0
        solange j < SLOTS:
            zeichne_peg(fenster, SLOT_X0 + j * SLOT_DX + 20, y, g[j])
            setze j auf j + 1
        setze fb auf feedbacks[i]
        zeichne_feedback(fenster, FEEDBACK_X, y, fb[0], fb[1])
        # Zug-Nummer
        zeichne_text(fenster, text(i + 1), 20, y - 7, 2, "weiss")
        setze i auf i + 1

funktion zeichne_farbpalette(fenster):
    setze i auf 0
    solange i < ANZ_FARBEN:
        setze x auf 30 + i * 45
        setze y auf FENSTER_H - 110
        zeichne_kreis(fenster, x + 18, y + 18, 16, FARBEN[i])
        setze i auf i + 1

funktion zeichne_knopf(fenster, x, y, b, h, label, bg, vg):
    zeichne_rechteck(fenster, x, y, b, h, bg)
    zeichne_text(fenster, label, x + 10, y + 10, 3, vg)

funktion zeichne_buttons(fenster, phase):
    zeichne_knopf(fenster, 30, FENSTER_H - 60, 120, 40, "NEU", "rot", "weiss")
    wenn phase == "setup":
        zeichne_knopf(fenster, 180, FENSTER_H - 60, 180, 40, "START", "gruen", "weiss")
    wenn phase == "gewonnen":
        zeichne_knopf(fenster, 180, FENSTER_H - 60, 180, 40, "GEWONNEN", "gruen", "schwarz")
    wenn phase == "verloren":
        zeichne_knopf(fenster, 180, FENSTER_H - 60, 180, 40, "VERLOREN", "magenta", "weiss")

funktion im_rect(mx, my, x, y, b, h):
    gib_zurück mx >= x und mx < x + b und my >= y und my < y + h

# --- Main ---

zeige "=== moo Mastermind-Solver ==="
zeige "Knuth Minimax, 4 Slots x 6 Farben, max 10 Versuche"

# Initial-Kombi (der klassische Knuth-Opener: 1 1 2 2)
funktion initial_guess():
    gib_zurück kodiere([0, 0, 1, 1])

setze geheim auf [0, 0, 0, 0]
setze versuche auf []
setze feedbacks auf []
setze phase auf "setup"
setze vorher_maus auf falsch

setze fenster auf fenster_erstelle("moo Mastermind", FENSTER_B, FENSTER_H)

solange fenster_offen(fenster):
    setze mx auf maus_x(fenster)
    setze my auf maus_y(fenster)
    setze jetzt auf maus_gedrückt(fenster)

    wenn jetzt und nicht vorher_maus:
        # Geheim-Slots anklicken: rotieren
        wenn phase == "setup":
            setze i auf 0
            solange i < SLOTS:
                setze sx auf SLOT_X0 + i * SLOT_DX + 20
                wenn im_rect(mx, my, sx - 20, 80, 40, 40):
                    geheim[i] = (geheim[i] + 1) % 6
                setze i auf i + 1

        # NEU
        wenn im_rect(mx, my, 30, FENSTER_H - 60, 120, 40):
            setze geheim auf [0, 0, 0, 0]
            setze versuche auf []
            setze feedbacks auf []
            setze phase auf "setup"

        # START → Solver-Loop
        wenn phase == "setup" und im_rect(mx, my, 180, FENSTER_H - 60, 180, 40):
            setze phase auf "loesen"
            setze S auf alle_kombis()
            setze alle auf alle_kombis()
            setze guess_idx auf initial_guess()
            setze runde auf 0

            solange runde < MAX_RUNDEN:
                setze guess_dec auf dekodiere(guess_idx)
                setze fb auf feedback(guess_dec, geheim)
                versuche.hinzufügen(guess_dec)
                feedbacks.hinzufügen(fb)

                wenn fb[0] == 4:
                    setze phase auf "gewonnen"
                    setze runde auf MAX_RUNDEN
                sonst:
                    setze S auf filter_s(S, guess_dec, fb)
                    wenn S.länge() == 0:
                        setze phase auf "verloren"
                        setze runde auf MAX_RUNDEN
                    sonst:
                        setze guess_idx auf minimax_pick(S, alle)
                        setze runde auf runde + 1

            wenn phase == "loesen":
                setze phase auf "verloren"

    setze vorher_maus auf jetzt

    # --- Render ---
    fenster_löschen(fenster, "schwarz")
    zeichne_text(fenster, "MASTERMIND", 150, 20, 4, "weiss")
    zeichne_geheim(fenster, geheim, phase != "setup")
    zeichne_versuche(fenster, versuche, feedbacks)
    zeichne_farbpalette(fenster)
    zeichne_buttons(fenster, phase)
    fenster_aktualisieren(fenster)

zeige "Mastermind beendet"

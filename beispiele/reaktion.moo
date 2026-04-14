# ============================================================
# reaktion.moo — Reaktionstest
#
# Kompilieren: moo-compiler compile beispiele/reaktion.moo -o beispiele/reaktion
# Starten:     ./beispiele/reaktion
#
# Steuerung: Mausklick wenn Bildschirm GRUEN wird!
# Ziel: Schnellstmoegliche Reaktion
# ============================================================

konstante WIN_W auf 600
konstante WIN_H auf 400

# Phasen:
# 0 = idle (Klick zum Starten)
# 1 = warten (rot, zufaellig 1-4 Sek)
# 2 = jetzt! (gruen, Zeit messen)
# 3 = ergebnis
# 4 = fehlstart (zu frueh geklickt)

setze phase auf 0
setze start_zeit auf 0
setze reaktion_zeit auf 0
setze warte_dauer auf 0
setze timer auf 0
setze klick_cd auf 0

setze versuche auf 0
setze best_zeit auf 9999
setze summe auf 0

# PRNG
setze rt_seed auf 42

funktion rt_rand(min_v, max_v):
    setze rt_seed auf (rt_seed * 1103515245 + 12345) % 2147483648
    gib_zurück min_v + rt_seed % (max_v - min_v)

# === Hauptprogramm ===
zeige "=== moo Reaktionstest ==="
zeige "Klick wenn der Bildschirm GRUEN wird!"

setze win auf fenster_erstelle("moo Reaktion", WIN_W, WIN_H)
setze rt_seed auf zeit_ms() % 99991

solange fenster_offen(win):
    wenn taste_gedrückt("escape"):
        stopp

    wenn klick_cd > 0:
        setze klick_cd auf klick_cd - 1

    setze geklickt auf maus_gedrückt(win) und klick_cd == 0

    wenn phase == 0 und geklickt:
        # Start
        setze phase auf 1
        setze warte_dauer auf rt_rand(60, 240)
        setze timer auf 0
        setze klick_cd auf 20

    wenn phase == 1:
        setze timer auf timer + 1
        wenn geklickt:
            # Fehlstart
            setze phase auf 4
            setze klick_cd auf 30
        sonst wenn timer >= warte_dauer:
            setze phase auf 2
            setze start_zeit auf zeit_ms()

    wenn phase == 2 und geklickt:
        setze reaktion_zeit auf zeit_ms() - start_zeit
        setze phase auf 3
        setze versuche auf versuche + 1
        setze summe auf summe + reaktion_zeit
        wenn reaktion_zeit < best_zeit:
            setze best_zeit auf reaktion_zeit
        setze klick_cd auf 30

    wenn phase == 3 und geklickt:
        setze phase auf 0
        setze klick_cd auf 20

    wenn phase == 4 und geklickt:
        setze phase auf 0
        setze klick_cd auf 20

    # === Zeichnen ===
    # Hintergrund nach Phase
    wenn phase == 0:
        fenster_löschen(win, "#37474F")
    wenn phase == 1:
        fenster_löschen(win, "#D32F2F")
    wenn phase == 2:
        fenster_löschen(win, "#4CAF50")
    wenn phase == 3:
        fenster_löschen(win, "#1565C0")
    wenn phase == 4:
        fenster_löschen(win, "#F57F17")

    # Symbole fuer Phase
    wenn phase == 0:
        # Spielball (Klick-Hinweis)
        zeichne_kreis(win, WIN_W / 2, WIN_H / 2, 40, "#FFFFFF")
        zeichne_kreis(win, WIN_W / 2, WIN_H / 2, 30, "#37474F")
        zeichne_kreis(win, WIN_W / 2, WIN_H / 2, 10, "#FFFFFF")

    wenn phase == 1:
        # Warten: Stoppuhr
        zeichne_kreis(win, WIN_W / 2, WIN_H / 2, 50, "#FFFFFF")
        zeichne_kreis(win, WIN_W / 2, WIN_H / 2, 42, "#D32F2F")
        zeichne_rechteck(win, WIN_W / 2 - 2, WIN_H / 2 - 32, 4, 30, "#FFFFFF")

    wenn phase == 2:
        # JETZT! Grosser Blitz
        zeichne_kreis(win, WIN_W / 2, WIN_H / 2, 60, "#FFFFFF")
        zeichne_kreis(win, WIN_W / 2, WIN_H / 2, 48, "#4CAF50")
        zeichne_rechteck(win, WIN_W / 2 - 4, WIN_H / 2 - 30, 8, 60, "#FFFFFF")

    wenn phase == 3:
        # Ergebnis: Reaktion als Punkte (ms / 10)
        setze punkte auf reaktion_zeit / 10
        wenn punkte > 40:
            setze punkte auf 40
        setze pi auf 0
        solange pi < punkte:
            zeichne_kreis(win, 50 + pi * 12, WIN_H / 2, 4, "#FFD700")
            setze pi auf pi + 1
        # Best-Zeit
        setze best_punkte auf best_zeit / 10
        wenn best_punkte > 40:
            setze best_punkte auf 40
        setze bi auf 0
        solange bi < best_punkte:
            zeichne_kreis(win, 50 + bi * 12, WIN_H / 2 + 30, 3, "#4CAF50")
            setze bi auf bi + 1

    wenn phase == 4:
        # Fehlstart: X
        zeichne_linie(win, WIN_W / 2 - 30, WIN_H / 2 - 30, WIN_W / 2 + 30, WIN_H / 2 + 30, "#FFFFFF")
        zeichne_linie(win, WIN_W / 2 + 30, WIN_H / 2 - 30, WIN_W / 2 - 30, WIN_H / 2 + 30, "#FFFFFF")
        zeichne_linie(win, WIN_W / 2 - 32, WIN_H / 2 - 30, WIN_W / 2 + 28, WIN_H / 2 + 30, "#FFFFFF")
        zeichne_linie(win, WIN_W / 2 + 32, WIN_H / 2 - 30, WIN_W / 2 - 28, WIN_H / 2 + 30, "#FFFFFF")

    # HUD unten
    zeichne_rechteck(win, 0, WIN_H - 30, WIN_W, 30, "#1A1A2E")
    # Versuche
    setze vi auf 0
    solange vi < versuche und vi < 30:
        zeichne_kreis(win, 10 + vi * 10, WIN_H - 15, 2, "#42A5F5")
        setze vi auf vi + 1

    fenster_aktualisieren(win)
    warte(16)

fenster_schliessen(win)
zeige "Reaktion beendet. Beste Zeit: " + text(best_zeit) + "ms"

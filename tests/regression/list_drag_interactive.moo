# Interaktiver Drag-Test: laeuft Event-Loop, druckt col-Breiten in Intervallen,
# damit ein externes xdotool-Skript Drag-Events injizieren kann und der Test
# beobachtet wie sich die Spaltenbreite aendert.
importiere ui

setze fenster auf ui_fenster("Drag-Diag-Live", 700, 400, 1, 0, nichts)
ui_fenster_position_setze(fenster, 50, 50)
setze l auf ui_liste(fenster, ["A", "B", "C"], 10, 10, 680, 380)
ui_liste_zeile_hinzu(l, ["a1", "b1", "c1"])
ui_liste_zeile_hinzu(l, ["a2", "b2", "c2"])
ui_liste_zeile_hinzu(l, ["a3", "b3", "c3"])
ui_zeige(fenster)

# Polling-Loop: alle 250ms Breiten ausgeben, exit nach 8 Sekunden
setze i auf 0
solange i < 32:
    ui_test_pump()
    ui_test_warte(250)
    zeige("tick=" + text(i) + " col0=" + text(ui_liste_spalte_breite_lese(l, 0)) + " col1=" + text(ui_liste_spalte_breite_lese(l, 1)) + " col2=" + text(ui_liste_spalte_breite_lese(l, 2)))
    setze i auf i + 1

ui_test_snapshot(fenster, "/tmp/list_drag_after.png")
zeige("== EXIT ==")

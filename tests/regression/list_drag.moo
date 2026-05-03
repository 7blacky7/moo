# tests/regression/list_drag.moo
# Beweist Drag-/Resize-Faehigkeit von ui_liste-Spalten via Programmatic-API.
#
# Test 1: ui_liste_spalte_breite(l, idx, px) setzt die Breite -> Lesen via
#         ui_liste_spalte_breite_lese muss den neuen Wert liefern (+/- Slop).
# Test 2: ui_liste_spalte_min_breite und Re-Read.
#
# Der visuelle User-Drag testet sich nicht ohne OS-Mouse-Synth - dafuer
# ist run_list_drag.sh der Wrapper (xdotool/ydotool).

importiere ui

setze fenster auf ui_fenster("Drag-Diag", 700, 400, 1, 0, nichts)
setze l auf ui_liste(fenster, ["A", "B", "C"], 10, 10, 680, 380)
ui_liste_zeile_hinzu(l, ["a1", "b1", "c1"])
ui_liste_zeile_hinzu(l, ["a2", "b2", "c2"])
ui_liste_zeile_hinzu(l, ["a3", "b3", "c3"])

ui_zeige(fenster)
ui_test_pump()
ui_test_warte(150)

zeige("== T0: nach Show, vor jedem set ==")
zeige("col0=" + text(ui_liste_spalte_breite_lese(l, 0)))
zeige("col1=" + text(ui_liste_spalte_breite_lese(l, 1)))
zeige("col2=" + text(ui_liste_spalte_breite_lese(l, 2)))

ui_liste_spalte_breite(l, 0, 200)
ui_liste_spalte_breite(l, 1, 120)
ui_test_pump()
ui_test_warte(100)

zeige("== T1: nach ui_liste_spalte_breite(0,200) + (1,120) ==")
zeige("col0=" + text(ui_liste_spalte_breite_lese(l, 0)))
zeige("col1=" + text(ui_liste_spalte_breite_lese(l, 1)))
zeige("col2=" + text(ui_liste_spalte_breite_lese(l, 2)))

ui_liste_spalte_min_breite(l, 0, 80)
ui_liste_spalte_breite(l, 0, 80)
ui_test_pump()
ui_test_warte(100)

zeige("== T2: nach min_breite(0,80) + breite(0,80) ==")
zeige("col0=" + text(ui_liste_spalte_breite_lese(l, 0)))

ui_test_snapshot(fenster, "/tmp/list_drag_t2.png")

zeige("== TEST FERTIG ==")

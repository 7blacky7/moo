# ============================================================
# beispiele/tests/ui_moo_container_test.moo — UIMOO-5 Container
#
# Panel:  Knopf bei (20,40) IM Panel (Panel bei 20,20) -> Klick auf
#         absolute (10+40+30, 10+60+16) muss den Knopf treffen.
# Liste:  20 Zeilen, Höhe 5*zh -> Klick wählt Zeile, programm.
#         Scroll (scroll_y) verschiebt die Klick-Zuordnung korrekt.
# Scroll: Knopf bei y=150 im Scrollbereich (Höhe 100, inhalt 300);
#         nach scroll_y=100 trifft Klick bei lokal y=50 den Knopf.
# Marker: UIMOO5-CONTAINER-OK
# ============================================================

importiere ui
importiere ui_moo

setze g auf {}
g["panel_klick"] = 0
g["scroll_klick"] = 0
g["auswahl"] = 0 - 1

funktion klick_panel(w):
    g["panel_klick"] = g["panel_klick"] + 1
    gib_zurück wahr

funktion klick_scroll(w):
    g["scroll_klick"] = g["scroll_klick"] + 1
    gib_zurück wahr

funktion liste_auswahl(w, idx):
    g["auswahl"] = idx
    gib_zurück wahr

funktion pruefe():
    # 1. Knopf im Panel: Panel (20,20), Knopf relativ (20,40) 120x32
    #    absolut auf Leinwand: 40..160 x 60..92 -> Fenster +10.
    ui_test_klick_xy(g["hFenster"], 10 + 70, 10 + 76)
    ui_test_pump()

    # 2. Liste (280,20) 200 breit: Klick auf Zeile 2 (zh = 13+8 = 21)
    #    Zeile 2 beginnt bei y = 20 + 2*21 = 62 -> Mitte 72.
    ui_test_klick_xy(g["hFenster"], 10 + 300, 10 + 72)
    ui_test_pump()
    setze auswahl_ohne_scroll auf g["auswahl"]

    # 3. Liste programmatisch scrollen (2 Zeilen) -> selber Klickpunkt
    #    muss jetzt Zeile 4 treffen.
    g["li"]["scroll_y"] = 42
    uim_neuzeichnen(g["k"])
    ui_test_klick_xy(g["hFenster"], 10 + 300, 10 + 72)
    ui_test_pump()
    setze auswahl_mit_scroll auf g["auswahl"]

    # 4. Scrollbereich (20,200) 200x100, inhalt 300; Knopf relativ
    #    (20,150) 120x32. Ungescrollt unsichtbar/untreffbar; nach
    #    scroll_y=120 liegt er bei lokal y=30..62 -> absolut 230..262.
    ui_test_klick_xy(g["hFenster"], 10 + 70, 10 + 240)
    ui_test_pump()
    setze vor_scroll auf g["scroll_klick"]
    g["sc"]["scroll_y"] = 120
    uim_neuzeichnen(g["k"])
    ui_test_klick_xy(g["hFenster"], 10 + 70, 10 + 240)
    ui_test_pump()

    zeige "UIMOO5 panel=" + text(g["panel_klick"]) + " liste=" + text(auswahl_ohne_scroll) + "/" + text(auswahl_mit_scroll) + " scroll=" + text(vor_scroll) + "->" + text(g["scroll_klick"])
    wenn g["panel_klick"] == 1 und auswahl_ohne_scroll == 2 und auswahl_mit_scroll == 4 und vor_scroll == 0 und g["scroll_klick"] == 1:
        zeige "UIMOO5-CONTAINER-OK"
    sonst:
        zeige "UIMOO5-CONTAINER-FEHLER"
    ui_beenden()
    gib_zurück wahr

g["hFenster"] = ui_fenster("UIMOO5-Container", 600, 460, 0, nichts)
g["k"] = uim_wurzel(g["hFenster"], 10, 10, 580, 400)

g["pn"] = uim_hinzu(g["k"], uim_panel("Einstellungen", 20, 20, 200, 120))
uim_hinzu(g["pn"], uim_knopf("Im Panel", 20, 40, 120, 32, klick_panel))

setze zeilen auf []
setze i auf 0
solange i < 20:
    zeilen.hinzufügen("Zeile " + text(i))
    setze i auf i + 1
g["li"] = uim_hinzu(g["k"], uim_liste(280, 20, 200, 105, zeilen, liste_auswahl))

g["sc"] = uim_hinzu(g["k"], uim_scroll(20, 200, 200, 100, 300))
uim_hinzu(g["sc"], uim_knopf("Tief unten", 20, 150, 120, 32, klick_scroll))

ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(500, pruefe)
ui_laufen()

# ============================================================
# beispiele/tests/ui_moo_kern_test.moo — Kern-Test UIMOO-2
#
# Headless-Verifikation des ui_moo-Kerns über test_klick_xy:
#   1. Wurzel + 2 Knöpfe + Label anlegen
#   2. Klick auf Knopf A -> on_klick feuert, Fokus auf A
#   3. Klick auf Knopf B -> Zähler B, Fokus wechselt
#   4. Klick ins Leere    -> kein Callback, Fokus weg
#   5. Theme-Wechsel + uim_finde per ID
# Erwartete Marker: UIMOO2-A=1 B=1, UIMOO2-KERN-OK
# ============================================================

importiere ui
importiere ui_moo

setze g auf {}
g["a"] = 0
g["b"] = 0

funktion klick_a(w):
    g["a"] = g["a"] + 1
    gib_zurück wahr

funktion klick_b(w):
    g["b"] = g["b"] + 1
    gib_zurück wahr

funktion pruefe():
    # Leinwand liegt bei 10,10 -> Fenster-Koordinaten = Widget + 10.
    ui_test_klick_xy(g["hFenster"], 10 + 60, 10 + 36)    # Knopf A (20,20 120x32)
    ui_test_pump()
    ui_test_klick_xy(g["hFenster"], 10 + 220, 10 + 36)   # Knopf B (160,20 120x32)
    ui_test_pump()
    ui_test_klick_xy(g["hFenster"], 10 + 400, 10 + 300)  # Leere Fläche
    ui_test_pump()

    setze fokus_leer auf g["k"]["fokus"] == nichts
    uim_theme_setze(g["k"], uim_theme_hell())
    setze gef auf uim_finde(g["k"], "knopf-b")
    setze finde_ok auf falsch
    wenn gef != nichts:
        wenn gef["uid"] == g["wb"]["uid"]:
            setze finde_ok auf wahr

    zeige "UIMOO2-A=" + text(g["a"]) + " B=" + text(g["b"])
    wenn g["a"] == 1 und g["b"] == 1 und fokus_leer und finde_ok:
        zeige "UIMOO2-KERN-OK"
    sonst:
        zeige "UIMOO2-KERN-FEHLER fokus_leer=" + text(fokus_leer) + " finde=" + text(finde_ok)
    ui_beenden()
    gib_zurück wahr

g["hFenster"] = ui_fenster("UIMOO2-Kern", 600, 460, 0, nichts)
g["k"] = uim_wurzel(g["hFenster"], 10, 10, 580, 400)

g["wa"] = uim_hinzu(g["k"], uim_knopf("A", 20, 20, 120, 32, klick_a))
g["wb"] = uim_hinzu(g["k"], uim_knopf("B", 160, 20, 120, 32, klick_b))
g["wb"]["id"] = "knopf-b"
uim_hinzu(g["k"], uim_label("ui_moo Kern-Test", 20, 70, 200, 20))

ui_zeige_nebenbei(g["hFenster"])
ui_timer_hinzu(500, pruefe)
ui_laufen()

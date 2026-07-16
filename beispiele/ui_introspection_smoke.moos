# ============================================================
# ui_introspection_smoke.moo — Smoke-Test Plan-004 P1 (GTK)
# ============================================================

importiere ui


funktion pruefe():
    setze f auf g["fenster"]

    setze id_zurueck auf ui_widget_id_hole(g["knopf_ok"])
    zeige "id_hole(knopf_ok): " + id_zurueck

    setze gefunden auf ui_widget_suche(f, "btnOk")
    wenn gefunden == nichts:
        zeige "FEHLER: suche btnOk ist nichts"
    sonst:
        zeige "OK: suche btnOk hat Treffer"

    setze info auf ui_widget_info(g["knopf_ok"])
    setze typ auf info["typ"]
    setze txt auf info["text"]
    setze idv auf info["id"]
    setze sicht auf info["sichtbar"]
    zeige "info typ=" + typ + " text=" + txt + " id=" + idv
    zeige "info sichtbar=" + sicht

    setze baum auf ui_widget_baum(f)
    zeige "baum-laenge=" + länge(baum)
    fuer eintrag in baum:
        setze t auf eintrag["tiefe"]
        setze ty auf eintrag["typ"]
        setze i auf eintrag["id"]
        zeige "  tiefe=" + t + " typ=" + ty + " id=" + i

    setze nichts_da auf ui_widget_suche(f, "gibts_nicht")
    zeige "suche unbekannt=" + nichts_da

    ui_beenden()


setze g auf {}
setze g["fenster"] auf ui_fenster("Introspection-Smoke", 300, 200, 0, nichts)

setze g["label"] auf ui_label(g["fenster"], "Hallo", 10, 10, 200, 24)
ui_widget_id_setze(g["label"], "lblHello")

setze g["knopf_ok"] auf ui_knopf(g["fenster"], "OK", 10, 40, 100, 30, nichts)
ui_widget_id_setze(g["knopf_ok"], "btnOk")

setze g["eingabe"] auf ui_eingabe(g["fenster"], 10, 80, 200, 30, "Name", falsch)
ui_widget_id_setze(g["eingabe"], "inpName")
ui_eingabe_setze(g["eingabe"], "Moritz")

ui_widget_id_setze(g["label"], "")
zeige "label id nach Loeschen: " + ui_widget_id_hole(g["label"])
ui_widget_id_setze(g["label"], "lblHello")

ui_zeige_nebenbei(g["fenster"])
ui_timer_hinzu(200, pruefe)
ui_laufen()

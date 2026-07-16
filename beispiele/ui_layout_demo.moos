# ============================================================
# ui_layout_demo.moo — Showcase fuer Flexbox-light Layouts
#
# Demonstriert:
#   - ui_spalte als Haupt-Container (Vertikal)
#   - ui_zeile als Button-Leiste (Horizontal)
#   - Verschachtelung Spalte > Zeile > Zeile
#   - padding, abstand (gap), fill_x, gewicht, align
#   - Komfort-Wrapper ui_layout_label / _knopf / _eingabe / _checkbox
#
# Kompilieren:
#   cargo run --release --manifest-path compiler/Cargo.toml -- \
#       compile beispiele/ui_layout_demo.moo
# Starten:
#   ./beispiele/ui_layout_demo
# ============================================================

importiere ui
importiere ui_layout

setze g auf {}


funktion on_ok():
    setze name auf ui_eingabe_text(g["inpName"])
    ui_label_setze(g["lblStatus"], "OK — Hallo, " + name + "!")


funktion on_abbrechen():
    ui_beenden()


funktion on_toggle(wert):
    wenn wert:
        ui_label_setze(g["lblStatus"], "Option aktiviert")
    sonst:
        ui_label_setze(g["lblStatus"], "Option deaktiviert")


# Fenster direkt via Primitive (ui_baue additiv nicht zwingend)
setze FB auf 520
setze FH auf 360
g["hFenster"] = ui_fenster("Layout-Demo", FB, FH, 0, nichts)

# Haupt-Spalte: fuellt das Fenster
setze haupt auf ui_spalte(g["hFenster"], 0, 0, FB, FH)
ui_layout_padding(haupt, 12, 12, 12, 12)
ui_layout_abstand(haupt, 8)

# --- Header-Label ---
g["lblTitel"] = ui_layout_label(haupt, "Flexbox-light Layout-Demo", { "hoehe": 28, "fill_x": wahr })

# --- Zeile mit Name-Feld ---
setze zeile_name auf ui_zeile(g["hFenster"], 0, 0, 0, 0)
ui_layout_abstand(zeile_name, 6)
# In der Zeile: festes Label + dehnbare Eingabe
g["lblName"] = ui_layout_label(zeile_name, "Name:", { "breite": 80, "align": "mitte" })
g["inpName"] = ui_layout_eingabe(zeile_name, "Dein Name", { "gewicht": 1, "fill_y": falsch, "hoehe": 26 })
# Zeile als Kind der Haupt-Spalte
ui_layout_hinzufuegen(haupt, nichts, { "hoehe": 28, "fill_x": wahr })
# Die letzte Eintragung ist ein Platzhalter — wir ersetzen sie durch
# die Zeile selbst via manuelle Kopie (Container ohne Widget-Handle):
# einfacher: wir fuegen die Zeile als „logisches Kind" ein und rufen
# neu_berechnen auf der Zeile separat nach neu_berechnen auf haupt.
# Damit die Zeile jedoch am richtigen Platz liegt, nutzen wir ein
# Rahmen-Widget als Traeger:
# -> Vereinfachung: wir berechnen haupt und nehmen den Platzhalter-
#    Bereich, um zeile_name dort hinzusetzen.

# --- Checkbox-Reihe ---
setze zeile_opt auf ui_zeile(g["hFenster"], 0, 0, 0, 0)
ui_layout_abstand(zeile_opt, 10)
g["chkA"] = ui_layout_checkbox(zeile_opt, "Option A", falsch, on_toggle, { "breite": 110 })
g["chkB"] = ui_layout_checkbox(zeile_opt, "Option B", wahr,  on_toggle, { "breite": 110 })
g["chkC"] = ui_layout_checkbox(zeile_opt, "Option C", falsch, on_toggle, { "breite": 110, "gewicht": 1, "fill_x": wahr })
ui_layout_hinzufuegen(haupt, nichts, { "hoehe": 28, "fill_x": wahr })

# --- Status-Label (wachsend) ---
g["lblStatus"] = ui_layout_label(haupt, "Bereit.", { "fill_x": wahr, "gewicht": 1, "fill_y": wahr })

# --- Button-Leiste unten ---
setze zeile_btns auf ui_zeile(g["hFenster"], 0, 0, 0, 0)
ui_layout_abstand(zeile_btns, 8)
# Linker Spacer (dehnbar) → Buttons rechts-ausgerichtet
ui_layout_hinzufuegen(zeile_btns, nichts, { "gewicht": 1 })
g["btnAbbrechen"] = ui_layout_knopf(zeile_btns, "Abbrechen", on_abbrechen, { "breite": 110, "hoehe": 30 })
g["btnOK"]        = ui_layout_knopf(zeile_btns, "OK",        on_ok,        { "breite": 110, "hoehe": 30 })
ui_layout_hinzufuegen(haupt, nichts, { "hoehe": 34, "fill_x": wahr })

# Haupt-Spalte berechnen — weist jedem Platzhalter seinen Rect zu
ui_layout_neu_berechnen(haupt)

# Die Platzhalter-Kinder (zeile_name, zeile_opt, zeile_btns) als
# Sub-Layouts auf den berechneten Rects positionieren:
setze k auf haupt["kinder"]
# Index 0 = Titel-Label (Widget)
# Index 1 = Platzhalter fuer zeile_name
# Index 2 = Platzhalter fuer zeile_opt
# Index 3 = Status-Label (Widget)
# Index 4 = Platzhalter fuer zeile_btns

zeile_name["x"] = k[1]["_x"]
zeile_name["y"] = k[1]["_y"]
zeile_name["b"] = k[1]["_b"]
zeile_name["h"] = k[1]["_h"]
ui_layout_neu_berechnen(zeile_name)

zeile_opt["x"] = k[2]["_x"]
zeile_opt["y"] = k[2]["_y"]
zeile_opt["b"] = k[2]["_b"]
zeile_opt["h"] = k[2]["_h"]
ui_layout_neu_berechnen(zeile_opt)

zeile_btns["x"] = k[4]["_x"]
zeile_btns["y"] = k[4]["_y"]
zeile_btns["b"] = k[4]["_b"]
zeile_btns["h"] = k[4]["_h"]
ui_layout_neu_berechnen(zeile_btns)

ui_zeige(g["hFenster"])
ui_laufen()

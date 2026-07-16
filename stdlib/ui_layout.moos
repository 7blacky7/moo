# ============================================================
# stdlib/ui_layout.moo — Flexbox-light Layout-Primitive (Schicht 2)
#
# Additiv zu ui_baue. ui_baue bleibt unveraendert — Layout-Funktionen
# bauen Container innerhalb eines Parents und platzieren vorhandene
# x/y/b/h-Primitive (ui_label, ui_knopf, ui_eingabe, ...).
#
# Ziel (Plan-003 P1, @gpt-Klaerung id=6713):
#   - Simple, berechenbare Layouts
#   - KEINE komplette CSS/Flexbox-Engine
#   - ui_spalte + ui_zeile zuerst, Grid spaeter
#
# ⚠ Dict-Zugriff strikt mit Bracket-Syntax (`L["kinder"]` statt `L.kinder`)
# wegen bekanntem Dot-Access-Bug auf Pointer-tagged MooValues.
#
# === Modell ===
# Ein Layout ist ein Dict mit:
#   richtung  : "spalte" | "zeile"
#   parent    : Parent-Widget (Fenster o.ae.)
#   x, y, b, h: Bereich relativ zum Parent
#   pad_oben/rechts/unten/links : Innen-Abstand (0)
#   gap       : Abstand zwischen Kindern (0)
#   kinder    : Liste von Kind-Dicts mit:
#                 widget     : Widget-Handle (oder nichts fuer reine Platzhalter)
#                 breite     : feste Breite (oder -1 = fill)
#                 hoehe      : feste Hoehe  (oder -1 = fill)
#                 fill_x     : bool — in Querrichtung strecken (Spalte: X-fill)
#                 fill_y     : bool — in Querrichtung strecken (Zeile: Y-fill)
#                 gewicht    : Flex-Gewicht in Hauptrichtung (0 = fix, >0 = flex)
#                 align      : "start" | "mitte" | "ende" (in Querrichtung)
#                 min_breite : Minimal-Breite (0)
#                 min_hoehe  : Minimal-Hoehe  (0)
# ============================================================


# ---------- interne Helfer ----------

funktion _ui_layout_opt(opts, schluessel, fallback):
    wenn opts == nichts:
        gib_zurück fallback
    wenn opts.hat(schluessel):
        gib_zurück opts[schluessel]
    gib_zurück fallback


funktion _ui_layout_neu(parent, richtung, x, y, b, h):
    setze L auf {}
    L["richtung"] = richtung
    L["parent"] = parent
    L["x"] = x
    L["y"] = y
    L["b"] = b
    L["h"] = h
    L["pad_oben"] = 0
    L["pad_rechts"] = 0
    L["pad_unten"] = 0
    L["pad_links"] = 0
    L["gap"] = 0
    L["kinder"] = []
    gib_zurück L


funktion _ui_layout_align_offset(verfuegbar, groesse, modus):
    wenn modus == "mitte":
        setze d auf verfuegbar - groesse
        wenn d < 0:
            gib_zurück 0
        gib_zurück d / 2
    wenn modus == "ende":
        setze d auf verfuegbar - groesse
        wenn d < 0:
            gib_zurück 0
        gib_zurück d
    gib_zurück 0


# ---------- oeffentliche API ----------

funktion ui_spalte(parent, x, y, b, h):
    gib_zurück _ui_layout_neu(parent, "spalte", x, y, b, h)


funktion ui_zeile(parent, x, y, b, h):
    gib_zurück _ui_layout_neu(parent, "zeile", x, y, b, h)


funktion ui_layout_padding(layout, oben, rechts, unten, links):
    layout["pad_oben"] = oben
    layout["pad_rechts"] = rechts
    layout["pad_unten"] = unten
    layout["pad_links"] = links
    gib_zurück layout


funktion ui_layout_abstand(layout, gap):
    layout["gap"] = gap
    gib_zurück layout


funktion ui_layout_hinzufuegen(layout, widget, opts):
    setze kind auf {}
    kind["widget"] = widget
    kind["breite"]     = _ui_layout_opt(opts, "breite", -1)
    kind["hoehe"]      = _ui_layout_opt(opts, "hoehe", -1)
    kind["fill_x"]     = _ui_layout_opt(opts, "fill_x", falsch)
    kind["fill_y"]     = _ui_layout_opt(opts, "fill_y", falsch)
    kind["gewicht"]    = _ui_layout_opt(opts, "gewicht", 0)
    kind["align"]      = _ui_layout_opt(opts, "align", "start")
    kind["min_breite"] = _ui_layout_opt(opts, "min_breite", 0)
    kind["min_hoehe"]  = _ui_layout_opt(opts, "min_hoehe", 0)
    layout["kinder"].hinzufügen(kind)
    gib_zurück kind


funktion _ui_layout_apply_kind(kind, px, py, kb, kh):
    kind["_x"] = px
    kind["_y"] = py
    kind["_b"] = kb
    kind["_h"] = kh
    wenn kind["widget"] != nichts:
        ui_position_setze(kind["widget"], px, py)
        ui_groesse_setze(kind["widget"], kb, kh)


funktion ui_layout_neu_berechnen(layout):
    setze richtung auf layout["richtung"]
    setze x0 auf layout["x"] + layout["pad_links"]
    setze y0 auf layout["y"] + layout["pad_oben"]
    setze inner_b auf layout["b"] - layout["pad_links"] - layout["pad_rechts"]
    setze inner_h auf layout["h"] - layout["pad_oben"] - layout["pad_unten"]
    wenn inner_b < 0:
        setze inner_b auf 0
    wenn inner_h < 0:
        setze inner_h auf 0

    setze kinder auf layout["kinder"]
    setze n auf länge(kinder)
    wenn n == 0:
        gib_zurück wahr

    setze gap auf layout["gap"]
    setze gap_gesamt auf 0
    wenn n > 1:
        setze gap_gesamt auf gap * (n - 1)

    # Pass 1: fixe Groessen + Gewicht-Summe in Hauptrichtung
    setze fix_summe auf 0
    setze gewicht_summe auf 0
    für kind in kinder:
        setze eigen auf kind["hoehe"]
        wenn richtung == "zeile":
            setze eigen auf kind["breite"]
        wenn kind["gewicht"] > 0:
            setze gewicht_summe auf gewicht_summe + kind["gewicht"]
        sonst:
            wenn eigen < 0:
                # fill ohne gewicht → wie gewicht 1
                setze gewicht_summe auf gewicht_summe + 1
            sonst:
                setze fix_summe auf fix_summe + eigen

    setze haupt_verfuegbar auf inner_h - gap_gesamt - fix_summe
    wenn richtung == "zeile":
        setze haupt_verfuegbar auf inner_b - gap_gesamt - fix_summe
    wenn haupt_verfuegbar < 0:
        setze haupt_verfuegbar auf 0

    # Pass 2: Positionieren
    setze cursor_x auf x0
    setze cursor_y auf y0
    setze rest auf haupt_verfuegbar
    setze rest_gewicht auf gewicht_summe
    für kind in kinder:
        setze kb auf kind["breite"]
        setze kh auf kind["hoehe"]
        wenn richtung == "spalte":
            # Hauptrichtung = Y
            setze hat_gewicht auf falsch
            wenn kind["gewicht"] > 0:
                setze hat_gewicht auf wahr
            wenn hat_gewicht == falsch:
                wenn kh < 0:
                    setze hat_gewicht auf wahr
                    kind["gewicht"] = 1
            wenn hat_gewicht:
                setze anteil auf 0
                wenn rest_gewicht > 0:
                    setze anteil auf (rest * kind["gewicht"]) / rest_gewicht
                wenn anteil < 0:
                    setze anteil auf 0
                setze kh auf anteil
                setze rest auf rest - anteil
                setze rest_gewicht auf rest_gewicht - kind["gewicht"]
            wenn kh < kind["min_hoehe"]:
                setze kh auf kind["min_hoehe"]
            # Querrichtung X
            setze off_x auf 0
            wenn kind["fill_x"]:
                setze kb auf inner_b
            sonst:
                wenn kb < 0:
                    setze kb auf inner_b
                sonst:
                    setze off_x auf _ui_layout_align_offset(inner_b, kb, kind["align"])
            wenn kb < kind["min_breite"]:
                setze kb auf kind["min_breite"]
            _ui_layout_apply_kind(kind, x0 + off_x, cursor_y, kb, kh)
            setze cursor_y auf cursor_y + kh + gap
        sonst:
            # Zeile: Hauptrichtung = X
            setze hat_gewicht auf falsch
            wenn kind["gewicht"] > 0:
                setze hat_gewicht auf wahr
            wenn hat_gewicht == falsch:
                wenn kb < 0:
                    setze hat_gewicht auf wahr
                    kind["gewicht"] = 1
            wenn hat_gewicht:
                setze anteil auf 0
                wenn rest_gewicht > 0:
                    setze anteil auf (rest * kind["gewicht"]) / rest_gewicht
                wenn anteil < 0:
                    setze anteil auf 0
                setze kb auf anteil
                setze rest auf rest - anteil
                setze rest_gewicht auf rest_gewicht - kind["gewicht"]
            wenn kb < kind["min_breite"]:
                setze kb auf kind["min_breite"]
            # Querrichtung Y
            setze off_y auf 0
            wenn kind["fill_y"]:
                setze kh auf inner_h
            sonst:
                wenn kh < 0:
                    setze kh auf inner_h
                sonst:
                    setze off_y auf _ui_layout_align_offset(inner_h, kh, kind["align"])
            wenn kh < kind["min_hoehe"]:
                setze kh auf kind["min_hoehe"]
            _ui_layout_apply_kind(kind, cursor_x, y0 + off_y, kb, kh)
            setze cursor_x auf cursor_x + kb + gap

    gib_zurück wahr


# ---------- Komfort-Wrapper ----------
# Erzeugen Widget im Parent des Layouts, fuegen es dem Layout mit opts hinzu
# und rufen KEIN neu_berechnen (Aufrufer macht das am Ende einmalig).

funktion ui_layout_label(layout, text, opts):
    setze w auf ui_label(layout["parent"], text, 0, 0, 100, 24)
    ui_layout_hinzufuegen(layout, w, opts)
    gib_zurück w


funktion ui_layout_knopf(layout, text, cb, opts):
    setze w auf ui_knopf(layout["parent"], text, 0, 0, 100, 28, cb)
    ui_layout_hinzufuegen(layout, w, opts)
    gib_zurück w


funktion ui_layout_eingabe(layout, platzhalter, opts):
    setze w auf ui_eingabe(layout["parent"], 0, 0, 100, 24, platzhalter, falsch)
    ui_layout_hinzufuegen(layout, w, opts)
    gib_zurück w


funktion ui_layout_checkbox(layout, text, initial, cb, opts):
    setze w auf ui_checkbox(layout["parent"], text, 0, 0, 120, 24, initial, cb)
    ui_layout_hinzufuegen(layout, w, opts)
    gib_zurück w


funktion ui_layout_dropdown(layout, optionen, cb, opts):
    setze w auf ui_dropdown(layout["parent"], optionen, 0, 0, 120, 24, cb)
    ui_layout_hinzufuegen(layout, w, opts)
    gib_zurück w

# ============================================================
# stdlib/ui_leinwand.moo — Canvas-Event-Wrapper (P5 Phase 1)
#
# Komfort-Schicht ueber den drei neuen P5-Primitiven (C-Builtins):
#   ui_leinwand_on_maus(leinwand, callback)
#     callback-Signatur: auf_maus(leinwand, x, y, taste)
#     taste: 1 = links, 2 = mitte, 3 = rechts
#     Feuert auf MouseDown (nicht Release). Doppelklick = 2 Events.
#
#   ui_leinwand_on_bewegung(leinwand, callback)
#     callback-Signatur: auf_bewegung(leinwand, x, y)
#     Backends koennen Events koaleszieren. Ausserhalb Bereich: kein Feuer.
#
#   ui_zeichne_text_breite(zeichner, text, groesse) -> Pixel (Integer)
#     NUR im on_zeichne(lw, z)-Callback gueltig. Ausserhalb liefert 0.
#     Gerundet auf ganze Pixel. Backend: cairo_text_extents.x_advance /
#     GetTextExtentPoint32W.cx / [NSString sizeWithAttributes:].width.
#
# Dieses Modul ergaenzt darauf aufbauende Komfort-Helfer:
#
#   ui_leinwand_zentriert_text(z, cx, y, text, groesse)
#     Zeichnet Text horizontal um cx zentriert.
#
#   ui_leinwand_drag_init(ziel)
#     Initialisiert Drag-State-Felder im Dict ziel.
#
#   ui_leinwand_drag_rechteck(ziel) -> [x, y, breite, hoehe]
#     Berechnet normalisiertes Rechteck aus Drag-Zustand.
#
#   ui_leinwand_status_text(x, y, taste) -> string
#     Formatiert Koordinaten-Statustext fuer eine Statusleiste.
#
# Bekannte Grenzen (Phase 1):
#   - on_maus feuert nur auf Press, kein MouseUp-Event.
#     Drag-Pattern daher: 1. Klick = Start, 2. Klick = Commit.
#   - ui_zeichne_text_breite gibt 0 ausserhalb on_zeichne.
#   - Nur ein Callback pro Event pro Leinwand (Re-Bind loest alten).
#
# Nutzung: wird automatisch importiert durch `importiere ui`.
# ============================================================


# ---- ui_leinwand_zentriert_text(z, cx, y, text, groesse) ---
#
# Zeichnet `text` horizontal zentriert um die x-Koordinate `cx`.
# Nutzt ui_zeichne_text_breite fuer korrekte Pixel-Berechnung.
# Muss innerhalb on_zeichne(lw, z) aufgerufen werden — ausserhalb
# gibt text_breite 0 zurueck und Text erscheint am linken Rand.
funktion ui_leinwand_zentriert_text(z, cx, y, text, groesse):
    setze tw auf ui_zeichne_text_breite(z, text, groesse)
    setze x auf cx - tw / 2
    ui_zeichne_text(z, x, y, text, groesse)
    gib_zurück wahr


# ---- ui_leinwand_drag_init(ziel) --------------------------
#
# Initialisiert Drag-Zustandsfelder im Dict `ziel`.
# Muss vor dem ersten on_maus-Callback aufgerufen werden.
#
# Felder die angelegt werden:
#   ziel["drag_aktiv"]  = falsch   (ob Drag gerade laeuft)
#   ziel["drag_x0"]     = 0        (Startpunkt x)
#   ziel["drag_y0"]     = 0        (Startpunkt y)
#   ziel["drag_x1"]     = 0        (aktueller Endpunkt x)
#   ziel["drag_y1"]     = 0        (aktueller Endpunkt y)
funktion ui_leinwand_drag_init(ziel):
    ziel["drag_aktiv"] = falsch
    ziel["drag_x0"] = 0
    ziel["drag_y0"] = 0
    ziel["drag_x1"] = 0
    ziel["drag_y1"] = 0
    gib_zurück wahr


# ---- ui_leinwand_drag_rechteck(ziel) -> [x, y, b, h] ------
#
# Berechnet das normalisierte Rechteck (immer positive Breite/Hoehe)
# aus dem aktuellen Drag-Zustand in `ziel`.
# Geeignet fuer Live-Vorschau im on_zeichne-Callback sowie zum
# Speichern des fertigen Rechtecks nach Commit.
#
# Rueckgabe: Liste [x, y, breite, hoehe]
funktion ui_leinwand_drag_rechteck(ziel):
    setze x0 auf ziel["drag_x0"]
    setze y0 auf ziel["drag_y0"]
    setze x1 auf ziel["drag_x1"]
    setze y1 auf ziel["drag_y1"]
    setze rx auf x0
    setze ry auf y0
    setze rb auf x1 - x0
    setze rh auf y1 - y0
    wenn rb < 0:
        setze rx auf x1
        setze rb auf x0 - x1
    wenn rh < 0:
        setze ry auf y1
        setze rh auf y0 - y1
    gib_zurück [rx, ry, rb, rh]


# ---- ui_leinwand_status_text(x, y, taste) -> string --------
#
# Formatiert einen lesbaren Statustext aus Mauskoordinaten und
# der gedrueckten Taste (1=links, 2=mitte, 3=rechts).
# Geeignet fuer ui_label_setze in einer Statusleiste.
funktion ui_leinwand_status_text(x, y, taste):
    gib_zurück "x=" + x + "  y=" + y + "  taste=" + taste

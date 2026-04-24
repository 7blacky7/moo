# ============================================================
# stdlib/ui_introspect.moo — Introspection-Wrapper (Plan-004 P1)
#
# Komfort-Hilfen rund um die nativen Introspection-Builtins
# aus der Bridge (siehe moo_ui.h):
#     ui_widget_id_setze(widget, id)
#     ui_widget_id(widget)
#     ui_widget_info(widget)        -> Dict
#     ui_widget_baum(fenster)       -> flache MooList pre-order
#     ui_widget_suche(fenster, id)  -> Widget oder nichts
#
# Dieses Modul liefert:
#   - ui_widget_dump(widget)           -> mehrzeiliger Debug-String
#   - ui_widget_baum_json(fenster)     -> JSON-String (Sidecar-tauglich)
#   - ui_widget_anzahl(fenster)        -> int (Laenge baum)
#   - ui_widget_typen_zaehlen(fenster) -> Dict typ → count
#
# Runtime-Limit-Beachtung:
#   - Bracket-Syntax d["k"] statt Dot (pointer-tagged MooValues).
#   - Keine Keyword-Args am Call-Site, keine Multi-Line-Lambdas.
#   - .enthält(key) statt "in".
#   - Reservierte Bezeichner vermeiden: default/standard/neu/st.
#   - JSON-Erzeugung nutzt das Builtin json_string(v), damit
#     der Output maschinenlesbar und valide parsebar ist.
# ============================================================


# ---------- Interne Helfer ----------------------------------

# Leerzeichen-String der Laenge n*2 fuer Einrueckung nach tiefe.
funktion __ui_intro_einrueck(n):
    setze s auf ""
    setze i auf 0
    solange i < n:
        setze s auf s + "  "
        setze i auf i + 1
    gib_zurück s


# Robustes Lookup mit Fallback "?" falls Key fehlt.
funktion __ui_intro_feld(d, key):
    wenn d.enthält(key):
        gib_zurück d[key]
    gib_zurück "?"


# ---------- Oeffentliche API --------------------------------

# Einzel-Widget debuggen. Erwartet ein Widget-Handle (nicht Dict).
# Holt intern ui_widget_info und formatiert mehrzeilig.
funktion ui_widget_dump(widget):
    setze info auf ui_widget_info(widget)
    setze tiefe auf 0
    wenn info.enthält("tiefe"):
        setze tiefe auf info["tiefe"]
    setze vor auf __ui_intro_einrueck(tiefe)
    setze typ auf __ui_intro_feld(info, "typ")
    setze id auf __ui_intro_feld(info, "id")
    setze x auf __ui_intro_feld(info, "x")
    setze y auf __ui_intro_feld(info, "y")
    setze b auf __ui_intro_feld(info, "b")
    setze h auf __ui_intro_feld(info, "h")
    setze sichtbar auf __ui_intro_feld(info, "sichtbar")
    setze aktiv auf __ui_intro_feld(info, "aktiv")
    setze text auf __ui_intro_feld(info, "text")
    setze name auf __ui_intro_feld(info, "name")
    setze zeile auf vor + "[" + typ + "] id=" + id + " name=" + name
    setze zeile auf zeile + " pos=(" + x + "," + y + ") groesse=(" + b + "x" + h + ")"
    setze zeile auf zeile + " sichtbar=" + sichtbar + " aktiv=" + aktiv
    setze zeile auf zeile + " text=" + text
    gib_zurück zeile


# Baum als JSON-String. Nutzt das Runtime-Builtin json_string,
# damit das Ergebnis direkt parsebar ist (Sidecar-Artefakte fuer
# KI-Review, Plan-004 @gpt-Vorgabe).
# Liefert "[]" wenn fenster ungueltig und baum leer.
funktion ui_widget_baum_json(fenster):
    setze baum auf ui_widget_baum(fenster)
    wenn baum == nichts:
        gib_zurück "[]"
    gib_zurück json_string(baum)


# Anzahl aller Widgets im Baum (Fenster inklusive).
funktion ui_widget_anzahl(fenster):
    setze baum auf ui_widget_baum(fenster)
    wenn baum == nichts:
        gib_zurück 0
    gib_zurück länge(baum)


# Zaehlt Widget-Typen. Rueckgabe: Dict typ → count.
funktion ui_widget_typen_zaehlen(fenster):
    setze zaehler auf {}
    setze baum auf ui_widget_baum(fenster)
    wenn baum == nichts:
        gib_zurück zaehler
    für eintrag in baum:
        setze typ auf __ui_intro_feld(eintrag, "typ")
        wenn zaehler.enthält(typ):
            setze zaehler[typ] auf zaehler[typ] + 1
        sonst:
            setze zaehler[typ] auf 1
    gib_zurück zaehler

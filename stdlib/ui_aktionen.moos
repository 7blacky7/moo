# ============================================================
# stdlib/ui_aktionen.moo — Aktionen, Toolbar, Menue-Integration
#
# Eine "Aktion" ist die Single-Source-of-Truth fuer
# Menueeintrag, Toolbar-Knopf und Tastenkuerzel.
#
# Pattern (nach Qt QAction / WPF RoutedCommand):
#   aktion = {
#       "name":     "speichern",       # interner Schluessel
#       "titel":    "Speichern",        # angezeigter Text
#       "shortcut": "Ctrl+S",           # Tasten-Sequenz (nichts = kein Shortcut)
#       "callback": funk,               # parameterlose Funktion
#       "enabled":  wahr,               # noch nicht verdrahtet — Hook fuer spaeter
#   }
#
# API:
#   ui_aktion(g, name, titel, shortcut, callback)
#       → legt eine Aktion im g["aktionen"]-Dict ab (name als Key).
#
#   ui_aktionen_liste(g)
#       → gibt eine Liste aller im Container registrierten Aktionen zurueck
#         (in Insertion-Reihenfolge, falls die Laufzeit das unterstuetzt).
#
#   ui_toolbar(parent, x, y, b, h, aktionsliste)
#       → baut eine horizontale Toolbar. Jede Aktion wird ein Knopf,
#         dessen Callback die Aktion feuert. Eine Aktion mit name
#         "_separator" wird als visueller Trenner (schmaler, leerer
#         Bereich) eingefuegt.
#       → liefert ein Dict mit "knoepfe" (Liste der Knopf-Handles)
#
#   ui_menue_aktion(menue_handle, aktion)
#       → fuegt einen Menueeintrag mit aktion["titel"] + aktion["callback"]
#         ein. Bindet automatisch aktion["shortcut"], wenn gesetzt
#         UND wenn g["hFenster"] bekannt ist (dafuer muss die Aktion
#         ueber ui_aktion(...) registriert worden sein — wir binden
#         im ui_aktion-Call schon, damit der Shortcut unabhaengig vom
#         Menue arbeitet. ui_menue_aktion fuegt nur das Menue-Label +
#         Klick-Handler hinzu).
#
# Hinweise zu Sprach-Limits (Stand 2026-04-21):
#   - Dict-Handles werden immer mit g["key"] (Bracket) abgelegt.
#   - Indirekte Aufrufe sind auf max. 8 Args begrenzt (hier kein Problem).
#   - Keine Keyword-Args am Call-Site.
#
# Backend-Stand (Shortcuts):
#   GTK (Linux)   — implementiert (commit c22e85a)
#   Win32         — Header vorhanden, Impl fehlt (Fallback: silent noop)
#   Cocoa (macOS) — Header vorhanden, Impl fehlt (Fallback: silent noop)
#   Bridge (runtime_bindings/codegen) — implementiert (commit b178b67).
# ============================================================


# Hilfs-Dummy fuer Aktionen ohne Callback.
funktion ui_aktion_noop():
    gib_zurück nichts


# Registriert eine Aktion. Bindet den Shortcut sofort an das Fenster.
# g muss ein bereits gebautes Container-Dict mit g["hFenster"] sein.
# shortcut darf "" oder nichts sein, dann wird kein Shortcut gebunden.
funktion ui_aktion(g, name, titel, shortcut, callback):
    setze a auf {}
    a["name"]     = name
    a["titel"]    = titel
    a["shortcut"] = shortcut
    a["callback"] = callback
    a["enabled"]  = wahr

    # Aktionen-Register im Container anlegen, falls noch nicht da.
    wenn nicht g.enthält("aktionen"):
        g["aktionen"] = {}
    setze reg auf g["aktionen"]
    reg[name] = a

    # Reihenfolge separat pflegen (Dict-Iterationsreihenfolge ist
    # Implementation-abhaengig).
    wenn nicht g.enthält("aktionen_order"):
        g["aktionen_order"] = []
    g["aktionen_order"].hinzufügen(name)

    # Shortcut an das Fenster binden, falls vorhanden.
    wenn g.enthält("hFenster"):
        wenn shortcut != nichts:
            wenn shortcut != "":
                ui_shortcut_bind(g["hFenster"], shortcut, callback)

    gib_zurück a


# Gibt Aktionen in Registrierungs-Reihenfolge zurueck.
funktion ui_aktionen_liste(g):
    setze result auf []
    wenn nicht g.enthält("aktionen"):
        gib_zurück result
    wenn nicht g.enthält("aktionen_order"):
        gib_zurück result
    setze reg auf g["aktionen"]
    für name in g["aktionen_order"]:
        wenn reg.enthält(name):
            result.hinzufügen(reg[name])
    gib_zurück result


# Hilfe: erkennt Separator-Eintraege.
funktion ui_aktion_ist_separator(a):
    wenn typ_von(a) == "Dictionary":
        wenn a.enthält("name"):
            wenn a["name"] == "_separator":
                gib_zurück wahr
    gib_zurück falsch


# Baut eine horizontale Toolbar aus einer Aktionsliste.
# aktionsliste: Liste von Aktion-Dicts (ggf. {"name": "_separator"}).
# Liefert {"knoepfe": [...]} — ein Dict, damit wir spaeter erweitern koennen.
funktion ui_toolbar(parent, x, y, b, h, aktionsliste):
    setze tb auf {}
    setze knoepfe auf []

    setze cx auf x
    setze btn_breite auf 100
    setze sep_breite auf 10
    setze gap auf 4

    für a in aktionsliste:
        wenn ui_aktion_ist_separator(a):
            # visueller Trenner — wir nutzen ui_trenner wenn vorhanden,
            # sonst einfach Platz lassen.
            setze cx auf cx + sep_breite
        sonst:
            setze titel auf a["titel"]
            setze cb auf a["callback"]
            setze btn auf ui_knopf(parent, titel, cx, y, btn_breite, h, cb)
            knoepfe.hinzufügen(btn)
            setze cx auf cx + btn_breite + gap

    tb["knoepfe"] = knoepfe
    tb["breite_genutzt"] = cx - x
    gib_zurück tb


# Fuegt einen Menueeintrag aus einer Aktion in ein bestehendes Menue ein.
# Der Shortcut ist bereits ueber ui_aktion(g, ...) ans Fenster gebunden.
# Diese Funktion haengt nur den Menueeintrag selbst an — mit dem Label
# das den Shortcut-Hinweis als Text enthaelt, so dass User ihn sehen.
funktion ui_menue_aktion(menue_handle, aktion):
    setze label auf aktion["titel"]
    wenn aktion.enthält("shortcut"):
        wenn aktion["shortcut"] != nichts:
            wenn aktion["shortcut"] != "":
                setze label auf label + "    " + aktion["shortcut"]
    gib_zurück ui_menue_eintrag(menue_handle, label, aktion["callback"])


# Komfort: baut Toolbar direkt aus dem Container g. Erwartet, dass
# die Aktionen vorher mit ui_aktion(g, ...) registriert wurden.
funktion ui_toolbar_aus_g(g, x, y, b, h):
    gib_zurück ui_toolbar(g["hFenster"], x, y, b, h, ui_aktionen_liste(g))

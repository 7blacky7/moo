# ============================================================
# stdlib/ui_liste.moo — Convenience-Wrapper fuer ListView-Erweiterungen
#
# Die 6 neuen Bridge-Funktionen sind bereits als C-Builtins
# im Runtime verfuegbar (commit d71ef86). Dieses Modul bietet
# hoeher-schichtige Komfort-Wrapper, die typische Muster
# kapseln (Batch-Konfiguration, Bulk-Fill, Config-Dict).
#
# Primitive (direkt nutzbar ohne Wrapper):
#   ui_liste_spalte_breite(liste, spalten_idx, breite)
#       Setzt die Pixelbreite einer Spalte.
#   ui_liste_sortierbar(liste, spalten_idx, sortierbar)
#       Aktiviert/deaktiviert Klick-Sortierung fuer eine Spalte.
#   ui_liste_sortiere(liste, spalten_idx, aufsteigend)
#       Sortiert die Liste programmatisch nach einer Spalte.
#   ui_liste_zeile_setze(liste, zeilen_idx, werte_liste)
#       Ueberschreibt alle Zellen einer Zeile.
#   ui_liste_zelle_setze(liste, zeilen_idx, spalten_idx, wert)
#       Setzt den Wert einer einzelnen Zelle.
#   ui_liste_entferne(liste, zeilen_idx)
#       Entfernt eine Zeile (alle folgenden ruecken auf).
#
# Komfort-API (dieser Wrapper):
#   ui_liste_spalten_breiten(liste, breiten_liste)
#   ui_liste_alle_sortierbar(liste, anzahl)
#   ui_liste_fuellen(liste, zeilen_liste)
#   ui_liste_konfiguriere(liste, config)
#
# Hinweise zu Sprach-Limits (Stand 2026-04-21):
#   - Immer Bracket-Syntax g["key"] fuer Dict-Zugriff (Dot segfaultet).
#   - Keine Keyword-Args am Call-Site.
#   - Keine Multi-Line-Lambdas — top-level Funktionen verwenden.
#   - Dict-Membership via .enthält(key).
# ============================================================


# Setzt die Breite mehrerer Spalten auf einmal.
# breiten_liste: Liste von Pixelwerten, Position = Spalten-Index.
# Beispiel: ui_liste_spalten_breiten(lst, [200, 100, 150])
funktion ui_liste_spalten_breiten(liste, breiten_liste):
    setze idx auf 0
    für breite in breiten_liste:
        ui_liste_spalte_breite(liste, idx, breite)
        setze idx auf idx + 1
    gib_zurück wahr


# Macht die ersten `anzahl` Spalten klick-sortierbar.
# Entspricht anzahl Aufrufen von ui_liste_sortierbar(liste, i, wahr).
# Beispiel: ui_liste_alle_sortierbar(lst, 3)
funktion ui_liste_alle_sortierbar(liste, anzahl):
    setze idx auf 0
    solange idx < anzahl:
        ui_liste_sortierbar(liste, idx, wahr)
        setze idx auf idx + 1
    gib_zurück wahr


# Füllt eine Liste mit mehreren Zeilen auf einmal.
# zeilen: Liste von Werte-Listen, z.B. [["Anna", "30"], ["Bert", "25"]]
# Entspricht mehreren Aufrufen von ui_liste_zeile_hinzu.
funktion ui_liste_fuellen(liste, zeilen):
    für zeile in zeilen:
        ui_liste_zeile_hinzu(liste, zeile)
    gib_zurück wahr


# Konfiguriert Spaltenbreiten und Sortierbarkeit aus einem Config-Dict.
# Erlaubte Schluessel im config-Dict:
#   config["breiten"]    -> Liste von Pixelwerten (optional)
#   config["sortierbar"] -> Liste von Wahrheitswerten pro Spalte (optional)
# Beispiel:
#   setze cfg auf {}
#   cfg["breiten"]    = [280, 150, 130]
#   cfg["sortierbar"] = [wahr, wahr, falsch]
#   ui_liste_konfiguriere(g["liste"], cfg)
funktion ui_liste_konfiguriere(liste, config):
    wenn config.enthält("breiten"):
        ui_liste_spalten_breiten(liste, config["breiten"])
    wenn config.enthält("sortierbar"):
        setze flags auf config["sortierbar"]
        setze idx auf 0
        für flag in flags:
            ui_liste_sortierbar(liste, idx, flag)
            setze idx auf idx + 1
    gib_zurück wahr

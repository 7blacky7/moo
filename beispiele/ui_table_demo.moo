# ============================================================
# beispiele/ui_table_demo.moo — ListView-Demo (P2 Polish)
#
# File-Browser-aehnliche Liste mit 3 Spalten:
#   - Name, Groesse, Typ
#   - Einstellbare Spaltenbreiten (ui_liste_konfiguriere)
#   - Alle Spalten per Klick sortierbar (ui_liste_konfiguriere)
#   - Hinweis: Sortierung ist string-lexikografisch, nicht numerisch
#   - Programmatische Sortierung per Button
#   - Zeile aktualisieren per Button (ui_liste_zeile_setze)
#   - Zelle aktualisieren per Button (ui_liste_zelle_setze)
#   - Zeile entfernen per Button (ui_liste_entferne)
#
# Laeuft ALIVE auf GTK/Linux (und Win32/Cocoa sobald verfuegbar).
# Alle 6 neuen Bridge-Funktionen werden mindestens einmal aufgerufen.
#
# Sprach-Limits beachtet (Stand 2026-04-21):
#   - Bracket-Syntax g["key"] statt Dot (pointer-tagged MooValues)
#   - Keine Keyword-Args am Call-Site
#   - Keine Multi-Line-Lambdas (top-level Funktionen fuer Callbacks)
#   - Dict-Membership via .enthält(key)
# ============================================================

importiere ui

setze g auf {}

# --- Beispieldaten (File-Browser-Stil) ---
setze datei_daten auf [
    ["dokument.pdf",  "1.2 MB",  "PDF"],
    ["bild.png",      "500 KB",  "Bild"],
    ["video.mp4",     "50 MB",   "Video"],
    ["musik.mp3",     "4.5 MB",  "Audio"],
    ["archiv.zip",    "10 MB",   "Archiv"],
    ["skript.moo",    "8 KB",    "Quellcode"],
    ["tabelle.csv",   "32 KB",   "CSV"],
]


# --- Callbacks (top-level, 0-arg — Runtime-Limit) ---

funktion sortiere_name():
    ui_liste_sortiere(g["liste"], 0, wahr)
    ui_label_setze(g["lbl_status"], "Sortiert nach: Name (aufsteigend)")

funktion sortiere_groesse():
    ui_liste_sortiere(g["liste"], 1, wahr)
    ui_label_setze(g["lbl_status"], "Sortiert nach: Groesse (aufsteigend)")

funktion sortiere_typ():
    ui_liste_sortiere(g["liste"], 2, wahr)
    ui_label_setze(g["lbl_status"], "Sortiert nach: Typ (aufsteigend)")

funktion zeile_aktualisieren():
    ui_liste_zeile_setze(g["liste"], 0, ["README.md", "2 KB", "Text"])
    ui_label_setze(g["lbl_status"], "Zeile 0 -> [README.md, 2 KB, Text]")

funktion zelle_setzen():
    ui_liste_zelle_setze(g["liste"], 1, 1, "999 MB")
    ui_label_setze(g["lbl_status"], "Zelle [1][1] -> 999 MB")

funktion zeile_entfernen():
    ui_liste_entferne(g["liste"], 0)
    ui_label_setze(g["lbl_status"], "Erste Zeile entfernt")

funktion app_beenden():
    ui_beenden()


# --- Fenster ---
g["hFenster"] = ui_fenster("ListView-Demo — File Browser", 620, 470, 0, nichts)


# --- Liste mit 3 Spalten ---
setze spalten auf ["Name", "Groesse", "Typ"]
g["liste"] = ui_liste(g["hFenster"], spalten, 10, 10, 598, 260)

# Spaltenbreiten + Sortierbarkeit via Config-Wrapper.
# Damit ist ui_liste_konfiguriere nicht nur dokumentiert, sondern in der Demo sichtbar.
setze cfg auf {}
cfg["breiten"] = [290, 155, 133]
cfg["sortierbar"] = [wahr, wahr, wahr]
ui_liste_konfiguriere(g["liste"], cfg)

# Daten einfuellen via stdlib-Wrapper (ruft ui_liste_zeile_hinzu 7x)
ui_liste_fuellen(g["liste"], datei_daten)


# --- Programmatische Sort-Buttons ---
ui_label(g["hFenster"], "Sortierung:", 10, 278, 90, 20)
ui_knopf(g["hFenster"], "Name",    105, 275, 90, 28, sortiere_name)
ui_knopf(g["hFenster"], "Groesse", 200, 275, 90, 28, sortiere_groesse)
ui_knopf(g["hFenster"], "Typ",     295, 275, 90, 28, sortiere_typ)


# --- Zeile/Zelle/Entfernen-Buttons ---
ui_label(g["hFenster"], "Bearbeiten:", 10, 313, 90, 20)
ui_knopf(g["hFenster"], "Zeile update", 105, 310, 120, 28, zeile_aktualisieren)
ui_knopf(g["hFenster"], "Zelle setzen", 230, 310, 120, 28, zelle_setzen)
ui_knopf(g["hFenster"], "Zeile entf.",  355, 310, 110, 28, zeile_entfernen)


# --- Status-Label ---
g["lbl_status"] = ui_label(g["hFenster"], "Bereit — Spalten-Klick sortiert ebenfalls", 10, 348, 598, 22)


# --- Trennlinie + Beenden ---
ui_trenner(g["hFenster"], 10, 378, 598, 4)
ui_knopf(g["hFenster"], "Beenden", 498, 390, 110, 30, app_beenden)

ui_label(g["hFenster"], "Tipp: Spalten-Header anklicken um zu sortieren.", 10, 392, 480, 20)


ui_zeige(g["hFenster"])
ui_laufen()

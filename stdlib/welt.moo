# ============================================================
# stdlib/welt.moo — Welten-Modul (Deutsch)
#
# Erstellt prozedurale 3D-Welten mit einem Aufruf.
# Nutzung: importiere welt
#
# Beispiel:
#   setze w auf welt_erstelle("Meine Welt", 800, 600)
#   welt_seed(w, 42)
#   solange welt_offen(w):
#       welt_aktualisieren(w)
#   welt_beenden(w)
# ============================================================

# === Kern-Funktionen ===

funktion welt_erstelle(titel, breite, hoehe):
    gib_zurück __welt_erstelle(titel, breite, hoehe)

funktion welt_offen(w):
    gib_zurück __welt_offen(w)

funktion welt_aktualisieren(w):
    __welt_aktualisieren(w)

funktion welt_beenden(w):
    __welt_beenden(w)

# === Konfiguration ===

funktion welt_seed(w, seed):
    __welt_seed(w, seed)

funktion welt_biom(w, name, h_min, h_max, farbe, baeume):
    __welt_biom(w, name, h_min, h_max, farbe, baeume)

funktion welt_hoehe_bei(w, x, z):
    gib_zurück __welt_hoehe_bei(w, x, z)

funktion welt_sonne(w, x, y, z):
    __welt_sonne(w, x, y, z)

funktion welt_nebel(w, distanz):
    __welt_nebel(w, distanz)

funktion welt_meeresspiegel(w, hoehe):
    __welt_meeresspiegel(w, hoehe)

funktion welt_render_distanz(w, distanz):
    __welt_render_distanz(w, distanz)

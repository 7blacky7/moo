# ============================================================
# buildings.moo — Gebaeude-Definitionen (Modul-Skizze)
#
# Saubere Referenz fuer alle Gebaeudetypen mit Farbe, Hoehe, Kosten,
# Effekten. Im Haupt-Spiel city_builder.moo sind diese Funktionen
# inline, damit das Spiel ohne lokale Imports kompiliert.
# ============================================================

konstante B_LEER auf 0
konstante B_HAUS auf 1
konstante B_FABRIK auf 2
konstante B_PARK auf 3
konstante B_STRASSE auf 4
konstante B_WASSER auf 5

# Hex-Farben fuer 3D-Wuerfel.
funktion gebaeude_hex(typ):
    wenn typ == B_HAUS:
        gib_zurück "#d6a86b"
    wenn typ == B_FABRIK:
        gib_zurück "#7a7d83"
    wenn typ == B_PARK:
        gib_zurück "#4caf50"
    wenn typ == B_STRASSE:
        gib_zurück "#2c2c2c"
    wenn typ == B_WASSER:
        gib_zurück "#2196f3"
    gib_zurück "#3b6e3a"

# Welt-Hoehe in Einheiten (1.0 = 1 Tile).
funktion gebaeude_baseheight(typ):
    wenn typ == B_HAUS:
        gib_zurück 0.7
    wenn typ == B_FABRIK:
        gib_zurück 1.0
    wenn typ == B_PARK:
        gib_zurück 0.15
    wenn typ == B_STRASSE:
        gib_zurück 0.06
    wenn typ == B_WASSER:
        gib_zurück 0.04
    gib_zurück 0.0

# [holz, stein, gold]
funktion gebaeude_costs(typ):
    wenn typ == B_HAUS:
        gib_zurück [3, 1, 2]
    wenn typ == B_FABRIK:
        gib_zurück [2, 5, 5]
    wenn typ == B_PARK:
        gib_zurück [2, 0, 1]
    wenn typ == B_STRASSE:
        gib_zurück [0, 2, 1]
    gib_zurück [0, 0, 0]

# Kurzbeschreibung fuer HUD/Tooltip.
funktion gebaeude_label(typ):
    wenn typ == B_HAUS:
        gib_zurück "Haus  (+4 Buerger, +1 Gold/Tick)"
    wenn typ == B_FABRIK:
        gib_zurück "Fabrik (+2 Gold/Tick, +1 Stein/Tick)"
    wenn typ == B_PARK:
        gib_zurück "Park  (+1 Holz/Tick, +1 Buerger)"
    wenn typ == B_STRASSE:
        gib_zurück "Strasse (verbindet)"
    wenn typ == B_WASSER:
        gib_zurück "Wasser (Dekoration)"
    gib_zurück "Leer"

# Kategorie: 0=Wohnen, 1=Industrie, 2=Gruen, 3=Infrastruktur, 4=Natur
funktion gebaeude_kategorie(typ):
    wenn typ == B_HAUS:
        gib_zurück 0
    wenn typ == B_FABRIK:
        gib_zurück 1
    wenn typ == B_PARK:
        gib_zurück 2
    wenn typ == B_STRASSE:
        gib_zurück 3
    wenn typ == B_WASSER:
        gib_zurück 4
    gib_zurück -1

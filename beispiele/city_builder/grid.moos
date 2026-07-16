# ============================================================
# grid.moo — Grid-System fuer den City Builder (Modul-Skizze)
#
# Diese Datei ist das logische "Modul" fuer alle Grid-Operationen.
# Im Hauptspiel city_builder.moo sind die Funktionen aus
# Kompatibilitaetsgruenden bereits inline enthalten — diese Datei
# dient als saubere, isoliert testbare Referenz-Implementierung
# und kann mit `importiere grid` eingebunden werden, sobald das
# moo-Modulsystem lokale Imports unterstuetzt.
#
# Datenmodell:
#   Ein Grid ist eine flache Liste der Laenge N*N.
#   grid[y * N + x] = gebaeude_typ (Integer-Konstante 0..5)
# ============================================================

konstante GRID_LEER auf 0

# Erzeugt ein neues NxN-Grid, gefuellt mit GRID_LEER.
funktion grid_create(groesse):
    setze g auf []
    setze i auf 0
    solange i < groesse * groesse:
        g.hinzufügen(GRID_LEER)
        setze i auf i + 1
    gib_zurück g

# Liest einen Wert aus dem Grid; out-of-bounds liefert GRID_LEER.
funktion grid_get_v(g, groesse, x, y):
    wenn x < 0 oder x >= groesse oder y < 0 oder y >= groesse:
        gib_zurück GRID_LEER
    gib_zurück g[y * groesse + x]

# Setzt einen Wert; out-of-bounds wird ignoriert.
funktion grid_set_v(g, groesse, x, y, typ):
    wenn x < 0 oder x >= groesse oder y < 0 oder y >= groesse:
        gib_zurück falsch
    g[y * groesse + x] = typ
    gib_zurück wahr

# Zaehlt Vorkommen eines Typs (z.B. fuer Bevoelkerungs-Berechnung).
funktion grid_count_v(g, groesse, typ):
    setze n auf 0
    setze i auf 0
    solange i < groesse * groesse:
        wenn g[i] == typ:
            setze n auf n + 1
        setze i auf i + 1
    gib_zurück n

# Liefert die Anzahl angrenzender Felder (4er-Nachbarschaft) eines Typs.
# Nuetzlich fuer Adjazenz-Boni (z.B. Park neben Haus).
funktion grid_nachbarn_typ(g, groesse, x, y, typ):
    setze n auf 0
    wenn grid_get_v(g, groesse, x - 1, y) == typ:
        setze n auf n + 1
    wenn grid_get_v(g, groesse, x + 1, y) == typ:
        setze n auf n + 1
    wenn grid_get_v(g, groesse, x, y - 1) == typ:
        setze n auf n + 1
    wenn grid_get_v(g, groesse, x, y + 1) == typ:
        setze n auf n + 1
    gib_zurück n

# Berechnet eine Ressourcen-Bilanz aus Zaehlungen.
# Rueckgabe: [holz_pro_tick, stein_pro_tick, gold_pro_tick, bevoelkerung]
funktion grid_bilanz(g, groesse):
    setze haeuser auf grid_count_v(g, groesse, 1)   # HAUS
    setze fabriken auf grid_count_v(g, groesse, 2)  # FABRIK
    setze parks auf grid_count_v(g, groesse, 3)     # PARK

    setze holz_dt auf parks
    setze stein_dt auf fabriken
    setze gold_dt auf fabriken * 2 + haeuser * 1
    setze leute auf haeuser * 4 + parks * 1
    gib_zurück [holz_dt, stein_dt, gold_dt, leute]

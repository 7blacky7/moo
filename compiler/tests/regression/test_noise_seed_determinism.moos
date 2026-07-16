# Regression P005-1a: Seed-Determinismus auf moo-Ebene.
#
# Contract (Plan-005 P0.3/P0.5): Noise ist seed-parametrisiert OHNE globalen
# Seed-Leak. Zwei Welten mit GLEICHEM Seed muessen identische Worldgen-Ergebnisse
# liefern; benachbarte Seeds muessen sich unterscheiden. Beobachtbar auf
# moo-Ebene ueber voxel_welt_neu(seed) + voxel_holen entlang einer Saeule.
#
# HINWEIS (Stand RT5): Worldgen ist da. voxel_holen bleibt ein REINER
# Lesezugriff (P0.5-Contract: nie beschriebener Chunk = Luft, kein Lazy-Gen —
# das schuetzen test_voxel_empty_chunk_air / _refcount_release). Terrain wird
# deshalb EXPLIZIT erzeugt: welt_signatur ruft voxel_generieren(w, cx, cz) fuer
# die horizontalen Chunk-Spalten der Probepunkte, BEVOR die Saeulen gelesen
# werden. Generieren ist seed-deterministisch (moo_noise_fbm) -> gleicher Seed
# liefert identische Saeulen, benachbarter Seed verschiedene.

# Fuellstand einer Saeule bei (x,z): Anzahl nicht-Luft-Bloecke von y=0..63.
# Determinismus heisst: gleiche Eingaben -> gleicher Fuellstand.
funktion saeule_summe(w, x, z):
    setze summe auf 0
    setze y auf 0
    solange y < 64:
        wenn voxel_holen(w, x, z, y) != 0:
            setze summe auf summe + 1
        setze y auf y + 1
    gib_zurück summe

# Signatur einer Welt: Fuellstaende an mehreren Probepunkten kombiniert.
funktion welt_signatur(seed):
    setze w auf voxel_welt_neu(seed)
    # Terrain explizit erzeugen: horizontale Chunk-Spalten der Probepunkte
    # (Chunk-Dim 32). Probepunkte (0,0)/(5,7) -> Chunk (0,0);
    # (-3,12) -> Chunk (-1,0); (20,-8) -> Chunk (0,-1).
    voxel_generieren(w, 0, 0)
    voxel_generieren(w, -1, 0)
    voxel_generieren(w, 0, -1)
    setze sig auf 0
    setze sig auf sig + saeule_summe(w, 0, 0)
    setze sig auf sig + saeule_summe(w, 5, 7) * 31
    setze sig auf sig + saeule_summe(w, -3, 12) * 131
    setze sig auf sig + saeule_summe(w, 20, -8) * 911
    gib_zurück sig

# Gleicher Seed -> identische Signatur (Determinismus, kein Seed-Leak von
# vorherigen Welt-Allokationen). Muss IMMER gelten.
setze a auf welt_signatur(42)
setze b auf welt_signatur(42)
wenn a == b:
    zeige "gleich"
sonst:
    zeige "FEHLER-NICHT-DETERMINISTISCH"

# Reihenfolge-Unabhaengigkeit: Eine andere Welt dazwischen darf die Wiederholung
# nicht beeinflussen (Seed ist instanzgebunden, nicht global).
setze zwischen auf welt_signatur(777)
setze c auf welt_signatur(42)
wenn a == c:
    zeige "kein-leak"
sonst:
    zeige "FEHLER-SEED-LEAK"

# Benachbarte Seeds muessen sich unterscheiden (Worldgen reagiert auf Seed).
# Bleibt rot bis Worldgen existiert (dann liefern beide != 0 und verschieden).
setze s42 auf welt_signatur(42)
setze s43 auf welt_signatur(43)
wenn s42 != s43:
    zeige "verschieden"
sonst:
    zeige "gleich-seeds"

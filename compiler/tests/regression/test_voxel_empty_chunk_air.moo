# Regression P005-1a: Nie allokierter / leerer Chunk liefert Luft (0).
# P0.5-Contract: voxel_holen auf einem nie beschriebenen Chunk = 0 (luft),
# KEIN Crash, keine Allokation noetig. Auch weit entfernte Koordinaten
# (mehrere Chunks weg, positiv UND negativ) muessen 0 liefern.

setze w auf voxel_welt_neu(7)

# Frische Welt: alles ist Luft
zeige voxel_holen(w, 0, 0, 0)
zeige voxel_holen(w, 100, 64, 100)
zeige voxel_holen(w, -500, -20, -777)

# Einen Block setzen, dann einen NACHBARN im selben (jetzt allokierten)
# Chunk lesen -> der Rest des Chunks bleibt Luft.
voxel_setzen(w, 5, 5, 5, 3)
zeige voxel_holen(w, 5, 5, 5)
zeige voxel_holen(w, 6, 5, 5)
zeige voxel_holen(w, 5, 6, 5)

# Block wieder auf Luft setzen -> Chunk darf wieder Luft liefern.
voxel_setzen(w, 5, 5, 5, 0)
zeige voxel_holen(w, 5, 5, 5)

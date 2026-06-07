# Regression P005-1a: Negative Koordinaten ueber Chunk-Grenzen.
# CHUNK_DIM=32. Chunk-Lookup MUSS floor-div/floor-mod nutzen, NICHT C-/%
# (die runden bei negativen Zahlen Richtung 0 -> falscher Chunk + falscher
# lokaler Index). Wir setzen je einen eindeutigen Block an den kritischen
# Koordinaten -33,-32,-1,0,31,32 (Chunk-Grenzen und Vorzeichen-Wechsel)
# und lesen sie zurueck. Jeder Wert muss exakt erhalten bleiben.

setze w auf voxel_welt_neu(1)

# Eindeutige Block-IDs pro Testkoordinate (1..5 sind gueltige IDs, 0=luft)
voxel_setzen(w, -33, 0, 0, 3)
voxel_setzen(w, -32, 0, 0, 1)
voxel_setzen(w, -1, 0, 0, 2)
voxel_setzen(w, 0, 0, 0, 4)
voxel_setzen(w, 31, 0, 0, 5)
voxel_setzen(w, 32, 0, 0, 3)

# Auch auf der y- und z-Achse negative Werte pruefen
voxel_setzen(w, 0, -1, 0, 1)
voxel_setzen(w, 0, 0, -33, 2)

zeige voxel_holen(w, -33, 0, 0)
zeige voxel_holen(w, -32, 0, 0)
zeige voxel_holen(w, -1, 0, 0)
zeige voxel_holen(w, 0, 0, 0)
zeige voxel_holen(w, 31, 0, 0)
zeige voxel_holen(w, 32, 0, 0)
zeige voxel_holen(w, 0, -1, 0)
zeige voxel_holen(w, 0, 0, -33)

# Nachbar-Koordinaten muessen unberuehrt (luft=0) bleiben:
# Kein Overspill durch falsche Index-Berechnung.
zeige voxel_holen(w, -34, 0, 0)
zeige voxel_holen(w, 1, 0, 0)

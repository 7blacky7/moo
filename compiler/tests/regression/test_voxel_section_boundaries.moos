# Regression P006-R1T: 8^3-Section-GRENZEN innerhalb des 32^3-Chunks.
# Mit 4x4x4 Sections liegen Section-Grenzen bei lokalen Koordinaten
# 8/16/24. Wir setzen distinkte IDs genau an/um diese Grenzen (7/8, 15/16,
# 23/24, 31) damit ein Off-by-One in der Section-Indexrechnung (welche
# Section + lokaler Offset in der Section) sofort auffaellt.
# Zusaetzlich: negative Welt-Koordinaten, wo Chunk-Lookup (floor-div) UND
# Section-Index kombiniert werden muessen.

setze w auf voxel_welt_neu(3)

# --- Positive Section-Grenzen entlang x (y=z=0), 6 distinkte Stellen ---
voxel_setzen(w, 7, 0, 0, 1)
voxel_setzen(w, 8, 0, 0, 2)
voxel_setzen(w, 15, 0, 0, 3)
voxel_setzen(w, 16, 0, 0, 4)
voxel_setzen(w, 23, 0, 0, 5)
voxel_setzen(w, 24, 0, 0, 1)
voxel_setzen(w, 31, 0, 0, 2)

zeige voxel_holen(w, 7, 0, 0)
zeige voxel_holen(w, 8, 0, 0)
zeige voxel_holen(w, 15, 0, 0)
zeige voxel_holen(w, 16, 0, 0)
zeige voxel_holen(w, 23, 0, 0)
zeige voxel_holen(w, 24, 0, 0)
zeige voxel_holen(w, 31, 0, 0)

# Jeweils unberuehrter Nachbar zwischen den gesetzten Grenzwerten = Luft
zeige voxel_holen(w, 9, 0, 0)
zeige voxel_holen(w, 17, 0, 0)

# --- Section-Grenzen entlang y und z, gemischte Section-Indizes ---
voxel_setzen(w, 0, 8, 16, 3)
voxel_setzen(w, 0, 24, 7, 4)
zeige voxel_holen(w, 0, 8, 16)
zeige voxel_holen(w, 0, 24, 7)
zeige voxel_holen(w, 0, 8, 15)

# --- Negative Welt-Koordinaten: floor-div Chunk + Section-Index ---
# -1 -> Chunk -1, lokal 31 (oberste Section). -9 -> lokal 23. -8 -> lokal 24.
voxel_setzen(w, -1, -1, -1, 5)
voxel_setzen(w, -8, -8, -8, 1)
voxel_setzen(w, -9, -9, -9, 2)
voxel_setzen(w, -16, 0, 0, 3)
zeige voxel_holen(w, -1, -1, -1)
zeige voxel_holen(w, -8, -8, -8)
zeige voxel_holen(w, -9, -9, -9)
zeige voxel_holen(w, -16, 0, 0)
# Nachbarn der negativen Stellen unberuehrt (kein Section-Overspill)
zeige voxel_holen(w, -2, -1, -1)
zeige voxel_holen(w, -7, -8, -8)

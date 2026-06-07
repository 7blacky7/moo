# Regression P005-1a: Refcount + vollstaendige Freigabe (ASan-Gate).
# VoxelWorld ist ein Heap-Typ (refcount als erstes Struct-Feld).
# Dieser Test erzeugt viele Welten, beschreibt sie (allokiert Chunks)
# und laesst sie aus dem Scope laufen bzw. ueberschreibt die Variable.
# moo_release MUSS moo_voxel_free aufrufen und ALLE CPU-Allokationen
# (Chunks) freigeben. Korrektheit des Outputs hier + ASan-clean beim
# Lauf (run_all.sh unter ASan) = Akzeptanz.

funktion baue_und_verwerfe(seed):
    setze w auf voxel_welt_neu(seed)
    # mehrere Chunks anlegen (ueber Chunk-Grenzen verteilt)
    voxel_setzen(w, 0, 0, 0, 1)
    voxel_setzen(w, 40, 0, 0, 2)
    voxel_setzen(w, -40, 0, 0, 3)
    voxel_setzen(w, 0, 40, 0, 4)
    gib_zurück voxel_holen(w, 40, 0, 0)
    # w laeuft hier aus dem Scope -> release -> free

setze i auf 0
solange i < 20:
    zeige baue_und_verwerfe(i)
    setze i auf i + 1

# Ueberschreiben einer Welt-Variable muss die alte freigeben (kein Leak)
setze a auf voxel_welt_neu(100)
voxel_setzen(a, 1, 2, 3, 5)
setze a auf voxel_welt_neu(200)
zeige voxel_holen(a, 1, 2, 3)

# Regression P006-R1T: 8^3-Section-Layout, EMPTY-Section Einzel-Write.
# RISIKO 3 aus Plan-006: Ein einzelnes voxel_setzen(id!=0) in eine vorher
# leere (EMPTY) Section darf NICHT die ganze Section faelschlich auf SOLID(id)
# setzen. Nur das eine Voxel traegt die ID, der Rest der Section bleibt Luft.
# Getestet rein ueber die public API (Section-Interna sind opak).

setze w auf voxel_welt_neu(7)

# Ein einzelnes Voxel in einem frischen Chunk setzen. Das liegt in genau
# einer 8^3-Section; alle anderen Voxel dieser Section muessen 0 bleiben.
voxel_setzen(w, 2, 2, 2, 3)

# Das gesetzte Voxel:
zeige voxel_holen(w, 2, 2, 2)

# Direkte Nachbarn in derselben Section (0..7 auf jeder Achse) muessen Luft sein:
zeige voxel_holen(w, 3, 2, 2)
zeige voxel_holen(w, 2, 3, 2)
zeige voxel_holen(w, 2, 2, 3)
zeige voxel_holen(w, 1, 2, 2)

# Ecken der gleichen Section muessen Luft sein (kein faelschliches SOLID):
zeige voxel_holen(w, 0, 0, 0)
zeige voxel_holen(w, 7, 7, 7)

# Voxel in einer ANDEREN Section desselben Chunks (>=8) muss Luft sein:
zeige voxel_holen(w, 8, 8, 8)
zeige voxel_holen(w, 16, 0, 0)

# Zweites Voxel in derselben Section auf eine andere ID setzen: beide bleiben
# unabhaengig lesbar (PALETTE {0,3,5}), Rest weiter Luft.
voxel_setzen(w, 5, 5, 5, 5)
zeige voxel_holen(w, 2, 2, 2)
zeige voxel_holen(w, 5, 5, 5)
zeige voxel_holen(w, 4, 4, 4)

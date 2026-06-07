# Regression P006-R1T: 8^3-Section-Layout, SOLID -> PALETTE Upgrade.
# Eine uniform gefuellte Section ist intern SOLID(id). Wird darin EIN Voxel
# auf eine andere ID geaendert, muss die Section auf PALETTE{solid_id,new_id}
# upgraden: Das geaenderte Voxel liefert die neue ID, alle anderen weiterhin
# die alte solid_id. Rein ueber die public API getestet.

setze w auf voxel_welt_neu(7)

# Eine ganze 8^3-Section (0..7 je Achse) uniform mit ID 3 fuellen -> SOLID(3).
setze z auf 0
solange z < 8:
    setze y auf 0
    solange y < 8:
        setze x auf 0
        solange x < 8:
            voxel_setzen(w, x, y, z, 3)
            setze x auf x + 1
        setze y auf y + 1
    setze z auf z + 1

# Vor der Aenderung: alle Stichproben = 3
zeige voxel_holen(w, 0, 0, 0)
zeige voxel_holen(w, 7, 7, 7)
zeige voxel_holen(w, 4, 4, 4)

# EIN Voxel auf ID 5 aendern -> Upgrade auf PALETTE{3,5}
voxel_setzen(w, 4, 4, 4, 5)

# Das geaenderte Voxel liefert 5, alle anderen weiterhin 3:
zeige voxel_holen(w, 4, 4, 4)
zeige voxel_holen(w, 0, 0, 0)
zeige voxel_holen(w, 7, 7, 7)
zeige voxel_holen(w, 3, 4, 4)
zeige voxel_holen(w, 5, 4, 4)

# Geaendertes Voxel auf Luft setzen -> liefert 0, Rest bleibt 3
voxel_setzen(w, 4, 4, 4, 0)
zeige voxel_holen(w, 4, 4, 4)
zeige voxel_holen(w, 0, 0, 0)

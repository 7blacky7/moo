# Regression P005-1a: Invalider Handle MUSS werfen (P0.5-Contract).
# voxel_holen / voxel_setzen auf einem Wert der KEINE VoxelWorld ist
# (Zahl, String, nichts) darf NICHT still 0/nichts liefern, sondern
# moo_throw ausloesen -> in moo via versuche/fange fangbar.
# Genauso: out-of-range Block-ID bei voxel_setzen wirft (kein clamp).

funktion holen_auf(ungueltig):
    versuche:
        voxel_holen(ungueltig, 0, 0, 0)
        gib_zurück "KEIN-WURF"
    fange fehler:
        gib_zurück "wurf"

funktion setzen_auf(ungueltig):
    versuche:
        voxel_setzen(ungueltig, 0, 0, 0, 1)
        gib_zurück "KEIN-WURF"
    fange fehler:
        gib_zurück "wurf"

# Zahl als Handle -> wirft
zeige holen_auf(42)
zeige setzen_auf(42)
# String als Handle -> wirft
zeige holen_auf("keine welt")
# nichts als Handle -> wirft
zeige holen_auf(nichts)

# Gueltige Welt: out-of-range Block-ID wirft (kein stilles clamp)
setze w auf voxel_welt_neu(1)
versuche:
    voxel_setzen(w, 0, 0, 0, 9999)
    zeige "KEIN-WURF"
fange fehler:
    zeige "wurf"

# Gueltige Welt + gueltige ID -> KEIN Wurf, Wert kommt zurueck
voxel_setzen(w, 0, 0, 0, 3)
zeige voxel_holen(w, 0, 0, 0)

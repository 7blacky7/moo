# P008-A3B GIF-Smoke: VoxelWorld + 3D-Fenster -> 3 Frames -> GIF.
# Verdrahtet test_gif_start / test_gif_frame / test_gif_ende am echten
# Fenster-Grab (kein MOO_FRAME-Direktpfad). Beleg: Datei existiert + valide GIF.

zeige "== GIF-Smoke (P008-A3B) =="

# 1) VoxelWorld + kleine Plattform
setze welt auf voxel_welt_neu(4242)
setze z auf 0
solange z < 4:
    setze x auf 0
    solange x < 4:
        voxel_setzen(welt, x, 0, z, 1)
        setze x auf x + 1
    setze z auf z + 1
voxel_setzen(welt, 2, 1, 2, 3)

# 2) Fenster oeffnen (3D-Backend aktiv), Mesh bauen
setze win auf raum_erstelle("GIF Smoke", 320, 240)
raum_perspektive(win, 60.0, 0.1, 150.0)
setze render_id auf voxel_mesh_bauen(welt, 0, 0, 0)
voxel_aktualisieren(welt)

# 3) GIF starten — Dimensionen kommen aus dem ersten Grab des Fensters.
#    Ein erstes Bild rendern, DANN test_gif_start (das grabbt + schreibt Frame 1).
raum_löschen(win, 0.53, 0.81, 0.92)
raum_kamera(win, 9.0, 9.0, 9.0, 1.5, 0.0, 1.5)
wenn render_id >= 0:
    chunk_zeichne(render_id)
raum_aktualisieren(win)

setze gif auf test_gif_start(win, "tmp/gif_smoke.gif", 8)
zeige "gif gestartet (Frame 1 geschrieben)"

# 4) Zwei weitere Frames mit leicht bewegter Kamera -> Animation
setze i auf 1
solange i < 3:
    raum_löschen(win, 0.53, 0.81, 0.92)
    raum_kamera(win, 9.0 + i, 9.0, 9.0 - i, 1.5, 0.0, 1.5)
    wenn render_id >= 0:
        chunk_zeichne(render_id)
    raum_aktualisieren(win)
    test_gif_frame(gif, win)
    zeige "frame " + text(i + 1) + " geschrieben"
    setze i auf i + 1

# 5) GIF abschliessen + Fenster zu
test_gif_ende(gif)
raum_schliessen(win)
zeige "== GIF-Smoke OK: tmp/gif_smoke.gif =="

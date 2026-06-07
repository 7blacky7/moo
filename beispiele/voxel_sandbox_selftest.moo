# ============================================================
# voxel_sandbox_selftest.moo — Screenshot-Selftest fuer die Voxel-Sandbox
# (Plan-005 Phase 4, V4.2 Regressionstest)
#
# Reproduzierbar headless (xvfb + gl33). Ablauf:
#   1. VoxelWelt generieren (fester Seed) + Mesh bauen.
#   2. Hybrid-Fenster oeffnen, Fly-Cam auf das Terrain richten.
#   3. Ein paar Frames rendern (Backbuffer fuellen).
#   4. Screenshot VOR dem Abbau -> /tmp/voxel_sandbox_before.bmp
#   5. Maus-Simulation: einen Abbau-Klick ausfuehren (LMB), den
#      getroffenen Block via voxel_strahl bestimmen und entfernen.
#   6. Remesh + ein paar Frames + Screenshot NACH dem Abbau
#      -> /tmp/voxel_sandbox_after.bmp
#   7. Block-Zaehler vorher/nachher als deterministische Selbstpruefung
#      ausgeben (ein Block weniger nach dem Abbau).
#
# Die eigentliche Datei-Existenz-/Groessenpruefung uebernimmt der
# Wrapper (siehe Kommentar am Dateiende). Dieses Skript liefert die
# BMPs reproduzierbar + die Block-Diff-Zahlen.
#
# Start (headless):
#   xvfb-run -a -s "-screen 0 1280x800x24" \
#     env MOO_3D_BACKEND=gl33 \
#     moo-compiler run beispiele/voxel_sandbox_selftest.moo
# ============================================================

konstante WIN_B auf 1280
konstante WIN_H auf 800

konstante LUFT auf 0
konstante GRAS auf 1

setze welt auf voxel_welt_neu(1337)

# Terrain um den Spawn (gleicher Bereich wie die Demo).
konstante GEN_RADIUS auf 2
setze chunks_neu auf 0
für gcx in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
    für gcy in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
        setze chunks_neu auf chunks_neu + voxel_generieren(welt, gcx, gcy)
zeige "[selftest] Terrain generiert: " + text(chunks_neu) + " Chunks"

# Fenster (Hybrid 2D+3D, gl33-Bridge).
setze win auf fenster_unified("voxel selftest", WIN_B, WIN_H)
raum_perspektive(win, 70.0, 0.1, 300.0)

# Fly-Cam: ueber dem Gebiet, Blick nach unten auf das Terrain.
setze cam_x auf 16.0
setze cam_y auf 16.0
setze cam_z auf 40.0
setze yaw auf 3.926
setze pitch auf (0.0 - 0.6)

funktion blick_dx(yaw, pitch):
    gib_zurück cosinus(pitch) * cosinus(yaw)
funktion blick_dy(yaw, pitch):
    gib_zurück cosinus(pitch) * sinus(yaw)
funktion blick_dz(yaw, pitch):
    gib_zurück sinus(pitch)

setze dx auf blick_dx(yaw, pitch)
setze dy auf blick_dy(yaw, pitch)
setze dz auf blick_dz(yaw, pitch)

# Mesh bauen + Render-IDs sammeln. WICHTIG: gl33 zeichnet Chunks NICHT
# automatisch (nur Vulkan macht Auto-Draw in vk_swap). Auf gl33/Hybrid
# muessen wir die VBO-Chunks pro Frame explizit mit chunk_zeichne replayen.
# voxel_mesh_bauen(welt, cx, cy, cz) remesht den Chunk und liefert seine
# Render-Chunk-ID (-1 = kein Cache). Vertikal deckt cz=0..1 das Terrain ab.
setze chunk_ids auf []
für mcx in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
    für mcy in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
        für mcz in 0..2:
            setze rid auf voxel_mesh_bauen(welt, mcx, mcy, mcz)
            wenn rid >= 0:
                chunk_ids.hinzufügen(rid)
zeige "[selftest] Render-Chunks gemesht: " + text(länge(chunk_ids))

# Hilfsfunktion: ein Render-Frame (Terrain-Chunks + 2D-Overlay).
funktion render_frame(win, chunk_ids, cam_x, cam_y, cam_z, dx, dy, dz):
    raum_löschen(win, 0.53, 0.81, 0.92)
    raum_kamera(win, cam_x, cam_y, cam_z, cam_x + dx, cam_y + dy, cam_z + dz)
    # Alle Terrain-Chunks zeichnen (gl33 VBO-Replay).
    für ci in chunk_ids:
        chunk_zeichne(ci)
    # 2D-Marker damit der Hybrid-Overlay-Pfad mitgetestet wird.
    zeichne_rechteck_z(win, WIN_B / 2 - 10, WIN_H / 2 - 1, 0.40, 20, 2, "#FFFFFF")
    zeichne_rechteck_z(win, WIN_B / 2 - 1, WIN_H / 2 - 10, 0.40, 2, 20, "#FFFFFF")
    hybrid_aktualisieren(win)

# === 3 Frames zum Fuellen des Backbuffers, dann Screenshot VOR Abbau ===
setze f auf 0
solange f < 3:
    render_frame(win, chunk_ids, cam_x, cam_y, cam_z, dx, dy, dz)
    warte(16)
    setze f auf f + 1

setze ok_before auf raum_screenshot_bmp(win, "/tmp/voxel_sandbox_before.bmp")
zeige "[selftest] Screenshot before geschrieben: " + text(ok_before)

# === Maus-Simulation: Cursor in die Bildmitte, LMB druecken/loslassen ===
# (Belegt den Maus-Sim-Pfad; der eigentliche Abbau laeuft deterministisch
#  ueber denselben voxel_strahl-Pfad wie die Demo.)
raum_sim_maus_pos(win, WIN_B / 2, WIN_H / 2)
raum_sim_maus_taste(win, "links", wahr)
render_frame(win, chunk_ids, cam_x, cam_y, cam_z, dx, dy, dz)
raum_sim_maus_taste(win, "links", falsch)

# Raycast vom Auge in Blickrichtung -> getroffenen Block bestimmen.
setze treffer auf voxel_strahl(welt, cam_x, cam_y, cam_z, dx, dy, dz, 64.0)
setze entfernt auf falsch
wenn treffer["hit"]:
    setze hx auf treffer["x"]
    setze hy auf treffer["y"]
    setze hz auf treffer["z"]
    setze id_vorher auf voxel_holen(welt, hx, hy, hz)
    voxel_setzen(welt, hx, hy, hz, LUFT)
    setze id_nachher auf voxel_holen(welt, hx, hy, hz)
    voxel_aktualisieren(welt)
    setze entfernt auf (id_vorher != LUFT und id_nachher == LUFT)
    zeige "[selftest] Abbau bei (" + text(hx) + "," + text(hy) + "," + text(hz) + "): id " + text(id_vorher) + " -> " + text(id_nachher)
sonst:
    zeige "[selftest] WARN: kein Block im Strahl getroffen"

# === 3 Frames + Screenshot NACH Abbau ===
setze f auf 0
solange f < 3:
    render_frame(win, chunk_ids, cam_x, cam_y, cam_z, dx, dy, dz)
    warte(16)
    setze f auf f + 1

setze ok_after auf raum_screenshot_bmp(win, "/tmp/voxel_sandbox_after.bmp")
zeige "[selftest] Screenshot after geschrieben: " + text(ok_after)

hybrid_schliessen(win)

# Deterministisches Selbst-Ergebnis (vom Wrapper grep-bar).
wenn entfernt:
    zeige "[selftest] ERGEBNIS: OK (Block abgebaut, 2 Screenshots geschrieben)"
sonst:
    zeige "[selftest] ERGEBNIS: TEILWEISE (Screenshots geschrieben, kein Abbau)"

# ============================================================
# Wrapper-Pruefung (Datei-Existenz/Groesse) erfolgt extern, z.B.:
#   test -s /tmp/voxel_sandbox_before.bmp && test -s /tmp/voxel_sandbox_after.bmp
# Beide BMPs muessen existieren und > 0 Byte sein.
# ============================================================

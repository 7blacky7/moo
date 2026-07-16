# ============================================================
# voxel_sandbox_selftest_vulkan.moo — Vulkan-Visual-Selftest
# fuer die Voxel-Sandbox (Plan-006 R5).
#
# IDENTISCHE Welt/Seed (1337), Kamera und Abbau-Logik wie
# beispiele/voxel_sandbox_selftest.moo, damit gl33 und vulkan
# direkt vergleichbar sind. Unterschiede:
#   - Screenshots -> /tmp/voxel_vulkan_before.bmp / _after.bmp
#   - explizit als Vulkan-Lauf gedacht (MOO_3D_BACKEND=vulkan)
#
# BACKEND-SEMANTIK (Memory voxel-demo-render-gotcha-gl33-chunkdraw):
#   - Vulkan zeichnet in vk_swap automatisch ALLE kompilierten
#     Chunk-Slots; chunk_zeichne ist dort ein NO-OP.
#   - gl33/gl21 brauchen chunk_zeichne pro Frame pro Chunk.
# Dieser Test ruft chunk_zeichne trotzdem pro Frame auf: auf Vulkan
# schadet das NICHT (no-op), auf gl33 ist es zwingend. Damit ist das
# Skript backend-portabel und nutzt auf BEIDEN Backends den exakt
# gleichen Render-Pfad wie der gl33-Selftest -> fairer Vergleich.
#
# START (echter lokaler Vulkan-Lauf, GPU-Session, kein xvfb):
#   env MOO_3D_BACKEND=vulkan \
#     moo-compiler run beispiele/voxel_sandbox_selftest_vulkan.moo
#
# Automatisierter Run + Validierung (non-blank, Farbschwelle,
# gl33-Vergleich) inkl. CI-Skip:
#   beispiele/voxel_vulkan_visual_test.sh
# ============================================================

konstante WIN_B auf 1280
konstante WIN_H auf 800

konstante LUFT auf 0

setze welt auf voxel_welt_neu(1337)

# Terrain um den Spawn (gleicher Bereich/Seed wie der gl33-Selftest).
konstante GEN_RADIUS auf 2
setze chunks_neu auf 0
für gcx in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
    für gcy in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
        setze chunks_neu auf chunks_neu + voxel_generieren(welt, gcx, gcy)
zeige "[vk-selftest] Terrain generiert: " + text(chunks_neu) + " Chunks"

# Hybrid-Fenster (2D+3D). Unter MOO_3D_BACKEND=vulkan laeuft die
# 3D-Szene ueber das Vulkan-Backend (vk_swap-Auto-Draw).
setze win auf fenster_unified("voxel vulkan selftest", WIN_B, WIN_H)
raum_perspektive(win, 70.0, 0.1, 300.0)

# Fly-Cam: ueber dem Gebiet, Blick nach unten auf das Terrain
# (exakt wie gl33-Selftest fuer 1:1-Vergleichbarkeit).
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

# Mesh bauen + Render-IDs sammeln. voxel_mesh_bauen(welt, cx, cy, cz)
# kompiliert den Chunk (auf Vulkan landet er damit im vk_swap-Auto-Draw)
# und liefert die Render-Chunk-ID (-1 = leer/kein Cache).
setze chunk_ids auf []
für mcx in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
    für mcy in (0 - GEN_RADIUS)..(GEN_RADIUS + 1):
        für mcz in 0..2:
            setze rid auf voxel_mesh_bauen(welt, mcx, mcy, mcz)
            wenn rid >= 0:
                chunk_ids.hinzufügen(rid)
zeige "[vk-selftest] Render-Chunks gemesht: " + text(länge(chunk_ids))

# Hilfsfunktion: ein Render-Frame.
# chunk_zeichne: gl33 = VBO-Replay (Pflicht), vulkan = no-op (vk_swap
# zeichnet alle kompilierten Chunks selbst).
funktion render_frame(win, chunk_ids, cam_x, cam_y, cam_z, dx, dy, dz):
    raum_löschen(win, 0.53, 0.81, 0.92)
    raum_kamera(win, cam_x, cam_y, cam_z, cam_x + dx, cam_y + dy, cam_z + dz)
    für ci in chunk_ids:
        chunk_zeichne(ci)
    zeichne_rechteck_z(win, WIN_B / 2 - 10, WIN_H / 2 - 1, 0.40, 20, 2, "#FFFFFF")
    zeichne_rechteck_z(win, WIN_B / 2 - 1, WIN_H / 2 - 10, 0.40, 2, 20, "#FFFFFF")
    hybrid_aktualisieren(win)

# === 3 Frames zum Fuellen des Backbuffers, dann Screenshot VOR Abbau ===
setze f auf 0
solange f < 3:
    render_frame(win, chunk_ids, cam_x, cam_y, cam_z, dx, dy, dz)
    warte(16)
    setze f auf f + 1

setze ok_before auf raum_screenshot_bmp(win, "/tmp/voxel_vulkan_before.bmp")
zeige "[vk-selftest] Screenshot before geschrieben: " + text(ok_before)

# === Maus-Sim + deterministischer Abbau via voxel_strahl ===
raum_sim_maus_pos(win, WIN_B / 2, WIN_H / 2)
raum_sim_maus_taste(win, "links", wahr)
render_frame(win, chunk_ids, cam_x, cam_y, cam_z, dx, dy, dz)
raum_sim_maus_taste(win, "links", falsch)

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
    zeige "[vk-selftest] Abbau bei (" + text(hx) + "," + text(hy) + "," + text(hz) + "): id " + text(id_vorher) + " -> " + text(id_nachher)
sonst:
    zeige "[vk-selftest] WARN: kein Block im Strahl getroffen"

# === 3 Frames + Screenshot NACH Abbau ===
setze f auf 0
solange f < 3:
    render_frame(win, chunk_ids, cam_x, cam_y, cam_z, dx, dy, dz)
    warte(16)
    setze f auf f + 1

setze ok_after auf raum_screenshot_bmp(win, "/tmp/voxel_vulkan_after.bmp")
zeige "[vk-selftest] Screenshot after geschrieben: " + text(ok_after)

hybrid_schliessen(win)

wenn entfernt:
    zeige "[vk-selftest] ERGEBNIS: OK (Block abgebaut, 2 Screenshots geschrieben)"
sonst:
    zeige "[vk-selftest] ERGEBNIS: TEILWEISE (Screenshots geschrieben, kein Abbau)"

# ============================================================
# Pixel-Validierung (non-blank, Farbanzahl, gl33-Vergleich) erfolgt
# extern via compiler/tests/regression/voxel_screenshot_check.py.
# Wrapper mit CI-Skip: beispiele/voxel_vulkan_visual_test.sh
# ============================================================

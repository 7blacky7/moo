# Regression P005-1b: moo-Level Mesh-Smoke (Phase 1b, Agent p005-t2).
# Validiert das CG2-Binding voxel_mesh_bauen / voxel_aktualisieren + das
# Codegen-Wiring end-to-end. Headless (kein GL/Vulkan-Backend in der
# Testsuite): ohne aktives Backend legt der Mesher KEINEN GPU-Cache an
# (Risiko 10, GPU-Cache-Lifetime) -> mesh_bauen liefert -1 und
# aktualisieren 0, OHNE zu crashen. Genau das ist das Akzeptanzkriterium
# "kein Crash ohne aktiven Backend-Kontext". Die echte 1-Frame-GPU-Smoke
# (Fenster auf, Block setzen, rendern) ist CG2-Scope (Task d8593d54).

setze w auf voxel_welt_neu(7)

# Zwei benachbarte Bloecke in Chunk (0,0,0) setzen.
voxel_setzen(w, 5, 5, 5, 3)
voxel_setzen(w, 6, 5, 5, 1)

# Mesh bauen ohne Backend -> -1 (keine Render-ID), kein Crash.
setze rid auf voxel_mesh_bauen(w, 0, 0, 0)
zeige rid

# aktualisieren ohne Backend -> 0 remeshte Chunks.
setze n auf voxel_aktualisieren(w)
zeige n

# Alias build_chunk_mesh muss auf denselben Builtin zeigen.
setze rid2 auf build_chunk_mesh(w, 0, 0, 0)
zeige rid2

zeige "smoke-ok"

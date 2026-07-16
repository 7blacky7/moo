# Regression P005-1d: moo-Level Raycast-/AABB-Smoke (Phase 1d, Agent p005-cg3).
# Validiert die CG3-Bindings voxel_strahl/voxel_raycast + voxel_aabb/voxel_overlap
# end-to-end (runtime_bindings.rs + codegen.rs Wiring). Headless: Raycast/AABB
# sind reine CPU-Lookups und brauchen KEIN GL/Vulkan-Backend.
#
# Erwartete Werte exakt nach RT4's verifiziertem C-ASan-Harness
# (test_voxel_raycast_asan.c, test_ray_hit_axis + test_aabb_hit_and_count).
# Zahlen-Formatierung (moo_print.c): ganzzahlige Doubles -> "%lld" (5, 0, -1),
# sonst "%g" (4.5); Bool -> wahr/falsch.

setze w auf voxel_welt_neu(0)

# --- Raycast: Block (5,0,0)=stein, Strahl von (0.5,0.5,0.5) entlang +X ---
voxel_setzen(w, 5, 0, 0, 3)
setze r auf voxel_strahl(w, 0.5, 0.5, 0.5, 1, 0, 0, 100)

# Dict-Felder gemaess P0.5-Contract lesen und ausgeben:
zeige r["hit"]      # wahr
zeige r["x"]        # 5
zeige r["y"]        # 0
zeige r["z"]        # 0
zeige r["nx"]       # -1 (Einstieg von -X-Seite)
zeige r["ny"]       # 0
zeige r["nz"]       # 0
zeige r["id"]       # 3 (stein)
zeige r["dist"]     # 4.5 (Start x=0.5 bis Einstiegs-Face x=5)

# Alias voxel_raycast muss denselben Builtin treffen.
setze r2 auf voxel_raycast(w, 0.5, 0.5, 0.5, 1, 0, 0, 100)
zeige r2["x"]       # 5

# Miss: leerer Raum entlang +Y.
setze rm auf voxel_strahl(w, 0.5, 0.5, 0.5, 0, 1, 0, 100)
zeige rm["hit"]     # falsch

# --- AABB: drei solide Bloecke, Box ueberlappt 3 Zellen ---
voxel_setzen(w, 0, 0, 0, 1)
voxel_setzen(w, 1, 0, 0, 1)
voxel_setzen(w, 0, 1, 0, 1)
setze a auf voxel_aabb(w, 0, 0, 0, 2, 2, 1)
zeige a["hit"]      # wahr
zeige a["count"]    # 3
zeige a["x"]        # 0 (erster Treffer in Scan-Reihenfolge)
zeige a["y"]        # 0
zeige a["z"]        # 0

# Alias voxel_overlap: Box ueber leerem Raum -> Miss.
setze am auf voxel_overlap(w, 20, 20, 20, 22, 22, 22)
zeige am["hit"]     # falsch
zeige am["count"]   # 0

zeige "raycast-smoke-ok"

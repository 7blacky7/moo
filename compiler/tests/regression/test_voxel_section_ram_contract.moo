# Regression P006-R1T: K2-Contract der ram_statistik nach Section-Migration.
# Die 6 P0.5-Keys (chunks/bytes_blocks/bytes_palette/bytes_mesh/bytes_total/
# empty_chunks) bleiben STABIL und lesbar. Werte werden NUR qualitativ
# geprueft (>=0 / >0 / Vergleiche), NICHT als exakte Byte-Zahlen - Lehre aus
# dem bytes_total-Bug von P005-T1: exakte Erwartungen brechen bei jeder
# Layout-Aenderung. bytes_sections (additiv, optional) wird hier NICHT
# abgefragt (Dict-Key koennte je nach Implementierung fehlen) - das deckt
# der C-ASan-Harness ab.

setze w auf voxel_welt_neu(1)

# --- Frische Welt: 0 Chunks, alle Keys vorhanden ---
setze s0 auf voxel_ram_statistik(w)
zeige s0["chunks"]
zeige s0["bytes_blocks"] >= 0
zeige s0["bytes_palette"] >= 0
zeige s0["bytes_mesh"] >= 0
zeige s0["bytes_total"] >= 0
zeige s0["empty_chunks"] >= 0

# --- Nach einem Set: >=1 Chunk, bytes_total waechst gegenueber leer ---
voxel_setzen(w, 1, 2, 3, 4)
setze s1 auf voxel_ram_statistik(w)
zeige s1["chunks"] >= 1
zeige s1["bytes_total"] > s0["bytes_total"]
zeige s1["bytes_blocks"] >= 0
zeige s1["bytes_palette"] >= 0

# --- Voxel wieder auf Luft: Chunk wird (qualitativ) wieder billig ---
# Robuste Invariante: der bei-belegt gemessene bytes_total darf nach dem
# Zuruecksetzen NICHT weiter ANWACHSEN (kein Leak/falsches SOLID-Aufblaehen).
# Wir fordern NICHT exakte Rueckkehr auf s0 (Downgrade ist in R1 optional),
# nur: nicht groesser als der belegte Zustand und weiterhin alle Keys da.
voxel_setzen(w, 1, 2, 3, 0)
setze s2 auf voxel_ram_statistik(w)
zeige s2["bytes_total"] <= s1["bytes_total"]
zeige s2["chunks"] >= 0
zeige s2["empty_chunks"] >= 0

# --- Ein nur-Luft-Set in einen NIE beruehrten, weit entfernten Chunk darf
#     keine teuren Blockdaten erzeugen (leerer Chunk bleibt leer/NULL) ---
setze w2 auf voxel_welt_neu(2)
voxel_setzen(w2, 9000, 0, 9000, 0)
setze s3 auf voxel_ram_statistik(w2)
zeige s3["bytes_blocks"] == 0
zeige s3["bytes_palette"] == 0

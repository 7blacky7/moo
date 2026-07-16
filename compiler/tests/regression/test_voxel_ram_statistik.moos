# Regression P005-1a: voxel_ram_statistik liefert Dict mit stabilen Keys
# (P0.5-Contract): chunks, bytes_blocks, bytes_palette, bytes_mesh,
# bytes_total, empty_chunks. Frische Welt = 0 Chunks. Nach einem
# gesetzten Block ist >=1 Chunk allokiert und bytes_total > 0.
# Wir geben nur stabile, implementierungs-unabhaengige Eigenschaften aus
# (Praesenz der Keys + qualitative Schwellen), nicht die exakten Byte-Zahlen.

setze w auf voxel_welt_neu(1)
setze s0 auf voxel_ram_statistik(w)

# Alle Contract-Keys muessen existieren (Zugriff darf nicht werfen).
# Frische Welt: 0 Chunks. bytes_total ist implementierungsabhaengig
# (Verwaltungs-Overhead World-Struct + Hashmap-Tabelle zaehlt P0.5-konform
# mit, seit RT1/fd3cd98), daher KEINE exakte Byte-Erwartung -> qualitative
# Schwelle (>=0). chunks==0 ist die stabile, aussagekraeftige Invariante.
zeige s0["chunks"]
zeige s0["bytes_total"] >= 0

# Einen Block setzen -> mindestens 1 Chunk, bytes_total > 0.
voxel_setzen(w, 0, 0, 0, 3)
setze s1 auf voxel_ram_statistik(w)
zeige s1["chunks"] >= 1
zeige s1["bytes_total"] > 0
zeige s1["bytes_blocks"] >= 0
zeige s1["bytes_palette"] >= 0
zeige s1["bytes_mesh"] >= 0
zeige s1["empty_chunks"] >= 0

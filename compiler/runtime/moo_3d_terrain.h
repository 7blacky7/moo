#ifndef MOO_3D_TERRAIN_H
#define MOO_3D_TERRAIN_H

/**
 * moo_3d_terrain.h — Smooth Terrain-Mesh-Generator fuer GL 3.3 Backend.
 * Generiert Triangle-Mesh aus Heightmap mit Normalen und Vertex-Farben.
 * Vertex-Format: pos(3f) + color(3f) + normal(3f) = 9 floats = 36 bytes.
 */

#include <stdint.h>

/* Vertex: 9 floats = 36 bytes */
#define TERRAIN_FLOATS_PER_VERTEX 9

typedef struct {
    float x, y, z;     /* Position */
    float r, g, b;     /* Farbe */
    float nx, ny, nz;  /* Normale */
} TerrainVertex;

/* Generiertes Mesh — Caller muss vertices freigeben */
typedef struct {
    TerrainVertex* vertices;
    int vertex_count;
    uint32_t* indices;
    int index_count;
} TerrainMesh;

/* Biom-Typ fuer Farbbestimmung */
typedef enum {
    BIOM_OZEAN,
    BIOM_STRAND,
    BIOM_WIESE,
    BIOM_WALD,
    BIOM_BERG,
    BIOM_SCHNEE,
    BIOM_FLUSS
} BiomTyp;

/**
 * Callback: Liefert die Hoehe an Welt-Position (x, z).
 * Wird vom moo-Programm bereitgestellt (terrain_hoehe).
 */
typedef float (*TerrainHeightFn)(float x, float z);

/**
 * Callback: Liefert den Biom-Typ an Position (x, z).
 */
typedef int (*TerrainBiomFn)(float x, float z);

/**
 * Generiert ein Smooth-Terrain-Mesh fuer einen Chunk.
 *
 * @param chunk_x, chunk_z  Chunk-Ursprung in Welt-Koordinaten
 * @param chunk_size        Seitenlaenge des Chunks (z.B. 16)
 * @param resolution        Vertices pro Kante (z.B. 17 fuer 16 Quads)
 * @param height_fn         Callback fuer Hoehe an (x, z)
 * @param biom_fn           Callback fuer Biom an (x, z), oder NULL
 * @param out               Ausgabe-Mesh (Caller muss terrain_mesh_free aufrufen)
 */
void terrain_mesh_generate(
    float chunk_x, float chunk_z,
    int chunk_size, int resolution,
    TerrainHeightFn height_fn,
    TerrainBiomFn biom_fn,
    TerrainMesh* out
);

/**
 * Generiert ein Wasser-Mesh auf fester Hoehe (Meeresspiegel).
 */
void terrain_mesh_water(
    float chunk_x, float chunk_z,
    int chunk_size, int resolution,
    float water_level,
    TerrainMesh* out
);

/**
 * Gibt Mesh-Speicher frei.
 */
void terrain_mesh_free(TerrainMesh* mesh);

/**
 * Biom-Farbe nachschlagen. Gibt RGB als 3 floats zurueck.
 */
void terrain_biom_color(int biom, float* r, float* g, float* b);

/**
 * Berechnet interpolierte Biom-Farbe mit sanftem Uebergang.
 * Sampelt 4 Nachbar-Positionen und mittelt die Farben.
 */
void terrain_biom_color_smooth(
    float x, float z,
    TerrainBiomFn biom_fn,
    float* r, float* g, float* b
);

#endif

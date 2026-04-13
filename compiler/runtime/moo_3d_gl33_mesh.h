/**
 * moo_3d_gl33_mesh.h — VBO/VAO Mesh-System fuer GL 3.3 Backend.
 * Vertex-Format, Buffer-Erstellung, Chunk-Caching als VBOs.
 * Wird von moo_3d_gl33.c eingebunden.
 */

#ifndef MOO_3D_GL33_MESH_H
#define MOO_3D_GL33_MESH_H

#include <stdbool.h>

/* Needs GL 3.3 types — includer must have GL headers loaded */

/* === Vertex Format === */
/* pos(3f) + color(3f) + normal(3f) = 9 floats = 36 bytes per vertex */
typedef struct {
    float px, py, pz;   /* position */
    float r, g, b;      /* color */
    float nx, ny, nz;   /* normal */
} MooVertex;

#define MOO_VERTEX_STRIDE (sizeof(MooVertex))
#define MOO_VERTEX_POS_OFFSET    0
#define MOO_VERTEX_COLOR_OFFSET  (3 * sizeof(float))
#define MOO_VERTEX_NORMAL_OFFSET (6 * sizeof(float))

/* === Dynamic Mesh Builder === */
/* Collects vertices during chunk_begin..chunk_end, then uploads to VBO */
#define MESH_INITIAL_CAP 1024

typedef struct {
    MooVertex* vertices;
    int count;
    int capacity;
} MeshBuilder;

void mesh_builder_init(MeshBuilder* mb);
void mesh_builder_reset(MeshBuilder* mb);
void mesh_builder_free(MeshBuilder* mb);
void mesh_builder_add_vertex(MeshBuilder* mb,
                             float px, float py, float pz,
                             float r, float g, float b,
                             float nx, float ny, float nz);
void mesh_builder_add_quad(MeshBuilder* mb,
                           float x, float y, float z, float size,
                           float r, float g, float b,
                           float nx, float ny, float nz,
                           const float* v0, const float* v1,
                           const float* v2, const float* v3);
void mesh_builder_add_cube(MeshBuilder* mb,
                           float x, float y, float z, float size,
                           float r, float g, float b);

/* === VBO/VAO Chunk System === */
#define MAX_GL33_CHUNKS 256

typedef struct {
    unsigned int vao;
    unsigned int vbo;
    int vertex_count;
    bool is_compiled;
    bool is_used;
} GL33Chunk;

void gl33_chunk_system_init(GL33Chunk* chunks);
int  gl33_chunk_alloc(GL33Chunk* chunks);
void gl33_chunk_upload(GL33Chunk* chunks, int id, MeshBuilder* mb);
void gl33_chunk_draw(GL33Chunk* chunks, int id);
void gl33_chunk_delete(GL33Chunk* chunks, int id);

#endif

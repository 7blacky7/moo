#ifndef P2_MESH_REF_H
#define P2_MESH_REF_H
#include <stddef.h>
#include <stdint.h>

/* CPU reference only: an owned snapshot, not a GPU resource or public Moo ABI. */
typedef struct { float position[3], color[3], normal[3]; } P2Vertex;
typedef struct P2Mesh P2Mesh;
typedef enum { P2_MESH_OK, P2_MESH_ARGUMENT, P2_MESH_RANGE, P2_MESH_MEMORY } P2MeshStatus;

/* On failure *out is NULL. Inputs remain owned by caller; zero counts are valid.
 * Nonzero index count requires a multiple of three and valid vertex indices. */
P2MeshStatus p2_mesh_create(P2Mesh **out, const P2Vertex *vertices, size_t vertex_count,
                            const uint32_t *indices, size_t index_count);
void p2_mesh_destroy(P2Mesh *mesh);
size_t p2_mesh_vertex_count(const P2Mesh *mesh);
size_t p2_mesh_index_count(const P2Mesh *mesh);
/* Draw reference: transform indexed positions into caller output, four floats
 * per index. Output must not overlap the matrix or retained mesh data.
 * No rasterization occurs. */
P2MeshStatus p2_mesh_transform(const P2Mesh *mesh, const float matrix[16],
                               float *out_xyzw, size_t out_float_count);
#endif

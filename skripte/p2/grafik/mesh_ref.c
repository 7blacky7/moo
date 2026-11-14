#include "mesh_ref.h"
#include "mat_ref.h"
#include <stdlib.h>
#include <string.h>

struct P2Mesh {
    P2Vertex *vertices;
    uint32_t *indices;
    size_t vertex_count, index_count;
};
P2MeshStatus p2_mesh_create(P2Mesh **out, const P2Vertex *vertices, size_t nv,
                            const uint32_t *indices, size_t ni) {
    if (!out) return P2_MESH_ARGUMENT;
    *out = NULL;
    if ((nv && !vertices) || (ni && !indices)) return P2_MESH_ARGUMENT;
    if (ni % 3 != 0) return P2_MESH_ARGUMENT;
    if (nv > SIZE_MAX / sizeof(P2Vertex) || ni > SIZE_MAX / sizeof(uint32_t))
        return P2_MESH_RANGE;
    for (size_t i = 0; i < ni; ++i)
        if ((size_t)indices[i] >= nv) return P2_MESH_RANGE;
    P2Mesh *m = calloc(1, sizeof(*m));
    if (!m) return P2_MESH_MEMORY;
    if (nv) {
        m->vertices = malloc(nv * sizeof(*m->vertices));
        if (!m->vertices) { free(m); return P2_MESH_MEMORY; }
        memcpy(m->vertices, vertices, nv * sizeof(*m->vertices));
    }
    if (ni) {
        m->indices = malloc(ni * sizeof(*m->indices));
        if (!m->indices) { free(m->vertices); free(m); return P2_MESH_MEMORY; }
        memcpy(m->indices, indices, ni * sizeof(*m->indices));
    }
    m->vertex_count = nv; m->index_count = ni;
    *out = m;
    return P2_MESH_OK;
}
void p2_mesh_destroy(P2Mesh *m) {
    if (!m) return;
    free(m->vertices); free(m->indices); free(m);
}
size_t p2_mesh_vertex_count(const P2Mesh *m) { return m ? m->vertex_count : 0; }
size_t p2_mesh_index_count(const P2Mesh *m) { return m ? m->index_count : 0; }
P2MeshStatus p2_mesh_transform(const P2Mesh *m, const float matrix[16],
                               float *out, size_t out_count) {
    if (!m || !matrix) return P2_MESH_ARGUMENT;
    if (m->index_count > SIZE_MAX / 4) return P2_MESH_RANGE;
    if (out_count < m->index_count * 4 || (m->index_count && !out))
        return P2_MESH_ARGUMENT;
    for (size_t i = 0; i < m->index_count; ++i) {
        const P2Vertex *v = &m->vertices[m->indices[i]];
        const float in[4] = {v->position[0], v->position[1], v->position[2], 1.0f};
        p2_mat4_vec4(out + 4*i, matrix, in);
    }
    return P2_MESH_OK;
}

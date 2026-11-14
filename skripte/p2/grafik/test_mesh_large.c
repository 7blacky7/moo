#include "mesh_ref.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { VERTICES = 4, TRIANGLES = 100000, INDICES = TRIANGLES * 3 };
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); goto fail; } } while (0)

int main(void) {
    P2Vertex vertices[VERTICES] = {
        {.position = {0, 1, 2}}, {.position = {3, 4, 5}},
        {.position = {-1, -2, -3}}, {.position = {7, 8, 9}}
    };
    const float expected[VERTICES][4] = {
        {10, -4, 4, 1}, {13, -1, 7, 1},
        {9, -7, -1, 1}, {17, 3, 11, 1}
    };
    const float matrix[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, -5, 2, 1
    };
    uint32_t *indices = malloc((size_t)INDICES * sizeof(*indices));
    float *out = malloc((size_t)INDICES * 4 * sizeof(*out));
    P2Mesh *mesh = NULL;
    P2Mesh *invalid = NULL;
    CHECK(indices && out);
    for (size_t i = 0; i < INDICES; ++i) indices[i] = (uint32_t)(i % VERTICES);

    indices[INDICES - 1] = VERTICES;
    CHECK(p2_mesh_create(&invalid, vertices, VERTICES, indices, INDICES) == P2_MESH_RANGE);
    CHECK(invalid == NULL);
    indices[INDICES - 1] = (INDICES - 1) % VERTICES;
    CHECK(p2_mesh_create(&mesh, vertices, VERTICES, indices, INDICES) == P2_MESH_OK);
    CHECK(mesh && p2_mesh_vertex_count(mesh) == VERTICES);
    CHECK(p2_mesh_index_count(mesh) == INDICES);

    /* Inputs can change after creation: the mesh must retain its own copy. */
    vertices[0].position[0] = 999;
    for (size_t i = 0; i < INDICES; ++i) indices[i] = 0;
    CHECK(p2_mesh_transform(mesh, matrix, out, (size_t)INDICES * 4 - 1) == P2_MESH_ARGUMENT);
    for (int pass = 0; pass < 2; ++pass) {
        CHECK(p2_mesh_transform(mesh, matrix, out, (size_t)INDICES * 4) == P2_MESH_OK);
        for (size_t i = 0; i < INDICES; ++i)
            for (size_t component = 0; component < 4; ++component)
                CHECK(out[i * 4 + component] == expected[i % VERTICES][component]);
    }
    p2_mesh_destroy(mesh);
    free(indices);
    free(out);
    puts("CPU mesh: 100000 indexed triangles, snapshot, bounds and reuse OK");
    return 0;
fail:
    p2_mesh_destroy(invalid);
    p2_mesh_destroy(mesh);
    free(indices);
    free(out);
    return 1;
}

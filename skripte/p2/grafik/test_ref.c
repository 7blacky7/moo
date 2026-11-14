#include "mesh_ref.h"
#include "mat_ref.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The production C implementation is linked only for this differential test. */
void mat4_multiply(float *result, const float *a, const float *b);

int main(void) {
    float id[16], a[16], b[16], product[16], v[4] = {1,2,3,1}, out[4];
    p2_mat4_identity(id);
    memcpy(a, id, sizeof a); a[12]=10; a[13]=20; a[14]=30;
    memcpy(b, id, sizeof b); b[0]=2; b[5]=3; b[10]=4;
    p2_mat4_mul(product, a, b);
    float production[16];
    mat4_multiply(production, a, b);
    assert(memcmp(product, production, sizeof product)==0);
    p2_mat4_vec4(out, product, v);
    assert(out[0]==12 && out[1]==26 && out[2]==42 && out[3]==1);
    p2_mat4_mul(a, a, b);
    assert(memcmp(a, product, sizeof a)==0);
    p2_mat4_mul(b, id, b);
    assert(b[0]==2 && b[5]==3 && b[10]==4);
    p2_mat4_vec4(v, product, v);
    assert(memcmp(v, out, sizeof v)==0);

    P2Vertex vertices[3] = {{{1,2,3},{1,0,0},{0,0,1}},
                            {{0,0,0},{0,1,0},{0,0,1}},
                            {{1,0,0},{0,0,1},{0,0,1}}};
    uint32_t indices[3] = {0,1,2};
    P2Mesh *mesh = NULL;
    assert(p2_mesh_create(&mesh, vertices, 3, indices, 3)==P2_MESH_OK);
    vertices[0].position[0]=100; indices[0]=2;
    float transformed[12];
    assert(p2_mesh_transform(mesh, product, transformed, 12)==P2_MESH_OK);
    assert(transformed[0]==12 && transformed[1]==26 && transformed[2]==42);
    assert(p2_mesh_vertex_count(mesh)==3 && p2_mesh_index_count(mesh)==3);
    assert(p2_mesh_transform(mesh, product, transformed, 11)==P2_MESH_ARGUMENT);
    p2_mesh_destroy(mesh);
    mesh=NULL; indices[0]=3;
    assert(p2_mesh_create(&mesh, vertices, 3, indices, 3)==P2_MESH_RANGE && !mesh);
    assert(p2_mesh_create(&mesh, vertices, 3, indices, 2)==P2_MESH_ARGUMENT && !mesh);
    assert(p2_mesh_create(&mesh, NULL, 1, NULL, 0)==P2_MESH_ARGUMENT && !mesh);
    assert(p2_mesh_create(&mesh, vertices, SIZE_MAX, NULL, 0)==P2_MESH_RANGE && !mesh);
    assert(p2_mesh_create(&mesh, NULL, 0, NULL, 0)==P2_MESH_OK);
    assert(p2_mesh_transform(mesh, id, NULL, 0)==P2_MESH_OK);
    p2_mesh_destroy(mesh);
    p2_mesh_destroy(NULL);
    puts("P2 graphics CPU reference: OK");
    return 0;
}

/**
 * moo_3d_gl33_mesh.c — VBO/VAO Mesh-System fuer GL 3.3 Backend.
 * Vertex-Buffer-Erstellung, VAO-Setup, Chunk-Caching.
 */

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdlib.h>
#include <string.h>

#include "moo_3d_gl33_mesh.h"

/* ========================================================
 * MeshBuilder — sammelt Vertices waehrend chunk_begin..end
 * ======================================================== */

void mesh_builder_init(MeshBuilder* mb) {
    mb->vertices = (MooVertex*)malloc(MESH_INITIAL_CAP * sizeof(MooVertex));
    mb->count = 0;
    mb->capacity = MESH_INITIAL_CAP;
}

void mesh_builder_reset(MeshBuilder* mb) {
    mb->count = 0;
}

void mesh_builder_free(MeshBuilder* mb) {
    free(mb->vertices);
    mb->vertices = NULL;
    mb->count = 0;
    mb->capacity = 0;
}

static void mesh_builder_grow(MeshBuilder* mb) {
    mb->capacity *= 2;
    mb->vertices = (MooVertex*)realloc(mb->vertices, mb->capacity * sizeof(MooVertex));
}

void mesh_builder_add_vertex(MeshBuilder* mb,
                             float px, float py, float pz,
                             float r, float g, float b,
                             float nx, float ny, float nz) {
    if (mb->count >= mb->capacity) mesh_builder_grow(mb);
    MooVertex* v = &mb->vertices[mb->count++];
    v->px = px; v->py = py; v->pz = pz;
    v->r = r;   v->g = g;   v->b = b;
    v->nx = nx; v->ny = ny; v->nz = nz;
}

/* Add a quad as 2 triangles (6 vertices) */
void mesh_builder_add_quad(MeshBuilder* mb,
                           float x, float y, float z, float size,
                           float r, float g, float b,
                           float nx, float ny, float nz,
                           const float* v0, const float* v1,
                           const float* v2, const float* v3) {
    (void)x; (void)y; (void)z; (void)size;
    /* Triangle 1: v0, v1, v2 */
    mesh_builder_add_vertex(mb, v0[0], v0[1], v0[2], r, g, b, nx, ny, nz);
    mesh_builder_add_vertex(mb, v1[0], v1[1], v1[2], r, g, b, nx, ny, nz);
    mesh_builder_add_vertex(mb, v2[0], v2[1], v2[2], r, g, b, nx, ny, nz);
    /* Triangle 2: v0, v2, v3 */
    mesh_builder_add_vertex(mb, v0[0], v0[1], v0[2], r, g, b, nx, ny, nz);
    mesh_builder_add_vertex(mb, v2[0], v2[1], v2[2], r, g, b, nx, ny, nz);
    mesh_builder_add_vertex(mb, v3[0], v3[1], v3[2], r, g, b, nx, ny, nz);
}

/* Add a full cube as 12 triangles (36 vertices) */
void mesh_builder_add_cube(MeshBuilder* mb,
                           float x, float y, float z, float size,
                           float r, float g, float b) {
    float s = size / 2.0f;

    /* 8 corner positions */
    float corners[8][3] = {
        {x-s, y-s, z+s}, {x+s, y-s, z+s}, {x+s, y+s, z+s}, {x-s, y+s, z+s}, /* front */
        {x+s, y-s, z-s}, {x-s, y-s, z-s}, {x-s, y+s, z-s}, {x+s, y+s, z-s}, /* back */
    };

    /* 6 faces: {normal, v0, v1, v2, v3} indices into corners */
    struct { float nx, ny, nz; int i0, i1, i2, i3; } faces[6] = {
        { 0,  0,  1, 0, 1, 2, 3}, /* front */
        { 0,  0, -1, 4, 5, 6, 7}, /* back */
        { 0,  1,  0, 3, 2, 7, 6}, /* top */
        { 0, -1,  0, 5, 4, 1, 0}, /* bottom */
        { 1,  0,  0, 1, 4, 7, 2}, /* right */
        {-1,  0,  0, 5, 0, 3, 6}, /* left */
    };

    for (int f = 0; f < 6; f++) {
        mesh_builder_add_quad(mb, x, y, z, size, r, g, b,
                              faces[f].nx, faces[f].ny, faces[f].nz,
                              corners[faces[f].i0], corners[faces[f].i1],
                              corners[faces[f].i2], corners[faces[f].i3]);
    }
}

/* ========================================================
 * GL33 Chunk System — VBO/VAO Caching
 * ======================================================== */

void gl33_chunk_system_init(GL33Chunk* chunks) {
    memset(chunks, 0, MAX_GL33_CHUNKS * sizeof(GL33Chunk));
}

int gl33_chunk_alloc(GL33Chunk* chunks) {
    for (int i = 0; i < MAX_GL33_CHUNKS; i++) {
        if (!chunks[i].is_used) {
            chunks[i].is_used = true;
            chunks[i].is_compiled = false;
            chunks[i].vertex_count = 0;
            /* Generate VAO + VBO */
            glGenVertexArrays(1, &chunks[i].vao);
            glGenBuffers(1, &chunks[i].vbo);
            return i;
        }
    }
    return -1; /* no free slot */
}

void gl33_chunk_upload(GL33Chunk* chunks, int id, MeshBuilder* mb) {
    if (id < 0 || id >= MAX_GL33_CHUNKS || !chunks[id].is_used) return;

    GL33Chunk* c = &chunks[id];

    glBindVertexArray(c->vao);
    glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 mb->count * (int)sizeof(MooVertex),
                 mb->vertices,
                 GL_STATIC_DRAW);

    /* Attribute 0: position (vec3) */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          MOO_VERTEX_STRIDE, (void*)(size_t)MOO_VERTEX_POS_OFFSET);

    /* Attribute 1: color (vec3) */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          MOO_VERTEX_STRIDE, (void*)(size_t)MOO_VERTEX_COLOR_OFFSET);

    /* Attribute 2: normal (vec3) */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE,
                          MOO_VERTEX_STRIDE, (void*)(size_t)MOO_VERTEX_NORMAL_OFFSET);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    c->vertex_count = mb->count;
    c->is_compiled = true;
}

void gl33_chunk_draw(GL33Chunk* chunks, int id) {
    if (id < 0 || id >= MAX_GL33_CHUNKS) return;
    GL33Chunk* c = &chunks[id];
    if (!c->is_compiled || c->vertex_count == 0) return;

    glBindVertexArray(c->vao);
    glDrawArrays(GL_TRIANGLES, 0, c->vertex_count);
    glBindVertexArray(0);
}

void gl33_chunk_delete(GL33Chunk* chunks, int id) {
    if (id < 0 || id >= MAX_GL33_CHUNKS) return;
    GL33Chunk* c = &chunks[id];
    if (!c->is_used) return;

    if (c->vbo) glDeleteBuffers(1, &c->vbo);
    if (c->vao) glDeleteVertexArrays(1, &c->vao);
    memset(c, 0, sizeof(GL33Chunk));
}

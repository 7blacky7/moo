/**
 * moo_3d_terrain.c — Smooth Terrain-Mesh-Generator fuer GL 3.3 Backend.
 * Generiert Triangle-Mesh aus Heightmap mit Normalen, Vertex-Farben und Fog.
 */

#include "moo_3d_terrain.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* ============================================================
 * Biom-Farben
 * ============================================================ */

void terrain_biom_color(int biom, float* r, float* g, float* b) {
    switch (biom) {
    case BIOM_OZEAN:  *r = 0.129f; *g = 0.588f; *b = 0.953f; break;
    case BIOM_STRAND: *r = 1.000f; *g = 0.878f; *b = 0.510f; break;
    case BIOM_WIESE:  *r = 0.298f; *g = 0.686f; *b = 0.314f; break;
    case BIOM_WALD:   *r = 0.180f; *g = 0.490f; *b = 0.196f; break;
    case BIOM_BERG:   *r = 0.620f; *g = 0.620f; *b = 0.620f; break;
    case BIOM_SCHNEE: *r = 0.925f; *g = 0.937f; *b = 0.945f; break;
    case BIOM_FLUSS:  *r = 0.129f; *g = 0.588f; *b = 0.953f; break;
    default:          *r = 1.000f; *g = 0.000f; *b = 1.000f; break;
    }
}

void terrain_biom_color_smooth(
    float x, float z,
    TerrainBiomFn biom_fn,
    float* r, float* g, float* b)
{
    if (!biom_fn) {
        *r = 0.4f; *g = 0.7f; *b = 0.3f;
        return;
    }

    /* Sample 5 Punkte: Zentrum + 4 Nachbarn mit Offset 0.5 */
    float tr = 0, tg = 0, tb = 0;
    float offsets[][2] = {
        { 0.0f,  0.0f},
        { 0.5f,  0.0f},
        {-0.5f,  0.0f},
        { 0.0f,  0.5f},
        { 0.0f, -0.5f}
    };
    int n = 5;

    for (int i = 0; i < n; i++) {
        float cr, cg, cb;
        int biom = biom_fn(x + offsets[i][0], z + offsets[i][1]);
        terrain_biom_color(biom, &cr, &cg, &cb);
        /* Zentrum bekommt doppeltes Gewicht */
        float w = (i == 0) ? 2.0f : 1.0f;
        tr += cr * w;
        tg += cg * w;
        tb += cb * w;
    }

    float total = (float)(n + 1); /* 2 + 1 + 1 + 1 + 1 = 6 */
    *r = tr / total;
    *g = tg / total;
    *b = tb / total;
}

/* ============================================================
 * Normalen-Berechnung
 * ============================================================ */

static void calc_normal(float ax, float ay, float az,
                        float bx, float by, float bz,
                        float* nx, float* ny, float* nz) {
    *nx = ay * bz - az * by;
    *ny = az * bx - ax * bz;
    *nz = ax * by - ay * bx;
    float len = sqrtf((*nx)*(*nx) + (*ny)*(*ny) + (*nz)*(*nz));
    if (len > 1e-6f) {
        *nx /= len; *ny /= len; *nz /= len;
    } else {
        *nx = 0; *ny = 1; *nz = 0;
    }
}

/* Berechne Vertex-Normale als Gradient der Heightmap (zentrale Differenz) */
static void height_normal(float x, float z, TerrainHeightFn hfn, float step,
                          float* nx, float* ny, float* nz) {
    float hL = hfn(x - step, z);
    float hR = hfn(x + step, z);
    float hD = hfn(x, z - step);
    float hU = hfn(x, z + step);

    /* Gradient → Normale: n = normalize(-dh/dx, 2*step, -dh/dz) */
    float gx = hL - hR;
    float gz = hD - hU;
    float gy = 2.0f * step;

    float len = sqrtf(gx*gx + gy*gy + gz*gz);
    if (len > 1e-6f) {
        *nx = gx / len;
        *ny = gy / len;
        *nz = gz / len;
    } else {
        *nx = 0; *ny = 1; *nz = 0;
    }
}

/* ============================================================
 * Mesh-Generierung: Terrain
 * ============================================================ */

void terrain_mesh_generate(
    float chunk_x, float chunk_z,
    int chunk_size, int resolution,
    TerrainHeightFn height_fn,
    TerrainBiomFn biom_fn,
    TerrainMesh* out)
{
    if (!out || !height_fn || resolution < 2) {
        if (out) { out->vertices = NULL; out->vertex_count = 0; out->indices = NULL; out->index_count = 0; }
        return;
    }

    int verts_per_side = resolution;
    int total_verts = verts_per_side * verts_per_side;
    int quads = (verts_per_side - 1) * (verts_per_side - 1);
    int total_indices = quads * 6; /* 2 Dreiecke pro Quad */

    TerrainVertex* verts = (TerrainVertex*)malloc(sizeof(TerrainVertex) * total_verts);
    uint32_t* indices = (uint32_t*)malloc(sizeof(uint32_t) * total_indices);
    if (!verts || !indices) {
        free(verts); free(indices);
        out->vertices = NULL; out->vertex_count = 0;
        out->indices = NULL; out->index_count = 0;
        return;
    }

    float step = (float)chunk_size / (float)(verts_per_side - 1);
    float normal_step = step * 0.5f;

    /* Vertices generieren */
    for (int iz = 0; iz < verts_per_side; iz++) {
        for (int ix = 0; ix < verts_per_side; ix++) {
            int idx = iz * verts_per_side + ix;
            float wx = chunk_x + ix * step;
            float wz = chunk_z + iz * step;
            float wy = height_fn(wx, wz);

            verts[idx].x = wx;
            verts[idx].y = wy;
            verts[idx].z = wz;

            /* Normale aus Heightmap-Gradient */
            height_normal(wx, wz, height_fn, normal_step,
                         &verts[idx].nx, &verts[idx].ny, &verts[idx].nz);

            /* Farbe: Smooth Biom-Interpolation */
            terrain_biom_color_smooth(wx, wz, biom_fn,
                                      &verts[idx].r, &verts[idx].g, &verts[idx].b);
        }
    }

    /* Indices: 2 Dreiecke pro Quad */
    int ii = 0;
    for (int iz = 0; iz < verts_per_side - 1; iz++) {
        for (int ix = 0; ix < verts_per_side - 1; ix++) {
            uint32_t tl = iz * verts_per_side + ix;
            uint32_t tr = tl + 1;
            uint32_t bl = (iz + 1) * verts_per_side + ix;
            uint32_t br = bl + 1;

            /* Dreieck 1: tl → bl → tr */
            indices[ii++] = tl;
            indices[ii++] = bl;
            indices[ii++] = tr;

            /* Dreieck 2: tr → bl → br */
            indices[ii++] = tr;
            indices[ii++] = bl;
            indices[ii++] = br;
        }
    }

    out->vertices = verts;
    out->vertex_count = total_verts;
    out->indices = indices;
    out->index_count = total_indices;
}

/* ============================================================
 * Mesh-Generierung: Wasser
 * ============================================================ */

void terrain_mesh_water(
    float chunk_x, float chunk_z,
    int chunk_size, int resolution,
    float water_level,
    TerrainMesh* out)
{
    if (!out || resolution < 2) {
        if (out) { out->vertices = NULL; out->vertex_count = 0; out->indices = NULL; out->index_count = 0; }
        return;
    }

    int verts_per_side = resolution;
    int total_verts = verts_per_side * verts_per_side;
    int quads = (verts_per_side - 1) * (verts_per_side - 1);
    int total_indices = quads * 6;

    TerrainVertex* verts = (TerrainVertex*)malloc(sizeof(TerrainVertex) * total_verts);
    uint32_t* indices = (uint32_t*)malloc(sizeof(uint32_t) * total_indices);
    if (!verts || !indices) {
        free(verts); free(indices);
        out->vertices = NULL; out->vertex_count = 0;
        out->indices = NULL; out->index_count = 0;
        return;
    }

    float step = (float)chunk_size / (float)(verts_per_side - 1);

    /* Wasser-Farbe: Halbtransparent-blau */
    float wr = 0.129f, wg = 0.588f, wb = 0.953f;

    for (int iz = 0; iz < verts_per_side; iz++) {
        for (int ix = 0; ix < verts_per_side; ix++) {
            int idx = iz * verts_per_side + ix;
            verts[idx].x = chunk_x + ix * step;
            verts[idx].y = water_level;
            verts[idx].z = chunk_z + iz * step;
            verts[idx].r = wr;
            verts[idx].g = wg;
            verts[idx].b = wb;
            verts[idx].nx = 0.0f;
            verts[idx].ny = 1.0f;
            verts[idx].nz = 0.0f;
        }
    }

    int ii = 0;
    for (int iz = 0; iz < verts_per_side - 1; iz++) {
        for (int ix = 0; ix < verts_per_side - 1; ix++) {
            uint32_t tl = iz * verts_per_side + ix;
            uint32_t tr = tl + 1;
            uint32_t bl = (iz + 1) * verts_per_side + ix;
            uint32_t br = bl + 1;
            indices[ii++] = tl;
            indices[ii++] = bl;
            indices[ii++] = tr;
            indices[ii++] = tr;
            indices[ii++] = bl;
            indices[ii++] = br;
        }
    }

    out->vertices = verts;
    out->vertex_count = total_verts;
    out->indices = indices;
    out->index_count = total_indices;
}

/* ============================================================
 * Cleanup
 * ============================================================ */

void terrain_mesh_free(TerrainMesh* mesh) {
    if (!mesh) return;
    free(mesh->vertices);
    free(mesh->indices);
    mesh->vertices = NULL;
    mesh->indices = NULL;
    mesh->vertex_count = 0;
    mesh->index_count = 0;
}

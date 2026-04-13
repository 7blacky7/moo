/**
 * moo_world.c — Prozedurale Welt-Engine fuer moo.
 * Noise, Terrain, Chunks, Spieler-Physik, Rendering in einer C-Runtime.
 * Verfuegbar ueber: importiere welt → stdlib/welt.moo → Builtins
 */

#include "moo_runtime.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ========================================================
 * Vorwaertsdeklarationen (moo_3d + moo_stdlib)
 * ======================================================== */
extern MooValue moo_number(double v);
extern MooValue moo_bool(bool b);
extern MooValue moo_none(void);
extern MooValue moo_string_new(const char* s);
extern MooValue moo_3d_create(MooValue title, MooValue w, MooValue h);
extern MooValue moo_3d_is_open(MooValue win);
extern void moo_3d_clear(MooValue win, MooValue r, MooValue g, MooValue b);
extern void moo_3d_update(MooValue win);
extern void moo_3d_close(MooValue win);
extern void moo_3d_perspective(MooValue win, MooValue fov, MooValue near, MooValue far);
extern void moo_3d_camera(MooValue win, MooValue ex, MooValue ey, MooValue ez,
                          MooValue lx, MooValue ly, MooValue lz);
extern void moo_3d_triangle(MooValue win, MooValue x1, MooValue y1, MooValue z1,
                            MooValue x2, MooValue y2, MooValue z2,
                            MooValue x3, MooValue y3, MooValue z3, MooValue color);
extern void moo_3d_cube(MooValue win, MooValue x, MooValue y, MooValue z,
                        MooValue size, MooValue color);
extern void moo_3d_sphere(MooValue win, MooValue x, MooValue y, MooValue z,
                          MooValue radius, MooValue color, MooValue detail);
extern MooValue moo_3d_key_pressed(MooValue win, MooValue key);
extern MooValue moo_3d_chunk_create(void);
extern void moo_3d_chunk_begin(MooValue id);
extern void moo_3d_chunk_end(void);
extern void moo_3d_chunk_draw(MooValue id);
extern void moo_3d_chunk_delete(MooValue id);
extern void moo_3d_capture_mouse(MooValue win);
extern MooValue moo_3d_mouse_dx(MooValue win);
extern MooValue moo_3d_mouse_dy(MooValue win);
extern double moo_time_ms(void);

/* ========================================================
 * Noise (Perlin + fBm, rein in C)
 * ======================================================== */

static int world_noise_seed = 0;

static int hash_2d(int ix, int iy) {
    int n = ((ix + 1) * 374761 + (iy + 1) * 668265 + world_noise_seed) & 0x7FFFFFFF;
    n = (n ^ (n >> 11)) & 0x7FFFFFFF;
    n = (n * 45673) & 0x7FFFFFFF;
    n = (n ^ (n >> 15)) & 0x7FFFFFFF;
    n = (n * 31337) & 0x7FFFFFFF;
    n = (n ^ (n >> 13)) & 0x7FFFFFFF;
    return n;
}

static float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float lerp_f(float a, float b, float t) {
    return a + (b - a) * t;
}

static float perlin_2d(float x, float y) {
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);
    float fx = x - ix;
    float fy = y - iy;
    float u = fade(fx);
    float v = fade(fy);
    float a = (hash_2d(ix,     iy    ) % 1000) / 500.0f - 1.0f;
    float b = (hash_2d(ix + 1, iy    ) % 1000) / 500.0f - 1.0f;
    float c = (hash_2d(ix,     iy + 1) % 1000) / 500.0f - 1.0f;
    float d = (hash_2d(ix + 1, iy + 1) % 1000) / 500.0f - 1.0f;
    return lerp_f(lerp_f(a, b, u), lerp_f(c, d, u), v);
}

static float fbm(float x, float y, int oktaven, float freq, float amp) {
    float value = 0.0f, max_val = 0.0f;
    float a = 1.0f, f = freq;
    for (int i = 0; i < oktaven; i++) {
        value += perlin_2d(x * f, y * f) * a;
        max_val += a;
        a *= 0.5f;
        f *= 2.0f;
    }
    return (value / max_val) * amp;
}

/* ========================================================
 * Biom-System
 * ======================================================== */

#define MAX_BIOME 16
typedef struct {
    char name[32];
    float h_min, h_max;
    float r, g, b;
    int tree_chance; /* 0-100, 0 = keine Baeume */
} Biom;

/* ========================================================
 * MooWorld Struktur
 * ======================================================== */

#define CHUNK_SIZE 16
#define MAX_CHUNKS 256

typedef struct {
    int cx, cz;
    MooValue chunk_id;
    bool used;
} ChunkEntry;

typedef struct {
    MooValue win;
    /* Noise */
    int seed;
    /* Terrain */
    float sea_level;
    float base_freq, base_amp;
    int base_okt;
    float detail_freq, detail_amp;
    int detail_okt;
    float berg_freq, berg_amp;
    int berg_okt;
    float berg_threshold;
    float height_max;
    /* Biome */
    Biom biome[MAX_BIOME];
    int biom_count;
    /* Chunks */
    ChunkEntry chunks[MAX_CHUNKS];
    int render_dist;
    /* Spieler */
    float px, py, pz;
    float vy;
    float yaw, pitch;
    float speed, gravity, jump_force;
    bool on_ground;
    float mouse_sens;
    /* State */
    bool initialized;
} MooWorld;

static MooWorld g_world = {0};

/* ========================================================
 * Terrain-Hoehe (C-nativ, kein MooValue-Overhead)
 * ======================================================== */

static float world_terrain_height(float wx, float wz) {
    float kontinent = fbm(wx, wz, g_world.base_okt, g_world.base_freq, g_world.base_amp);
    float detail = fbm(wx, wz, g_world.detail_okt, g_world.detail_freq, g_world.detail_amp);
    float berg = fbm(wx, wz, g_world.berg_okt, g_world.berg_freq, 1.0f);
    float berg_faktor = 0.0f;
    if (berg > g_world.berg_threshold)
        berg_faktor = (berg - g_world.berg_threshold) * g_world.berg_amp;
    /* Offset hebt Terrain-Durchschnitt auf ~30 (weit ueber sea_level) */
    float h = kontinent + detail + berg_faktor + 30.0f;
    if (h < 0) h = 0;
    if (h > g_world.height_max) h = g_world.height_max;
    return floorf(h);
}

/* Biom-Bestimmung */
static int world_get_biom(float wx, float wz, float h) {
    for (int i = 0; i < g_world.biom_count; i++) {
        if (h >= g_world.biome[i].h_min && h < g_world.biome[i].h_max)
            return i;
    }
    return 0; /* Fallback: erstes Biom */
}

/* Hex-Farbe aus Biom-RGB bauen */
static void biom_to_hex(int idx, char* buf) {
    Biom* b = &g_world.biome[idx];
    int ri = (int)(b->r * 255);
    int gi = (int)(b->g * 255);
    int bi = (int)(b->b * 255);
    snprintf(buf, 8, "#%02X%02X%02X", ri, gi, bi);
}

/* ========================================================
 * Chunk-Bau (komplett in C — kein MooValue pro Vertex)
 * ======================================================== */

static void world_build_chunk(int cx, int cz, MooValue chunk_id) {
    moo_3d_chunk_begin(chunk_id);
    float ox = (float)(cx * CHUNK_SIZE);
    float oz = (float)(cz * CHUNK_SIZE);

    /* Terrain-Dreiecke */
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            float wx = ox + lx;
            float wz = oz + lz;
            float h00 = world_terrain_height(wx, wz);
            float h10 = world_terrain_height(wx + 1, wz);
            float h01 = world_terrain_height(wx, wz + 1);
            float h11 = world_terrain_height(wx + 1, wz + 1);
            float h_avg = (h00 + h10 + h01 + h11) * 0.25f;
            int biom = world_get_biom(wx, wz, h_avg);
            char hex[8];
            biom_to_hex(biom, hex);
            MooValue farbe = moo_string_new(hex);
            /* Dreieck 1 (CCW von oben) */
            moo_3d_triangle(g_world.win,
                moo_number(wx), moo_number(h00), moo_number(wz),
                moo_number(wx), moo_number(h01), moo_number(wz+1),
                moo_number(wx+1), moo_number(h10), moo_number(wz),
                farbe);
            /* Dreieck 2 (CCW von oben) */
            moo_3d_triangle(g_world.win,
                moo_number(wx+1), moo_number(h10), moo_number(wz),
                moo_number(wx), moo_number(h01), moo_number(wz+1),
                moo_number(wx+1), moo_number(h11), moo_number(wz+1),
                farbe);
        }
    }

    /* Wasser */
    float sl = g_world.sea_level;
    MooValue wasser_farbe = moo_string_new("#2196F3");
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            float wx = ox + lx;
            float wz = oz + lz;
            if (world_terrain_height(wx, wz) < sl) {
                moo_3d_triangle(g_world.win,
                    moo_number(wx), moo_number(sl), moo_number(wz),
                    moo_number(wx), moo_number(sl), moo_number(wz+1),
                    moo_number(wx+1), moo_number(sl), moo_number(wz),
                    wasser_farbe);
                moo_3d_triangle(g_world.win,
                    moo_number(wx+1), moo_number(sl), moo_number(wz),
                    moo_number(wx), moo_number(sl), moo_number(wz+1),
                    moo_number(wx+1), moo_number(sl), moo_number(wz+1),
                    wasser_farbe);
            }
        }
    }

    /* Baeume */
    MooValue holz = moo_string_new("#5D4037");
    MooValue blaetter = moo_string_new("#2E7D32");
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            float wx = ox + lx;
            float wz = oz + lz;
            float h = world_terrain_height(wx, wz);
            if (h <= sl) continue;
            int biom = world_get_biom(wx, wz, h);
            if (g_world.biome[biom].tree_chance <= 0) continue;
            int rng = hash_2d((int)wx * 7 + 1000, (int)wz * 13 + 2000);
            if ((rng % 100) >= g_world.biome[biom].tree_chance) continue;
            int stamm = 3 + (hash_2d((int)wx * 3 + 500, (int)wz * 5 + 700) % 3);
            for (int sy = 1; sy <= stamm; sy++)
                moo_3d_cube(g_world.win,
                    moo_number(wx), moo_number(h + sy), moo_number(wz),
                    moo_number(0.4), holz);
            float kr = 1.2f + (hash_2d((int)wx * 11, (int)wz * 17) % 5) * 0.15f;
            moo_3d_sphere(g_world.win,
                moo_number(wx), moo_number(h + stamm + 1.5), moo_number(wz),
                moo_number(kr), blaetter, moo_number(6));
        }
    }

    moo_3d_chunk_end();
}

/* ========================================================
 * Chunk-Management
 * ======================================================== */

static int find_chunk(int cx, int cz) {
    for (int i = 0; i < MAX_CHUNKS; i++)
        if (g_world.chunks[i].used && g_world.chunks[i].cx == cx && g_world.chunks[i].cz == cz)
            return i;
    return -1;
}

static int alloc_chunk_slot(void) {
    for (int i = 0; i < MAX_CHUNKS; i++)
        if (!g_world.chunks[i].used) return i;
    return -1;
}

/* ========================================================
 * Public API (aufgerufen von moo via Builtins)
 * ======================================================== */

MooValue moo_world_create(MooValue title, MooValue w, MooValue h) {
    memset(&g_world, 0, sizeof(g_world));

    g_world.win = moo_3d_create(title, w, h);
    moo_3d_perspective(g_world.win, moo_number(70.0), moo_number(0.1), moo_number(250.0));
    moo_3d_capture_mouse(g_world.win);

    /* Noise Defaults */
    g_world.seed = 42;
    world_noise_seed = 42;
    g_world.base_freq = 0.005f; g_world.base_okt = 5; g_world.base_amp = 60.0f;
    g_world.detail_freq = 0.03f; g_world.detail_okt = 3; g_world.detail_amp = 18.0f;
    g_world.berg_freq = 0.015f; g_world.berg_okt = 4; g_world.berg_amp = 60.0f;
    g_world.berg_threshold = 0.2f;
    g_world.height_max = 120.0f;
    g_world.sea_level = 8.0f;

    /* Default Biome */
    #define ADD_BIOM(n, hmin, hmax, cr, cg, cb, tc) do { \
        Biom* _b = &g_world.biome[g_world.biom_count++]; \
        strncpy(_b->name, n, 31); \
        _b->h_min = hmin; _b->h_max = hmax; \
        _b->r = cr; _b->g = cg; _b->b = cb; _b->tree_chance = tc; \
    } while(0)
    ADD_BIOM("ozean",  0, 12, 0.13f, 0.59f, 0.95f, 0);
    ADD_BIOM("strand", 12, 14, 1.0f, 0.88f, 0.51f, 0);
    ADD_BIOM("wiese",  14, 30, 0.30f, 0.69f, 0.31f, 4);
    ADD_BIOM("wald",   14, 30, 0.18f, 0.49f, 0.20f, 15);
    ADD_BIOM("berg",   50, 70, 0.62f, 0.62f, 0.62f, 0);
    ADD_BIOM("schnee", 70, 999, 0.93f, 0.94f, 0.95f, 0);
    #undef ADD_BIOM

    /* Spieler */
    g_world.px = 0; g_world.pz = 0;
    g_world.py = world_terrain_height(0, 0) + 3.0f;
    g_world.speed = 0.15f;
    g_world.gravity = 0.01f;
    g_world.jump_force = 0.2f;
    g_world.mouse_sens = 0.003f;
    g_world.render_dist = 4;
    g_world.initialized = true;

    return g_world.win;
}

MooValue moo_world_is_open(MooValue win) {
    (void)win;
    if (!g_world.initialized) return moo_bool(false);
    return moo_3d_is_open(g_world.win);
}

void moo_world_seed(MooValue win, MooValue seed) {
    (void)win;
    int s = (int)MV_NUM(seed);
    g_world.seed = s;
    int x = s ^ (s << 13);
    x = x ^ (x >> 7);
    x = x ^ (x << 17);
    world_noise_seed = abs(x) & 0x7FFFFFFF;
}

void moo_world_biome(MooValue win, MooValue name, MooValue h_min, MooValue h_max,
                     MooValue color, MooValue trees) {
    (void)win;
    if (g_world.biom_count >= MAX_BIOME) return;
    Biom* b = &g_world.biome[g_world.biom_count++];
    const char* n = MV_STR(name)->chars;
    strncpy(b->name, n, 31);
    b->h_min = (float)MV_NUM(h_min);
    b->h_max = (float)MV_NUM(h_max);
    b->tree_chance = (int)MV_NUM(trees);
    const char* c = MV_STR(color)->chars;
    if (c[0] == '#' && strlen(c) >= 7) {
        unsigned int hex;
        sscanf(c + 1, "%06x", &hex);
        b->r = ((hex >> 16) & 0xFF) / 255.0f;
        b->g = ((hex >> 8) & 0xFF) / 255.0f;
        b->b = (hex & 0xFF) / 255.0f;
    }
}

void moo_world_trees(MooValue win, MooValue biom_name, MooValue chance) {
    (void)win;
    const char* n = MV_STR(biom_name)->chars;
    for (int i = 0; i < g_world.biom_count; i++) {
        if (strcmp(g_world.biome[i].name, n) == 0) {
            g_world.biome[i].tree_chance = (int)MV_NUM(chance);
            return;
        }
    }
}

void moo_world_sun(MooValue win, MooValue x, MooValue y, MooValue z) {
    (void)win;
    /* Sonnen-Position wird in update() relativ zum Spieler gesetzt */
    /* TODO: Custom-Position wenn gewuenscht */
    (void)x; (void)y; (void)z;
}

void moo_world_fog(MooValue win, MooValue dist) {
    (void)win;
    /* fogDist wird vom Backend-UBO gesteuert — hier nur merken */
    (void)dist;
}

void moo_world_sea_level(MooValue win, MooValue level) {
    (void)win;
    g_world.sea_level = (float)MV_NUM(level);
}

void moo_world_render_dist(MooValue win, MooValue dist) {
    (void)win;
    g_world.render_dist = (int)MV_NUM(dist);
    if (g_world.render_dist < 1) g_world.render_dist = 1;
    if (g_world.render_dist > 8) g_world.render_dist = 8;
}

void moo_world_update(MooValue win) {
    (void)win;
    if (!g_world.initialized) return;
    MooValue w = g_world.win;

    /* === Input === */
    MooValue key_w = moo_string_new("w");
    MooValue key_s = moo_string_new("s");
    MooValue key_a = moo_string_new("a");
    MooValue key_d = moo_string_new("d");
    MooValue key_space = moo_string_new("leertaste");
    MooValue key_esc = moo_string_new("escape");

    if (MV_NUM(moo_3d_key_pressed(w, key_w))) {
        g_world.px += sinf(g_world.yaw) * g_world.speed;
        g_world.pz += cosf(g_world.yaw) * g_world.speed;
    }
    if (MV_NUM(moo_3d_key_pressed(w, key_s))) {
        g_world.px -= sinf(g_world.yaw) * g_world.speed;
        g_world.pz -= cosf(g_world.yaw) * g_world.speed;
    }
    if (MV_NUM(moo_3d_key_pressed(w, key_a))) {
        g_world.px -= cosf(g_world.yaw) * g_world.speed;
        g_world.pz += sinf(g_world.yaw) * g_world.speed;
    }
    if (MV_NUM(moo_3d_key_pressed(w, key_d))) {
        g_world.px += cosf(g_world.yaw) * g_world.speed;
        g_world.pz -= sinf(g_world.yaw) * g_world.speed;
    }

    /* Maus */
    float mdx = (float)MV_NUM(moo_3d_mouse_dx(w));
    float mdy = (float)MV_NUM(moo_3d_mouse_dy(w));
    g_world.yaw -= mdx * g_world.mouse_sens;
    g_world.pitch -= mdy * g_world.mouse_sens;
    if (g_world.pitch > 1.4f) g_world.pitch = 1.4f;
    if (g_world.pitch < -1.4f) g_world.pitch = -1.4f;

    /* Springen */
    if (MV_NUM(moo_3d_key_pressed(w, key_space)) && g_world.on_ground)
        g_world.vy = g_world.jump_force;

    /* Physik */
    g_world.vy -= g_world.gravity;
    g_world.py += g_world.vy;
    float ground = world_terrain_height(floorf(g_world.px), floorf(g_world.pz)) + 1.8f;
    if (g_world.py < ground) {
        g_world.py = ground;
        g_world.vy = 0;
        g_world.on_ground = true;
    } else {
        g_world.on_ground = false;
    }

    /* === Kamera === */
    float look_x = g_world.px + sinf(g_world.yaw) * cosf(g_world.pitch) * 5.0f;
    float look_y = g_world.py + sinf(g_world.pitch) * 5.0f;
    float look_z = g_world.pz + cosf(g_world.yaw) * cosf(g_world.pitch) * 5.0f;

    moo_3d_clear(w, moo_number(0.53), moo_number(0.81), moo_number(0.92));
    moo_3d_camera(w,
        moo_number(g_world.px), moo_number(g_world.py), moo_number(g_world.pz),
        moo_number(look_x), moo_number(look_y), moo_number(look_z));

    /* === Chunk-Management === */
    int pcx = (int)floorf(g_world.px / CHUNK_SIZE);
    int pcz = (int)floorf(g_world.pz / CHUNK_SIZE);
    int built = 0;
    int rd = g_world.render_dist;

    for (int dx = -rd; dx <= rd; dx++) {
        for (int dz = -rd; dz <= rd; dz++) {
            int cx = pcx + dx;
            int cz = pcz + dz;
            int idx = find_chunk(cx, cz);
            if (idx < 0 && built < 1) {
                /* Chunk bauen */
                int slot = alloc_chunk_slot();
                if (slot >= 0) {
                    MooValue cid = moo_3d_chunk_create();
                    world_build_chunk(cx, cz, cid);
                    g_world.chunks[slot].cx = cx;
                    g_world.chunks[slot].cz = cz;
                    g_world.chunks[slot].chunk_id = cid;
                    g_world.chunks[slot].used = true;
                    idx = slot;
                    built++;
                }
            }
            if (idx >= 0) {
                moo_3d_chunk_draw(g_world.chunks[idx].chunk_id);
            }
        }
    }

    /* Sonne */
    moo_3d_sphere(w,
        moo_number(g_world.px + 700), moo_number(g_world.py + 400),
        moo_number(g_world.pz + 500),
        moo_number(20), moo_string_new("#FFD700"), moo_number(8));

    moo_3d_update(w);
}

void moo_world_close(MooValue win) {
    (void)win;
    if (!g_world.initialized) return;
    /* Alle Chunks loeschen */
    for (int i = 0; i < MAX_CHUNKS; i++) {
        if (g_world.chunks[i].used)
            moo_3d_chunk_delete(g_world.chunks[i].chunk_id);
    }
    moo_3d_close(g_world.win);
    g_world.initialized = false;
}

MooValue moo_world_height_at(MooValue win, MooValue x, MooValue z) {
    (void)win;
    return moo_number(world_terrain_height((float)MV_NUM(x), (float)MV_NUM(z)));
}

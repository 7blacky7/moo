/**
 * bench_voxel_harness.h - Gemeinsame Host-Stubs fuer die Voxel-Benchmarks
 * (Plan-006 P006-R0, Agent p006-r0).
 *
 * ZWECK: bench_voxel.c (Perf-Timings) und bench_voxel_ram.c (RAM-Kategorien)
 * teilen sich dieselbe headless Host-Umgebung (moo_alloc, MooValue-Ctors,
 * Dict-Stub mit echtem Key/Value-Store, headless 3D-API, Peak-RSS, Timer).
 * Statt diese ~100 Zeilen zu duplizieren, leben sie hier zentral
 * (PERF1-Erweiterungsgedanke: bestehende Bench-Infrastruktur erweitern,
 * nicht kopieren).
 *
 * Genau EINE Uebersetzungseinheit pro Bench definiert die Stub-Bodies, indem
 * sie VOR dem Include BENCH_HARNESS_IMPL definiert. Header-only sonst (nur
 * Prototypen + externe Voxel-Deklarationen), damit mehrere Benches potenziell
 * zusammengelinkt werden koennten ohne Mehrfach-Definition.
 *
 * Build (Beispiel RAM-Bench):
 *   gcc -O2 -g -std=c11 -I.. bench_voxel_ram.c ../moo_voxel.c ../moo_noise.c \
 *       -lm -lpthread -o /tmp/bench_voxel_ram
 *   /usr/bin/time -v /tmp/bench_voxel_ram   (oder /proc/self/status VmHWM intern)
 */
#ifndef BENCH_VOXEL_HARNESS_H
#define BENCH_VOXEL_HARNESS_H

#define _POSIX_C_SOURCE 200809L  /* CLOCK_MONOTONIC unter -std=c11 */
#include "moo_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* ---- Host-Hooks (in der IMPL-TU definiert) ---- */
long     peak_rss_kb(void);
double   now_ms(void);
double   dict_get(const char* key);   /* letzter via moo_dict_set gespeicherter Wert */
void     dict_reset(void);            /* Dict-Store leeren (vor jedem Stat-Read) */

/* ---- Voxel-Runtime (aus moo_voxel.c, headless gelinkt) ---- */
extern MooValue moo_voxel_welt_neu(MooValue seed);
extern MooValue moo_voxel_generieren(MooValue welt, MooValue cx, MooValue cz);
extern MooValue moo_voxel_setzen(MooValue welt, MooValue x, MooValue y, MooValue z, MooValue id);
extern MooValue moo_voxel_holen(MooValue welt, MooValue x, MooValue y, MooValue z);
extern MooValue moo_voxel_mesh_bauen(MooValue welt, MooValue x, MooValue y, MooValue z);
extern MooValue moo_voxel_aktualisieren(MooValue welt);
extern MooValue moo_voxel_ram_statistik(MooValue welt);
extern MooValue moo_voxel_strahl(MooValue welt, MooValue ox, MooValue oy, MooValue oz,
                                 MooValue dx, MooValue dy, MooValue dz, MooValue max_dist);
extern void     moo_voxel_free(void* ptr);

/* Bequemer Zahlen-Konstruktor. */
static inline MooValue N(double d) { MooValue v; v.tag = MOO_NUMBER; moo_val_set_double(&v, d); return v; }

#ifdef BENCH_HARNESS_IMPL
/* ===================================================================== *
 *  Stub-Bodies — genau einmal pro Programm (BENCH_HARNESS_IMPL gesetzt). *
 * ===================================================================== */

/* Peak-RSS (VmHWM) aus /proc/self/status — portabler Ersatz fuer
 * /usr/bin/time -v. Liefert KB, -1 bei Fehler. */
long peak_rss_kb(void) {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256]; long kb = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmHWM:", 6) == 0) { sscanf(line + 6, "%ld", &kb); break; }
    }
    fclose(f);
    return kb;
}

double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

void* moo_alloc(size_t size)            { return malloc(size); }
void* moo_realloc(void* p, size_t size) { return realloc(p, size); }
void  moo_free(void* ptr)               { free(ptr); }

MooValue moo_number(double n) { MooValue v; v.tag = MOO_NUMBER; moo_val_set_double(&v, n); return v; }
MooValue moo_bool(bool b)     { MooValue v; v.tag = MOO_BOOL;   v.data = (uint64_t)b; return v; }
MooValue moo_none(void)       { MooValue v; v.tag = MOO_NONE;   v.data = 0; return v; }
double   moo_as_number(MooValue v) { return moo_val_as_double(v); }

MooValue moo_string_new(const char* s) {
    MooString* str = (MooString*)malloc(sizeof(MooString));
    str->refcount = 1; str->length = (int32_t)strlen(s);
    str->capacity = str->length + 1; str->chars = (char*)malloc((size_t)str->capacity);
    memcpy(str->chars, s, (size_t)str->capacity);
    MooValue v; v.tag = MOO_STRING; moo_val_set_ptr(&v, str); return v;
}
MooValue moo_error(const char* msg) { return moo_string_new(msg); }
void moo_throw(MooValue v) { (void)v; fprintf(stderr, "moo_throw im Bench!\n"); abort(); }

/* Dict-Stub mit echtem Key/Value-Store, damit ram_statistik/strahl auslesbar
 * sind. moo_dict_new() leert den Store (kompatibel zum alten bench_voxel.c). */
#define BENCH_MAX_DICT 64
static char    g_dk[BENCH_MAX_DICT][32];
static double  g_dv[BENCH_MAX_DICT];
static int     g_dn = 0;
void dict_reset(void) { g_dn = 0; }
MooValue moo_dict_new(void) { g_dn = 0; MooValue v; v.tag = MOO_DICT; v.data = 0; return v; }
void moo_dict_set(MooValue d, MooValue k, MooValue val) {
    (void)d;
    MooString* ks = (MooString*)moo_val_as_ptr(k);
    if (g_dn < BENCH_MAX_DICT) {
        snprintf(g_dk[g_dn], sizeof(g_dk[0]), "%s", ks->chars);
        /* moo_bool legt den Wert in .data ab (kein double-Encoding) -> tag-bewusst lesen. */
        g_dv[g_dn] = (val.tag == MOO_BOOL) ? (double)val.data : moo_as_number(val);
        g_dn++;
    }
    free(ks->chars); free(ks);  /* Key-String wieder freigeben (kein Leak im Bench). */
}
double dict_get(const char* key) {
    for (int i = 0; i < g_dn; i++) if (strcmp(g_dk[i], key) == 0) return g_dv[i];
    return -1.0;
}

/* headless 3D-API (kein GPU). */
static int g_backend_active = 1;
static int g_next_render_id = 0;
int moo_3d_backend_active(void) { return g_backend_active; }
MooValue moo_3d_chunk_create(void) { return moo_number((double)(g_next_render_id++)); }
void moo_3d_chunk_begin(MooValue id) { (void)id; }
void moo_3d_chunk_end(void) {}
void moo_3d_chunk_delete(MooValue id) { (void)id; }
void moo_3d_triangle(MooValue win, MooValue x1, MooValue y1, MooValue z1,
                     MooValue x2, MooValue y2, MooValue z2,
                     MooValue x3, MooValue y3, MooValue z3, MooValue color) {
    (void)win;(void)x1;(void)y1;(void)z1;(void)x2;(void)y2;(void)z2;(void)x3;(void)y3;(void)z3;
    MooString* s = (MooString*)moo_val_as_ptr(color);
    if (s) { free(s->chars); free(s); }  /* Farb-String sofort freigeben (kein Leak). */
}

#endif /* BENCH_HARNESS_IMPL */
#endif /* BENCH_VOXEL_HARNESS_H */

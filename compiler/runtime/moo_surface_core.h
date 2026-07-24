#ifndef MOO_SURFACE_CORE_H
#define MOO_SURFACE_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MOO_SURFACE_CLIP_CAPACITY 32u

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} MooSurfaceColor;

typedef struct {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
} MooSurfaceClip;

typedef struct {
    uint8_t* pixels;
    size_t buffer_bytes;
    size_t stride;
    int32_t width;
    int32_t height;
    uint32_t clip_depth;
    MooSurfaceClip clips[MOO_SURFACE_CLIP_CAPACITY];
} MooSurfaceCore;

/* RGBA8888 straight-alpha, top-left. The caller owns pixels. */
bool moo_surface_core_init(MooSurfaceCore* core, uint8_t* pixels,
                           size_t buffer_bytes, int32_t width, int32_t height,
                           size_t stride);
bool moo_surface_core_valid(const MooSurfaceCore* core);

/* Clear overwrites the complete surface and intentionally ignores the clip. */
bool moo_surface_core_clear(MooSurfaceCore* core, MooSurfaceColor color);

/* Half-open clip rectangles. Empty intersections are valid stack entries. */
bool moo_surface_core_clip_push(MooSurfaceCore* core, int32_t x, int32_t y,
                                int32_t width, int32_t height);
bool moo_surface_core_clip_pop(MooSurfaceCore* core);

/* Filled-only deterministic primitives; callers compose outlines from these. */
bool moo_surface_core_rect(MooSurfaceCore* core, int32_t x, int32_t y,
                           int32_t width, int32_t height,
                           MooSurfaceColor color);
bool moo_surface_core_roundrect(MooSurfaceCore* core, int32_t x, int32_t y,
                                int32_t width, int32_t height, int32_t radius,
                                MooSurfaceColor color);

/* Eine Roundrect-Blend-Stufe fuer moo_surface_core_roundrect_stapel. */
typedef struct MooSurfaceStufe {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t radius;
    MooSurfaceColor color;
} MooSurfaceStufe;

/* N Roundrect-Blends in EINEM Pixel-Pass (NATIVE-UI-1c): semantisch
 * identisch zu n aufeinanderfolgenden moo_surface_core_roundrect-Aufrufen
 * in Listen-Reihenfolge (gleiche Blend-Reihenfolge, gleicher Clip,
 * gleiche round_inside-Geometrie); Stufen, die ein Einzel-Aufruf ablehnen
 * wuerde (Radius zu gross, ausserhalb), werden uebersprungen. Spart bei
 * gestapelten Schatten die wiederholten Vollflaechen-Durchlaeufe. */
bool moo_surface_core_roundrect_stapel(MooSurfaceCore* core,
                                       const MooSurfaceStufe* stufen,
                                       int32_t anzahl);

/* NATIVE-UI-1c: Zeilen-parallele Ausfuehrung (Linux: pthreads, sonst
 * sequenziell). Partitionen sind schreibdisjunkt -> bit-identisch zur
 * sequenziellen Reihenfolge. MOO_SURFACE_THREADS steuert die Threadzahl. */
int moo_surface_threadzahl(void);
void moo_surface_zeilen_parallel(void (*fn)(void*, int32_t, int32_t),
                                 void* ctx, int32_t y0, int32_t y1,
                                 int64_t pixel_gesamt);
bool moo_surface_core_circle(MooSurfaceCore* core, int32_t cx, int32_t cy,
                             int32_t radius, MooSurfaceColor color);
bool moo_surface_core_line(MooSurfaceCore* core, int32_t x0, int32_t y0,
                           int32_t x1, int32_t y1, MooSurfaceColor color);

/* Internal 3x5 ASCII helper. It is not a Moo runtime builtin. */
bool moo_surface_core_text_3x5(MooSurfaceCore* core, int32_t x, int32_t y,
                               int32_t scale, const char* text,
                               size_t text_len, MooSurfaceColor color);
bool moo_surface_core_text_width_3x5(const char* text, size_t text_len,
                                     int32_t scale, int32_t* out_width);

bool moo_surface_core_read_pixel(const MooSurfaceCore* core, int32_t x,
                                 int32_t y, MooSurfaceColor* out_color);
uint64_t moo_surface_core_hash(const MooSurfaceCore* core);

#endif

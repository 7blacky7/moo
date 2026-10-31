#ifndef MOO_3D_BACKEND_H
#define MOO_3D_BACKEND_H

/**
 * moo_3d_backend.h — Backend-Interface fuer Multi-Engine 3D-Rendering.
 * Jedes Backend implementiert diesen Funktionszeiger-Struct.
 */

#include <stdint.h>  /* uint8_t fuer grab_rgba (Plan-008 A3A) */

/* Borrowed backend strings, copied by the caller while the context is alive. */
typedef struct {
    const char* backend;
    const char* api;
    const char* vendor;
    const char* renderer;
    const char* version;
} MooRendererInfo;

typedef struct {
    float ior, max_distance;
    float absorption[3], tint[3], sky_horizon[3], sky_zenith[3];
} MooWaterSettings;

typedef struct {
    // Lifecycle
    void* (*create_window)(const char* title, int w, int h);
    void  (*close)(void* ctx);
    int   (*is_open)(void* ctx);
    // Frame
    void  (*clear)(void* ctx, float r, float g, float b);
    void  (*swap)(void* ctx);
    // Kamera
    void  (*perspective)(void* ctx, float fov, float near, float far);
    void  (*camera)(void* ctx, float ex, float ey, float ez, float lx, float ly, float lz);
    // Transform
    void  (*push_matrix)(void* ctx);
    void  (*pop_matrix)(void* ctx);
    void  (*translate)(void* ctx, float x, float y, float z);
    void  (*rotate)(void* ctx, float angle, float ax, float ay, float az);
    // Zeichnen
    void  (*cube)(void* ctx, float x, float y, float z, float size, float r, float g, float b);
    void  (*sphere)(void* ctx, float x, float y, float z, float radius, float r, float g, float b, int detail);
    void  (*triangle)(void* ctx, float x1, float y1, float z1, float x2, float y2, float z2, float x3, float y3, float z3, float r, float g, float b);
    // Gouraud-Variante: eigene Farbe pro Vertex, Shader/Fixed-Function interpoliert.
    void  (*triangle_colors)(void* ctx, float x1, float y1, float z1, float r1, float g1, float b1, float x2, float y2, float z2, float r2, float g2, float b2, float x3, float y3, float z3, float r3, float g3, float b3);
    int   (*key_pressed)(void* ctx, const char* key);
    // Maus
    void  (*capture_mouse)(void* ctx);
    void  (*release_mouse)(void* ctx);
    float (*mouse_dx)(void* ctx);
    float (*mouse_dy)(void* ctx);
    float (*mouse_x)(void* ctx);
    float (*mouse_y)(void* ctx);
    int   (*mouse_button)(void* ctx, int btn); // 0=LMB, 1=RMB, 2=MMB
    float (*mouse_wheel)(void* ctx);           // accumulated scroll-y, consume-on-read
    // Fog + Licht
    void  (*set_fog_density)(void* ctx, float density);
    /* Opt-in analytic air extinction; returns 0 when HDR/backend is unavailable. */
    int   (*set_height_fog)(void* ctx, int on, float base_height, float falloff, float sun_strength);
    void  (*set_fog_color)(void* ctx, float r, float g, float b);
    void  (*sky)(void* ctx, float zr, float zg, float zb, float hr, float hg, float hb);
    void  (*set_light_dir)(void* ctx, float x, float y, float z);
    void  (*set_ambient)(void* ctx, float level);
    // Globale Transparenz fuer folgende Draws (1 = opak). Transparentes zuletzt zeichnen.
    void  (*set_alpha)(void* ctx, float level);
    // Wasserwellen: zeitanimiertes Vertex-Displacement fuer folgende Draws (amp 0 = aus).
    void  (*set_wave)(void* ctx, float amp, float freq, float speed);
    // Lichtfarbe (Tageszeit/Stimmung). Default weiss.
    void  (*set_light_color)(void* ctx, float r, float g, float b);
    // Specular-Glanz fuer folgende Draws (strength 0 = aus), power = Haerte.
    void  (*set_spec)(void* ctx, float strength, float power);
    // Chunks
    int   (*chunk_create)(void* ctx);
    void  (*chunk_begin)(void* ctx, int id);
    void  (*chunk_end)(void* ctx);
    void  (*chunk_draw)(void* ctx, int id);
    void  (*chunk_delete)(void* ctx, int id);
    // Screenshot — schreibt aktuelles Frame-Buffer als BMP. Returns 1 bei Erfolg, 0 sonst.
    int   (*screenshot_bmp)(void* ctx, const char* path);
    // Frame-Grab (Plan-008 A3A) — liefert aktuellen Framebuffer als RGBA8,
    // top-left origin (Y-Flip backend-uebergreifend EINHEITLICH hier erledigt).
    // Allokiert width*height*4 Bytes via malloc (Aufrufer free). Schreibt Dims
    // nach *out_w/*out_h. Returns Buffer oder NULL bei Fehler / nicht unterstuetzt.
    // NULL signalisiert dem test_*-Layer ein klares moo_throw (nie still false).
    uint8_t* (*grab_rgba)(void* ctx, int* out_w, int* out_h);
    // Test-Sim: programmatische Eingaben fuer Selbsttests
    void  (*simulate_mouse_pos)(void* ctx, float x, float y);
    void  (*simulate_mouse_button)(void* ctx, int btn, int pressed); // btn 0/1/2, pressed 0/1
    void  (*simulate_scroll)(void* ctx, float dy);
    // Tastatur-Sim (Tri-State): key als Name-String (gleiche Konvention wie key_pressed).
    // pressed 1 = simuliert gedrueckt, 0 = simuliert losgelassen. key_pressed liest
    // den Sim-State ZUERST; nicht-simulierte Tasten fallen auf echten Input zurueck.
    void  (*simulate_key)(void* ctx, const char* key, int pressed);
    // Maus-Delta-Sim (consume-on-read, SEPARAT von simulate_mouse_pos): akkumuliert
    // dx/dy; mouse_dx/mouse_dy konsumieren den Sim-Delta bevorzugt und nullen ihn.
    void  (*simulate_mouse_delta)(void* ctx, float dx, float dy);
    // Setzt alle Sim-States (Tastatur Tri-State + Maus-Delta) zurueck.
    void  (*simulate_reset)(void* ctx);
    // ERDE-5: nicht-uniforme Skalierung der Model-Matrix (NULL = nicht unterstuetzt).
    void  (*scale)(void* ctx, float sx, float sy, float sz);
    // ERDE-5: Farbton, mit der Vertex-Farbe folgender Draws multipliziert (1,1,1 = aus).
    void  (*set_tint)(void* ctx, float r, float g, float b);
    // ERDE-7: Richtungslicht idx 0..3 (0 = Hauptlicht) und Punktlicht idx 0..3.
    void  (*set_extra_light)(void* ctx, int idx, float x, float y, float z, float r, float g, float b);
    void  (*set_point_light)(void* ctx, int idx, float x, float y, float z, float radius, float r, float g, float b);
    // ERDE-8: Eigenleuchten (0 = aus) und Mischmodus (1 = additiv, 0 = normal).
    void  (*set_emissive)(void* ctx, float strength);
    void  (*set_blend_mode)(void* ctx, int additive);
    // ERDE-10: unbeleuchtete 3D-Linie und -Punkt (size in Pixeln), sofort gezeichnet.
    void  (*line)(void* ctx, float x1, float y1, float z1, float x2, float y2, float z2, float r, float g, float b);
    void  (*point)(void* ctx, float x, float y, float z, float size, float r, float g, float b);
    // Licht-Stufe 3: 1 = Normalen beim chunk_ende weich mitteln (Knick >= 60 Grad bleibt scharf).
    void  (*set_smooth)(void* ctx, int on);
    // ERDE-15: Textur aus RGBA8 (Zeile 0 oben) anlegen -> ID >= 1 (oder < 1 bei Fehler);
    // set_texture: aktive Textur fuer folgende Draws (0 = aus), triplanar, scale pro Welteinheit.
    int   (*texture_create)(void* ctx, const uint8_t* rgba, int w, int h);
    void  (*set_texture)(void* ctx, int id, float scale);
    // Licht-Stufe 4: Shadow-Map des Hauptlichts (size = Aufloesung, range = Bereich um den Blickpunkt).
    void  (*set_shadow)(void* ctx, int on, int size, float range);
    // ERDE-16: Bloom-Post-Pass am Frame-Ende (strength 0 = aus, threshold 0..1).
    void  (*set_bloom)(void* ctx, float strength, float threshold, int passes);
    // GFX-TIME: fixed=1 setzt Sekunden sofort; fixed=0 kehrt sofort zur Live-Uhr zurueck.
    // NULL = nicht unterstuetzt. Capture/Present veraendern fixe Zeit nicht.
    void  (*set_render_time)(void* ctx, int fixed, float seconds);
    void  (*set_hdr)(void* ctx, int on);
    void  (*set_exposure)(void* ctx, float multiplier);
    void  (*set_auto_exposure)(void* ctx, float minimum, float maximum, float key, float tau);
    float (*exposure_value)(void* ctx);
    void (*set_linear_light)(void* ctx, int slot, float r, float g, float b);
    void (*set_pbr)(void* ctx, int on, float metallic, float roughness);
    /* AO status callbacks restore backend state before returning 0; caller throws. */
    int (*set_ao)(void* ctx, int on, float radius, float strength, float bias);
    int (*set_ao_debug)(void* ctx, int on);
    int (*opaque_end)(void* ctx);
    void  (*scene_end)(void* ctx);
    // Metadata only. NULL/0 means unsupported; no capture, swap or frame mutation.
    int   (*renderer_info)(void* ctx, MooRendererInfo* info);
    int   (*water_background)(void* ctx);
    int   (*set_water)(void* ctx, const MooWaterSettings* settings);
} Moo3DBackend;

// Backend-Deklarationen (je nach Feature-Flag verfuegbar)
extern Moo3DBackend moo_backend_gl21;
extern Moo3DBackend moo_backend_gl33;
extern Moo3DBackend moo_backend_vulkan;

#endif

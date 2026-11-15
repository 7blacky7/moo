/**
 * moo_3d.c — Thin Dispatcher fuer moo 3D-Rendering.
 * Konvertiert MooValue → native C-Typen und delegiert an das aktive Backend.
 */

#include "moo_runtime.h"
#include "moo_3d_backend.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <math.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

static const float MOO_3D_PI_F = 3.14159265358979323846f;

// Vorwaertsdeklarationen
extern MooValue moo_string_new(const char* s);
extern MooValue moo_bool(bool b);
extern MooValue moo_number(double n);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);

// === Aktives Backend ===
static Moo3DBackend* g_backend = NULL;
static void* g_ctx = NULL;
/* Single active context only. Pointer-backed handles are not generation IDs:
 * allocator address reuse after close is not distinguishable from a new handle. */
static MooValue g_active_window = { .tag = MOO_NONE };
/* P2-T25 (Review c7071266): Netz-Platzbuch und offener chunk_begin gehoeren zum
 * Backendkontext und werden bei jedem Kontextwechsel explizit geleert. */
static void netz_front_zuruecksetzen(void);

void moo_3d_register_window(MooValue win, void* ctx) {
    if (ctx && ctx == g_ctx && g_backend &&
        (win.tag == MOO_WINDOW3D || win.tag == MOO_WINDOW_HYBRID) &&
        moo_val_as_ptr(win))
        g_active_window = win;
}

int moo_3d_active_window(MooValue win) {
    return g_backend && g_ctx &&
        (win.tag == MOO_WINDOW3D || win.tag == MOO_WINDOW_HYBRID) &&
        win.tag == g_active_window.tag && moo_val_as_ptr(win) &&
        moo_val_as_ptr(win) == moo_val_as_ptr(g_active_window);
}

void moo_3d_detach_external(void* ctx) {
    if (!ctx || ctx != g_ctx) return;
    g_active_window.tag = MOO_NONE;
    g_ctx = NULL;
    g_backend = NULL;
    netz_front_zuruecksetzen();
}

/* P6: extern hook — Hybrid-Renderer kann sich als aktives 3D-Backend
 * registrieren, sodass raum_*-Calls auf das Hybrid-Fenster zeichnen. */
void moo_3d_attach_external(void* backend, void* ctx) {
    g_active_window.tag = MOO_NONE;
    netz_front_zuruecksetzen();
    g_backend = (Moo3DBackend*)backend;
    g_ctx = ctx;
}

/* -1: invalid/closed/unregistered handle; 0: unsupported; 1: metadata. */
int moo_3d_renderer_info(MooValue win, MooRendererInfo* info) {
    if (!info || !moo_3d_active_window(win)) return -1;
    *info = (MooRendererInfo){0};
    if (!g_backend->renderer_info) return 0;
    return g_backend->renderer_info(g_ctx, info) ? 1 : 0;
}

static int moo_3d_hdr_window(MooValue win) {
    if (!moo_3d_active_window(win)) {
        moo_throw(moo_string_new("HDR: ungueltiges/geschlossenes Fenster")); return 0;
    }
    if (!g_backend->set_hdr || !g_backend->scene_end || !g_backend->set_exposure) {
        moo_throw(moo_string_new("HDR: Backend nicht unterstuetzt")); return 0;
    }
    return 1;
}
static int moo_3d_ao_window(MooValue win) {
    if(!moo_3d_hdr_window(win)) return 0;
    if(!g_backend->set_ao || !g_backend->set_ao_debug || !g_backend->opaque_end) {
        moo_throw(moo_string_new("AO: Backend nicht unterstuetzt")); return 0;
    }
    return 1;
}
void moo_3d_ao(MooValue win,MooValue radius,MooValue strength,MooValue bias) {
    if(!moo_3d_ao_window(win)) return;
    MooValue values[]={radius,strength,bias};
    for(int i=0;i<3;i++) if(values[i].tag!=MOO_NUMBER || !isfinite(MV_NUM(values[i]))) {
        moo_throw(moo_string_new("AO: Radius, Staerke und Bias muessen endliche Zahlen sein")); return;
    }
    double r=MV_NUM(radius),s=MV_NUM(strength),b=MV_NUM(bias);
    if(r<0 || r>10000 || s<0 || s>1 || b<0 || b>r) {
        moo_throw(moo_string_new("AO: Radius 0..10000, Staerke 0..1, Bias 0..Radius erforderlich")); return;
    }
    if(!g_backend->set_ao(g_ctx,1,(float)r,(float)s,(float)b))
        moo_throw(moo_string_new("AO: HDR und Konfiguration an einer Framegrenze erforderlich"));
}
void moo_3d_ao_off(MooValue win) {
    if(!moo_3d_ao_window(win)) return;
    if(!g_backend->set_ao(g_ctx,0,0,0,0))
        moo_throw(moo_string_new("AO aus: HDR und Framegrenze erforderlich"));
}
void moo_3d_ao_debug(MooValue win,MooValue on) {
    if(!moo_3d_ao_window(win)) return;
    if(on.tag!=MOO_BOOL) { moo_throw(moo_string_new("AO-Debug: Bool erforderlich")); return; }
    if(!g_backend->set_ao_debug(g_ctx,MV_BOOL(on)?1:0))
        moo_throw(moo_string_new("AO-Debug: HDR, AO-Konfiguration und Framegrenze erforderlich"));
}
void moo_3d_opaque_end(MooValue win) {
    if(!moo_3d_ao_window(win)) return;
    if(!g_backend->opaque_end(g_ctx))
        moo_throw(moo_string_new("Opaker Abschluss: HDR-Szene erforderlich oder AO-Pass fehlgeschlagen"));
}
void moo_3d_pbr(MooValue win,MooValue metallic,MooValue roughness) {
    if(!moo_3d_hdr_window(win)) return;
    if(!g_backend->set_pbr) { moo_throw(moo_string_new("PBR: Backend nicht unterstuetzt")); return; }
    MooValue values[]={metallic,roughness};
    for(int i=0;i<2;i++) if(values[i].tag!=MOO_NUMBER || !isfinite(MV_NUM(values[i])) ||
            MV_NUM(values[i])<0 || MV_NUM(values[i])>1) {
        moo_throw(moo_string_new("PBR: metallic und roughness muessen endlich in 0..1 sein")); return;
    }
    g_backend->set_pbr(g_ctx,1,(float)MV_NUM(metallic),(float)MV_NUM(roughness));
}
void moo_3d_pbr_off(MooValue win) {
    if(!moo_3d_hdr_window(win)) return;
    if(!g_backend->set_pbr) { moo_throw(moo_string_new("PBR: Backend nicht unterstuetzt")); return; }
    g_backend->set_pbr(g_ctx,0,0,1);
}
void moo_3d_hdr(MooValue win, MooValue on) {
    if (!moo_3d_hdr_window(win)) return;
    if (on.tag != MOO_BOOL && on.tag != MOO_NUMBER) {
        moo_throw(moo_string_new("raum_hdr: erwartet Bool oder 0/1")); return;
    }
    double v=on.tag==MOO_BOOL ? (MV_BOOL(on)?1.0:0.0) : MV_NUM(on);
    if (v!=0.0 && v!=1.0) { moo_throw(moo_string_new("raum_hdr: erwartet 0/1")); return; }
    g_backend->set_hdr(g_ctx,(int)v);
}
void moo_3d_linear_light(MooValue win,MooValue slot,MooValue r,MooValue g,MooValue b) {
    if (!moo_3d_hdr_window(win)) return;
    if (!g_backend->set_linear_light) { moo_throw(moo_string_new("Lineares Licht: Backend nicht unterstuetzt")); return; }
    MooValue values[]={slot,r,g,b};
    for(int i=0;i<4;i++) if(values[i].tag!=MOO_NUMBER || !isfinite(MV_NUM(values[i])) ||
            MV_NUM(values[i])<0 || MV_NUM(values[i])>(i==0?7:65504)) {
        moo_throw(moo_string_new("Lineares Licht: Slot 0..7, RGB endlich 0..65504 erforderlich")); return;
    }
    double s=MV_NUM(slot);
    if(s!=floor(s)) { moo_throw(moo_string_new("Lineares Licht: ganzzahliger Slot erforderlich")); return; }
    g_backend->set_linear_light(g_ctx,(int)s,(float)MV_NUM(r),(float)MV_NUM(g),(float)MV_NUM(b));
}
void moo_3d_auto_exposure(MooValue win,MooValue minimum,MooValue maximum,MooValue key,MooValue tau) {
    if (!moo_3d_hdr_window(win)) return;
    if (!g_backend->set_auto_exposure) { moo_throw(moo_string_new("Auto-Belichtung: Backend nicht unterstuetzt")); return; }
    MooValue values[]={minimum,maximum,key,tau};
    float v[4];
    for(int i=0;i<4;i++) {
        if(values[i].tag!=MOO_NUMBER || !isfinite(MV_NUM(values[i])) || !isfinite((float)MV_NUM(values[i]))) {
            moo_throw(moo_string_new("Auto-Belichtung: endliche Zahlen erforderlich")); return;
        }
        v[i]=(float)MV_NUM(values[i]);
    }
    if(v[0]<=0 || MV_NUM(maximum)<MV_NUM(minimum) || v[1]<v[0] || MV_NUM(maximum)>64 || v[2]<=0 || MV_NUM(tau)<0) {
        moo_throw(moo_string_new("Auto-Belichtung: 0<min<=max<=64, key>0, tau>=0 erforderlich")); return;
    }
    g_backend->set_auto_exposure(g_ctx,v[0],v[1],v[2],v[3]);
}
MooValue moo_3d_exposure_value(MooValue win) {
    if (!moo_3d_hdr_window(win)) return moo_none();
    if (!g_backend->exposure_value) { moo_throw(moo_string_new("Belichtungswert: Backend nicht unterstuetzt")); return moo_none(); }
    return moo_number(g_backend->exposure_value(g_ctx));
}
void moo_3d_exposure(MooValue win, MooValue multiplier) {
    if (!moo_3d_hdr_window(win)) return;
    double v=MV_NUM(multiplier);
    if (multiplier.tag!=MOO_NUMBER || !isfinite(v) || v<0.0 || v>64.0) {
        moo_throw(moo_string_new("Belichtung: endlicher Multiplikator 0..64 erforderlich")); return;
    }
    g_backend->set_exposure(g_ctx,(float)v);
}
void moo_3d_scene_end(MooValue win) {
    if (moo_3d_hdr_window(win)) g_backend->scene_end(g_ctx);
}

/* Plan-005 1b: Sagt dem Voxel-Mesher, ob ein 3D-Backend aktiv ist.
 * GPU-Render-Cache-Lifetime (Risiko 10): der Mesher legt Render-Chunk-IDs NUR
 * an, wenn ein Backend lebt. Ohne Backend liefe moo_3d_chunk_create() in ein
 * moo_throw; chunk_begin/end/draw/delete sind dagegen safe no-ops. Damit darf
 * eine VoxelWorld ein geschlossenes Fenster/Backend ueberleben: CPU-Daten
 * werden immer gepflegt, GPU-Calls nur bei aktivem Kontext. */
int moo_3d_backend_active(void) {
    return (g_backend != NULL && g_ctx != NULL) ? 1 : 0;
}

// === Farb-Helfer (Backend-agnostisch) ===
typedef struct { float r, g, b; } Color3;

static Color3 parse_color3(MooValue c) {
    Color3 col = {1.0f, 1.0f, 1.0f};
    /* Performance-Pfad fuer prozedurale Meshes: 0xRRGGBB als exakte
     * Moo-Zahl vermeidet tausende temporaere Farbstrings. Nur ganzzahlige
     * Werte im 24-Bit-Bereich sind Farben; alles andere behaelt wie bisher
     * den sicheren Weiss-Default. Der Cast ist durch den Range-Check definiert. */
    if (c.tag == MOO_NUMBER) {
        double numeric = MV_NUM(c);
        if (numeric >= 0.0 && numeric <= 16777215.0) {
            uint32_t hex = (uint32_t)numeric;
            if ((double)hex == numeric) {
                col.r = (float)((hex >> 16u) & 0xFFu) / 255.0f;
                col.g = (float)((hex >> 8u) & 0xFFu) / 255.0f;
                col.b = (float)(hex & 0xFFu) / 255.0f;
            }
        }
        return col;
    }
    if (c.tag != MOO_STRING) return col;
    const char* s = MV_STR(c)->chars;

    if (s[0] == '#' && strlen(s) == 7) {
        unsigned int hex;
        sscanf(s + 1, "%06x", &hex);
        col.r = ((hex >> 16) & 0xFF) / 255.0f;
        col.g = ((hex >> 8) & 0xFF) / 255.0f;
        col.b = (hex & 0xFF) / 255.0f;
        return col;
    }

    if (strcmp(s, "rot") == 0 || strcmp(s, "red") == 0) { col.r=1; col.g=0; col.b=0; }
    else if (strcmp(s, "gruen") == 0 || strcmp(s, "green") == 0 || strcmp(s, "grün") == 0) { col.r=0; col.g=1; col.b=0; }
    else if (strcmp(s, "blau") == 0 || strcmp(s, "blue") == 0) { col.r=0; col.g=0; col.b=1; }
    else if (strcmp(s, "weiss") == 0 || strcmp(s, "white") == 0 || strcmp(s, "weiß") == 0) { col.r=1; col.g=1; col.b=1; }
    else if (strcmp(s, "schwarz") == 0 || strcmp(s, "black") == 0) { col.r=0; col.g=0; col.b=0; }
    else if (strcmp(s, "gelb") == 0 || strcmp(s, "yellow") == 0) { col.r=1; col.g=1; col.b=0; }
    else if (strcmp(s, "cyan") == 0) { col.r=0; col.g=1; col.b=1; }
    else if (strcmp(s, "magenta") == 0) { col.r=1; col.g=0; col.b=1; }
    else if (strcmp(s, "orange") == 0) { col.r=1; col.g=0.65f; col.b=0; }
    else if (strcmp(s, "grau") == 0 || strcmp(s, "gray") == 0) { col.r=0.5f; col.g=0.5f; col.b=0.5f; }
    return col;
}

#include "moo_3d_water_dispatch.h"

// ============================================================
// Fenster
// ============================================================

MooValue moo_3d_create(MooValue title, MooValue w, MooValue h) {
    g_active_window.tag = MOO_NONE;
    netz_front_zuruecksetzen();
    const char* backend_name = getenv("MOO_3D_BACKEND");
    if (!backend_name) backend_name = "gl33";

    g_backend = NULL;
#ifdef MOO_HAS_GL21
    if (g_backend == NULL && strcmp(backend_name, "gl21") == 0)
        g_backend = &moo_backend_gl21;
#endif
#ifdef MOO_HAS_GL33
    if (g_backend == NULL && strcmp(backend_name, "gl33") == 0)
        g_backend = &moo_backend_gl33;
#endif
#ifdef MOO_HAS_VULKAN
    if (g_backend == NULL && strcmp(backend_name, "vulkan") == 0)
        g_backend = &moo_backend_vulkan;
#endif
    if (g_backend == NULL) {
        moo_throw(moo_string_new("Unbekanntes 3D-Backend"));
        return moo_none();
    }

    const char* t = (title.tag == MOO_STRING) ? MV_STR(title)->chars : "moo 3D";
    int width = (w.tag == MOO_NUMBER) ? (int)MV_NUM(w) : 800;
    int height = (h.tag == MOO_NUMBER) ? (int)MV_NUM(h) : 600;

    g_ctx = g_backend->create_window(t, width, height);
    if (!g_ctx) {
        moo_throw(moo_string_new("3D-Fenster erstellen fehlgeschlagen"));
        return moo_none();
    }

    MooValue result;
    result.tag = MOO_WINDOW3D;
    moo_val_set_ptr(&result, g_ctx);
    moo_3d_register_window(result, g_ctx);
    return result;
}

MooValue moo_3d_is_open(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx) return moo_bool(false);
    return moo_bool(g_backend->is_open(g_ctx));
}

void moo_3d_clear(MooValue win, MooValue r, MooValue g, MooValue b) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    g_backend->clear(g_ctx, (float)MV_NUM(r), (float)MV_NUM(g), (float)MV_NUM(b));
}

void moo_3d_update(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    g_backend->swap(g_ctx);
}

void moo_3d_close(MooValue win) {
    if (win.tag != MOO_WINDOW3D || !moo_3d_active_window(win)) return;
    Moo3DBackend* backend = g_backend;
    void* ctx = g_ctx;
    moo_3d_detach_external(ctx);
    backend->close(ctx);
}

// ============================================================
// Kamera & Projektion
// ============================================================

void moo_3d_perspective(MooValue win, MooValue fov, MooValue near_val, MooValue far_val) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    g_backend->perspective(g_ctx, (float)MV_NUM(fov), (float)MV_NUM(near_val), (float)MV_NUM(far_val));
}

void moo_3d_camera(MooValue win, MooValue eyeX, MooValue eyeY, MooValue eyeZ,
                   MooValue lookX, MooValue lookY, MooValue lookZ) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    g_backend->camera(g_ctx,
        (float)MV_NUM(eyeX), (float)MV_NUM(eyeY), (float)MV_NUM(eyeZ),
        (float)MV_NUM(lookX), (float)MV_NUM(lookY), (float)MV_NUM(lookZ));
}

// ============================================================
// Transform
// ============================================================

void moo_3d_rotate(MooValue win, MooValue angle, MooValue ax, MooValue ay, MooValue az) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    g_backend->rotate(g_ctx, (float)MV_NUM(angle), (float)MV_NUM(ax), (float)MV_NUM(ay), (float)MV_NUM(az));
}

void moo_3d_translate(MooValue win, MooValue x, MooValue y, MooValue z) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    g_backend->translate(g_ctx, (float)MV_NUM(x), (float)MV_NUM(y), (float)MV_NUM(z));
}

/* raum_skaliere(fenster, sx, sy, sz) — skaliert folgende Draws (Model-Matrix,
 * wirkt wie verschiebe/rotiere innerhalb von push/pop). */
void moo_3d_scale(MooValue win, MooValue sx, MooValue sy, MooValue sz) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->scale) return;
    g_backend->scale(g_ctx, (float)MV_NUM(sx), (float)MV_NUM(sy), (float)MV_NUM(sz));
}

/* raum_farbton(fenster, "#RRGGBB") — multipliziert die Farbe folgender Draws
 * (Tint pro chunk_zeichne-Instanz). "#FFFFFF" schaltet ihn ab. */
void moo_3d_tint(MooValue win, MooValue color) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->set_tint) return;
    Color3 c = parse_color3(color);
    g_backend->set_tint(g_ctx, c.r, c.g, c.b);
}

/* Moo-Zahl sicher nach int (NaN/ausserhalb -> -1, damit Backends ablehnen). */
static int moo_3d_index(MooValue v) {
    double d = MV_NUM(v);
    if (!(d >= 0.0 && d <= 1024.0)) return -1;
    return (int)d;
}

/* raum_licht_hinzufuegen(fenster, idx, x, y, z, farbe) — idx 0 = Hauptlicht,
 * 1..3 = zusaetzliche Richtungslichter (z.B. zweite Sonne, Mond). */
void moo_3d_light_add(MooValue win, MooValue idx, MooValue x, MooValue y, MooValue z, MooValue color) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->set_extra_light) return;
    Color3 c = parse_color3(color);
    g_backend->set_extra_light(g_ctx, moo_3d_index(idx), (float)MV_NUM(x), (float)MV_NUM(y), (float)MV_NUM(z), c.r, c.g, c.b);
}

/* raum_punktlicht(fenster, idx, x, y, z, radius, farbe) — idx 0..3, radius 0 = aus. */
void moo_3d_point_light(MooValue win, MooValue idx, MooValue x, MooValue y, MooValue z, MooValue radius, MooValue color) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->set_point_light) return;
    Color3 c = parse_color3(color);
    g_backend->set_point_light(g_ctx, moo_3d_index(idx), (float)MV_NUM(x), (float)MV_NUM(y), (float)MV_NUM(z), (float)MV_NUM(radius), c.r, c.g, c.b);
}

/* raum_leuchten(fenster, staerke) — Eigenleuchten folgender Draws (0 = aus). */
void moo_3d_emissive(MooValue win, MooValue strength) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->set_emissive) return;
    g_backend->set_emissive(g_ctx, (float)MV_NUM(strength));
}

/* raum_mischmodus(fenster, "additiv" | "normal") — additiv fuer Glow/Partikel/Blitze. */
void moo_3d_blend_mode(MooValue win, MooValue mode) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->set_blend_mode) return;
    int additive = 0;
    if (mode.tag == MOO_STRING) {
        const char* s = MV_STR(mode)->chars;
        additive = (strcmp(s, "additiv") == 0 || strcmp(s, "additive") == 0 || strcmp(s, "add") == 0);
    }
    g_backend->set_blend_mode(g_ctx, additive);
}

/* raum_linie(fenster, x1, y1, z1, x2, y2, z2, farbe) — unbeleuchtete 3D-Linie. */
void moo_3d_line(MooValue win, MooValue x1, MooValue y1, MooValue z1,
                 MooValue x2, MooValue y2, MooValue z2, MooValue color) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->line) return;
    Color3 c = parse_color3(color);
    g_backend->line(g_ctx, (float)MV_NUM(x1), (float)MV_NUM(y1), (float)MV_NUM(z1),
                    (float)MV_NUM(x2), (float)MV_NUM(y2), (float)MV_NUM(z2), c.r, c.g, c.b);
}

/* raum_punkt(fenster, x, y, z, groesse, farbe) — unbeleuchteter 3D-Punkt, groesse in Pixeln. */
void moo_3d_point(MooValue win, MooValue x, MooValue y, MooValue z, MooValue size, MooValue color) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->point) return;
    Color3 c = parse_color3(color);
    g_backend->point(g_ctx, (float)MV_NUM(x), (float)MV_NUM(y), (float)MV_NUM(z),
                     (float)MV_NUM(size), c.r, c.g, c.b);
}

/* raum_glaetten(fenster, an) — folgende chunk_ende mitteln Normalen gemeinsamer
 * Ecken (Smooth-Shading fuer Terrain/runde Koerper); Kanten ab 60 Grad bleiben scharf. */
void moo_3d_smooth(MooValue win, MooValue on) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->set_smooth) return;
    int an = (on.tag == MOO_BOOL) ? (MV_BOOL(on) ? 1 : 0) : (MV_NUM(on) != 0.0);
    g_backend->set_smooth(g_ctx, an);
}

/* raum_textur_laden(fenster, pfad) — PNG/JPG/BMP als Textur, liefert ID (>= 1)
 * oder nichts, wenn die Datei nicht geladen werden kann. */
MooValue moo_3d_texture_load(MooValue win, MooValue path) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->texture_create || path.tag != MOO_STRING) return moo_none();
    SDL_Surface* raw = IMG_Load(MV_STR(path)->chars);
    if (!raw) return moo_none();
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(raw);
    if (!rgba) return moo_none();
    int w = rgba->w, h = rgba->h;
    uint8_t* buf = (uint8_t*)malloc((size_t)w * (size_t)h * 4u);
    int id = -1;
    if (buf) {
        for (int row = 0; row < h; row++)
            memcpy(buf + (size_t)row * (size_t)w * 4u,
                   (const uint8_t*)rgba->pixels + (size_t)row * (size_t)rgba->pitch, (size_t)w * 4u);
        id = g_backend->texture_create(g_ctx, buf, w, h);
        free(buf);
    }
    SDL_FreeSurface(rgba);
    return id > 0 ? moo_number(id) : moo_none();
}

/* raum_textur_aus_flaeche(fenster, flaeche) — prozedurale Textur aus einer
 * moo-Surface (flaeche_neu + flaeche_*), liefert ID oder nichts. */
MooValue moo_3d_texture_from_surface(MooValue win, MooValue surface) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->texture_create) return moo_none();
    int32_t w = 0, h = 0;
    const uint8_t* px = moo_surface_rgba(surface, &w, &h);
    if (!px) return moo_none();
    int id = g_backend->texture_create(g_ctx, px, w, h);
    return id > 0 ? moo_number(id) : moo_none();
}

/* raum_textur(fenster, id, skala) — aktive Textur fuer folgende Draws (0 = aus),
 * triplanar in Weltkoordinaten; skala = Wiederholungen pro Welteinheit. */
void moo_3d_texture(MooValue win, MooValue id, MooValue scale) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->set_texture) return;
    int slot = (id.tag == MOO_NUMBER) ? moo_3d_index(id) : 0;
    g_backend->set_texture(g_ctx, slot, (float)MV_NUM(scale));
}

/* raum_schatten(fenster, an, aufloesung, bereich) — Schatten des Hauptlichts.
 * Nur Chunks (chunk_zeichne) werfen Schatten, alle Draws empfangen ihn; die
 * Map entsteht am Frame-Ende und gilt ab dem naechsten Frame. */
void moo_3d_shadow(MooValue win, MooValue on, MooValue size, MooValue range) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->set_shadow) return;
    int an = (on.tag == MOO_BOOL) ? (MV_BOOL(on) ? 1 : 0) : (MV_NUM(on) != 0.0);
    double s = (size.tag == MOO_NUMBER) ? MV_NUM(size) : 0.0;
    if (!(s >= 0.0 && s <= 8192.0)) s = 0.0;
    double r = (range.tag == MOO_NUMBER) ? MV_NUM(range) : 0.0;
    g_backend->set_shadow(g_ctx, an, (int)s, (float)r);
}

/* raum_bloom(fenster, staerke, schwelle) — helle Bildteile (ab schwelle, 0..1)
 * strahlen weich aus; staerke 0 = aus. Wirkt auf das ganze Bild inkl. HUD. */
void moo_3d_bloom(MooValue win, MooValue strength, MooValue threshold, MooValue radius) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->set_bloom) return;
    double t = (threshold.tag == MOO_NUMBER) ? MV_NUM(threshold) : 0.8;
    /* ERDE-17: radius = Blur-Durchgaenge 1..8 (Default 2). */
    double r = (radius.tag == MOO_NUMBER) ? MV_NUM(radius) : 2.0;
    if (!(r >= 1.0 && r <= 8.0)) r = (r > 8.0) ? 8.0 : 2.0;
    g_backend->set_bloom(g_ctx, (float)MV_NUM(strength), (float)t, (int)r);
}

void moo_3d_push(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    g_backend->push_matrix(g_ctx);
}

void moo_3d_pop(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    g_backend->pop_matrix(g_ctx);
}

// ============================================================
// 3D Zeichnen
// ============================================================

void moo_3d_triangle(MooValue win, MooValue x1, MooValue y1, MooValue z1,
                     MooValue x2, MooValue y2, MooValue z2,
                     MooValue x3, MooValue y3, MooValue z3, MooValue color) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    Color3 c = parse_color3(color);
    g_backend->triangle(g_ctx,
        (float)MV_NUM(x1), (float)MV_NUM(y1), (float)MV_NUM(z1),
        (float)MV_NUM(x2), (float)MV_NUM(y2), (float)MV_NUM(z2),
        (float)MV_NUM(x3), (float)MV_NUM(y3), (float)MV_NUM(z3),
        c.r, c.g, c.b);
}

void moo_3d_cube(MooValue win, MooValue x, MooValue y, MooValue z,
                 MooValue size, MooValue color) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    Color3 c = parse_color3(color);
    g_backend->cube(g_ctx,
        (float)MV_NUM(x), (float)MV_NUM(y), (float)MV_NUM(z),
        (float)MV_NUM(size), c.r, c.g, c.b);
}

void moo_3d_sphere(MooValue win, MooValue x, MooValue y, MooValue z,
                   MooValue radius, MooValue color, MooValue detail) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    Color3 c = parse_color3(color);
    int det = (detail.tag == MOO_NUMBER) ? (int)MV_NUM(detail) : 16;
    g_backend->sphere(g_ctx,
        (float)MV_NUM(x), (float)MV_NUM(y), (float)MV_NUM(z),
        (float)MV_NUM(radius), c.r, c.g, c.b, det);
}

// ============================================================
// Input
// ============================================================

MooValue moo_3d_key_pressed(MooValue win, MooValue key) {
    (void)win;
    if (!g_backend || !g_ctx || key.tag != MOO_STRING) return moo_bool(false);
    return moo_bool(g_backend->key_pressed(g_ctx, MV_STR(key)->chars));
}

// ============================================================
// Fog + Licht
// ============================================================

void moo_3d_set_fog_density(float density) {
    if (!g_backend || !g_ctx) return;
    g_backend->set_fog_density(g_ctx, density);
}

void moo_3d_set_light_dir(float x, float y, float z) {
    if (!g_backend || !g_ctx) return;
    g_backend->set_light_dir(g_ctx, x, y, z);
}

void moo_3d_set_ambient(float level) {
    if (!g_backend || !g_ctx) return;
    g_backend->set_ambient(g_ctx, level);
}

void moo_3d_set_fog_color(float r, float g, float b) {
    if (!g_backend || !g_ctx || !g_backend->set_fog_color) return;
    g_backend->set_fog_color(g_ctx, r, g, b);
}

/* MooValue-Wrapper: machen Fog + Licht als Sprach-Builtins nutzbar
 * (raum_licht / raum_umgebungslicht / raum_nebel / raum_nebel_farbe).
 * Farbe via parse_color3 — gleiche #RRGGBB-Konvention wie raum_dreieck. */
void moo_3d_light(MooValue win, MooValue x, MooValue y, MooValue z) {
    (void)win;
    moo_3d_set_light_dir((float)MV_NUM(x), (float)MV_NUM(y), (float)MV_NUM(z));
}

void moo_3d_ambient(MooValue win, MooValue level) {
    (void)win;
    moo_3d_set_ambient((float)MV_NUM(level));
}

void moo_3d_fog(MooValue win, MooValue density) {
    double d = density.tag == MOO_NUMBER ? MV_NUM(density) : -1.0;
    if (!isfinite(d) || d < 0 || d > 1e6) {
        moo_throw(moo_string_new("raum_nebel: endliche Dichte 0..1000000 erforderlich")); return;
    }
    (void)win;
    moo_3d_set_fog_density((float)d);
}

void moo_3d_height_fog(MooValue win, MooValue height, MooValue falloff, MooValue sun) {
    if (!moo_3d_active_window(win) || !g_backend->set_height_fog) {
        moo_throw(moo_string_new("Hoehennebel: aktives GL33-Fenster erforderlich")); return;
    }
    double h = MV_NUM(height), k = MV_NUM(falloff), s = MV_NUM(sun);
    if (height.tag != MOO_NUMBER || falloff.tag != MOO_NUMBER || sun.tag != MOO_NUMBER ||
        !isfinite(h) || !isfinite(k) || !isfinite(s) ||
        fabs(h) > 1e7 || k < 0 || k > 1000 || s < 0 || s > 1) {
        moo_throw(moo_string_new("Hoehennebel: Hoehe +/-1e7, Falloff 0..1000, Sonne 0..1 erforderlich")); return;
    }
    if (!g_backend->set_height_fog(g_ctx,1,(float)h,(float)k,(float)s)) {
        moo_throw(moo_string_new("Hoehennebel: offene HDR-Szene erforderlich")); return;
    }
}
void moo_3d_height_fog_off(MooValue win) {
    if (!moo_3d_active_window(win) || !g_backend->set_height_fog) {
        moo_throw(moo_string_new("Hoehennebel: aktives GL33-Fenster erforderlich")); return;
    }
    if (!g_backend->set_height_fog(g_ctx,0,0,0,0)) {
        moo_throw(moo_string_new("Hoehennebel: Backend nicht verfuegbar")); return;
    }
}

void moo_3d_fog_color(MooValue win, MooValue color) {
    (void)win;
    Color3 c = parse_color3(color);
    moo_3d_set_fog_color(c.r, c.g, c.b);
}


void moo_3d_sky(MooValue win, MooValue zenith, MooValue horizon) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->sky) return;
    Color3 z = parse_color3(zenith), h = parse_color3(horizon);
    g_backend->sky(g_ctx, z.r, z.g, z.b, h.r, h.g, h.b);
}

/* Gouraud-Dreieck: 3 Punkte + 3 Farben, Backend interpoliert.
 * Moo-Signatur: erst 9 Koordinaten, dann 3 Farben (konsistent zu raum_dreieck). */
void moo_3d_triangle_colors(MooValue win,
    MooValue x1, MooValue y1, MooValue z1,
    MooValue x2, MooValue y2, MooValue z2,
    MooValue x3, MooValue y3, MooValue z3,
    MooValue f1, MooValue f2, MooValue f3) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->triangle_colors) return;
    Color3 c1 = parse_color3(f1);
    Color3 c2 = parse_color3(f2);
    Color3 c3 = parse_color3(f3);
    g_backend->triangle_colors(g_ctx,
        (float)MV_NUM(x1), (float)MV_NUM(y1), (float)MV_NUM(z1), c1.r, c1.g, c1.b,
        (float)MV_NUM(x2), (float)MV_NUM(y2), (float)MV_NUM(z2), c2.r, c2.g, c2.b,
        (float)MV_NUM(x3), (float)MV_NUM(y3), (float)MV_NUM(z3), c3.r, c3.g, c3.b);
}

void moo_3d_set_alpha(float level) {
    if (!g_backend || !g_ctx || !g_backend->set_alpha) return;
    g_backend->set_alpha(g_ctx, level);
}

/* Builtin-Wrapper: raum_transparenz(fenster, alpha 0..1) — gilt fuer alle
 * folgenden Draws (auch chunk_zeichne). 1.0 = opak.
 * Konvention: Transparentes zuletzt zeichnen (Depth-Write-Verhalten). */
void moo_3d_alpha(MooValue win, MooValue level) {
    (void)win;
    moo_3d_set_alpha((float)MV_NUM(level));
}

void moo_3d_set_wave(float amp, float freq, float speed) {
    if (!g_backend || !g_ctx || !g_backend->set_wave) return;
    g_backend->set_wave(g_ctx, amp, freq, speed);
}

/* Builtin-Wrapper: raum_wellen(fenster, amp, freq, tempo) — zeitanimiertes
 * Vertex-Displacement fuer folgende Draws (amp 0 = aus). GL21 hat keinen
 * Vertex-Shader und ignoriert Wellen (NULL-Slot). */
void moo_3d_wave(MooValue win, MooValue amp, MooValue freq, MooValue speed) {
    (void)win;
    moo_3d_set_wave((float)MV_NUM(amp), (float)MV_NUM(freq), (float)MV_NUM(speed));
}

/* Wie die uebrigen raum_*-Aufrufe gilt dies fuer den aktiven 3D-Kontext,
 * auch wenn dieser an ein Hybrid-Fenster gebunden ist. */
void moo_3d_render_time(MooValue win, MooValue seconds) {
    (void)win;
    if (seconds.tag != MOO_NUMBER || !isfinite(MV_NUM(seconds)) ||
        MV_NUM(seconds) < 0.0 || MV_NUM(seconds) > (double)FLT_MAX) {
        moo_throw(moo_string_new("raum_renderzeit: Sekunden muessen endlich, nichtnegativ und im float-Bereich sein"));
        return;
    }
    if (!g_backend || !g_ctx || !g_backend->set_render_time) {
        moo_throw(moo_string_new("raum_renderzeit: aktives Backend unterstuetzt keine explizite Renderzeit"));
        return;
    }
    g_backend->set_render_time(g_ctx, 1, (float)MV_NUM(seconds));
}

void moo_3d_render_time_live(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->set_render_time) {
        moo_throw(moo_string_new("raum_renderzeit_live: aktives Backend unterstuetzt keine explizite Renderzeit"));
        return;
    }
    g_backend->set_render_time(g_ctx, 0, 0.0f);
}

void moo_3d_set_light_color(float r, float g, float b) {
    if (!g_backend || !g_ctx || !g_backend->set_light_color) return;
    g_backend->set_light_color(g_ctx, r, g, b);
}

void moo_3d_set_spec(float strength, float power) {
    if (!g_backend || !g_ctx || !g_backend->set_spec) return;
    g_backend->set_spec(g_ctx, strength, power);
}

/* raum_lichtfarbe(fenster, "#RRGGBB") — faerbt das Sonnenlicht. */
void moo_3d_light_color(MooValue win, MooValue color) {
    (void)win;
    Color3 c = parse_color3(color);
    moo_3d_set_light_color(c.r, c.g, c.b);
}

/* raum_glanz(fenster, staerke, haerte) — Blinn-Phong-Specular als
 * Draw-State (staerke 0 = aus). Fuer Sonnenglitzer auf Wasser. */
void moo_3d_spec(MooValue win, MooValue strength, MooValue power) {
    (void)win;
    moo_3d_set_spec((float)MV_NUM(strength), (float)MV_NUM(power));
}

/* raum_löschen_farbe(fenster, "#RRGGBB") — clear mit Hex-Farbe
 * (Gegenstueck zum Hex-Rueckgabewert von raum_tageszeit). */
void moo_3d_clear_hex(MooValue win, MooValue color) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    Color3 c = parse_color3(color);
    g_backend->clear(g_ctx, c.r, c.g, c.b);
}

static float moo_dn_lerp(float a, float b, float x) {
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return a + (b - a) * x;
}

/* raum_tageszeit(fenster, t 0..1) — Ein-Aufruf-Atmosphaere:
 * 0.0 = Mitternacht, 0.25 = Sonnenaufgang, 0.5 = Mittag, 0.75 = Untergang.
 * Setzt Sonnenrichtung, Lichtfarbe, Ambient und Nebelfarbe konsistent und
 * gibt die passende Himmelsfarbe als "#RRGGBB" zurueck (fuer raum_löschen_farbe). */
MooValue moo_3d_time_of_day(MooValue win, MooValue t_val) {
    (void)win;
    float t = (float)MV_NUM(t_val);
    t = t - floorf(t);
    float elev = sinf((t - 0.25f) * 2.0f * MOO_3D_PI_F);

    /* Sonnenrichtung: wandert Ost -> West, Elevation aus Sinus. */
    float az = (t - 0.25f) * 2.0f * MOO_3D_PI_F;
    float sy = elev > 0.08f ? elev : 0.08f;
    moo_3d_set_light_dir(cosf(az), sy, 0.35f);

    float lr, lg, lb, hr, hg, hb;
    if (elev >= 0.0f) {
        /* Tag: Daemmerungsfarben nur nahe Horizont (voller Tag ab elev 0.35) */
        float k = elev / 0.35f;
        if (k > 1.0f) k = 1.0f;
        lr = 1.0f;
        lg = moo_dn_lerp(0.62f, 0.97f, k);
        lb = moo_dn_lerp(0.42f, 0.90f, k);
        hr = moo_dn_lerp(0.93f, 0.62f, k);
        hg = moo_dn_lerp(0.63f, 0.79f, k);
        hb = moo_dn_lerp(0.48f, 0.93f, k);
        moo_3d_set_ambient(moo_dn_lerp(0.42f, 0.47f, k));
    } else {
        /* Nacht: Restglut -> kuehles Mondlicht (schneller Uebergang) */
        float k = -elev / 0.45f;
        if (k > 1.0f) k = 1.0f;
        lr = moo_dn_lerp(0.90f, 0.30f, k);
        lg = moo_dn_lerp(0.50f, 0.36f, k);
        lb = moo_dn_lerp(0.35f, 0.55f, k);
        hr = moo_dn_lerp(0.93f, 0.05f, k);
        hg = moo_dn_lerp(0.55f, 0.07f, k);
        hb = moo_dn_lerp(0.45f, 0.13f, k);
        moo_3d_set_ambient(moo_dn_lerp(0.42f, 0.24f, k));
    }
    moo_3d_set_light_color(lr, lg, lb);
    moo_3d_set_fog_color(hr, hg, hb);

    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X",
        (int)(hr * 255.0f), (int)(hg * 255.0f), (int)(hb * 255.0f));
    return moo_string_new(hex);
}

// ============================================================
// Maus
// ============================================================

void moo_3d_capture_mouse(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    g_backend->capture_mouse(g_ctx);
}

MooValue moo_3d_mouse_dx(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx) return moo_number(0);
    return moo_number((double)g_backend->mouse_dx(g_ctx));
}

MooValue moo_3d_mouse_dy(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx) return moo_number(0);
    return moo_number((double)g_backend->mouse_dy(g_ctx));
}

void moo_3d_release_mouse(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx) return;
    if (g_backend->release_mouse) g_backend->release_mouse(g_ctx);
}

MooValue moo_3d_mouse_x(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->mouse_x) return moo_number(0);
    return moo_number((double)g_backend->mouse_x(g_ctx));
}

MooValue moo_3d_mouse_y(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->mouse_y) return moo_number(0);
    return moo_number((double)g_backend->mouse_y(g_ctx));
}

MooValue moo_3d_mouse_button(MooValue win, MooValue button) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->mouse_button) return moo_bool(false);
    int btn = 0; /* default LMB */
    if (button.tag == MOO_STRING) {
        const char* s = MV_STR(button)->chars;
        if (strcmp(s, "links") == 0 || strcmp(s, "left") == 0 || strcmp(s, "l") == 0) btn = 0;
        else if (strcmp(s, "rechts") == 0 || strcmp(s, "right") == 0 || strcmp(s, "r") == 0) btn = 1;
        else if (strcmp(s, "mitte") == 0 || strcmp(s, "middle") == 0 || strcmp(s, "m") == 0) btn = 2;
    } else if (button.tag == MOO_NUMBER) {
        btn = (int)MV_NUM(button);
    }
    return moo_bool(g_backend->mouse_button(g_ctx, btn) != 0);
}

MooValue moo_3d_mouse_wheel(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->mouse_wheel) return moo_number(0);
    return moo_number((double)g_backend->mouse_wheel(g_ctx));
}

// ============================================================
// Chunk Display List System
// ============================================================

MooValue moo_3d_chunk_create(void) {
    if (!g_backend || !g_ctx) {
        moo_throw(moo_string_new("Kein 3D-Backend aktiv"));
        return moo_none();
    }
    int id = g_backend->chunk_create(g_ctx);
    if (id < 0) {
        return moo_number(-1.0); /* Graceful: Chunk nicht gecached, kein Crash */
    }
    return moo_number((double)id);
}

/* P2-T25: offener chunk_begin (Hochladen eines Netzes waehrenddessen ist ein Fehler). */
static int g_chunk_offen = 0;

void moo_3d_chunk_begin(MooValue chunk_id) {
    if (!g_backend || !g_ctx) return;
    g_chunk_offen = 1;
    g_backend->chunk_begin(g_ctx, (int)moo_as_number(chunk_id));
}

void moo_3d_chunk_end(void) {
    g_chunk_offen = 0;
    if (!g_backend || !g_ctx) return;
    g_backend->chunk_end(g_ctx);
}

void moo_3d_chunk_draw(MooValue chunk_id) {
    if (!g_backend || !g_ctx) return;
    g_backend->chunk_draw(g_ctx, (int)moo_as_number(chunk_id));
}

void moo_3d_chunk_delete(MooValue chunk_id) {
    if (!g_backend || !g_ctx) return;
    g_backend->chunk_delete(g_ctx, (int)moo_as_number(chunk_id));
}

/* ============================================================
 * P2-T25 (Plan 962e249a; GFX-Bezug P1-T478/T491): Retained-Netz.
 * Ein Netz IST ein Chunk (Griff = Chunk-Nummer), direkt aus Speicher gefuellt.
 * Reine C-Schnittstelle ohne MooValue fuer extern "C" aus nativem Moo
 * (stdlib/netz.moos). Fehlercodes (0 = ok, Erstellen liefert -Code):
 * 1 kein Backend, 2 nicht unterstuetzt, 3 Anzahl nicht durch 3 teilbar,
 * 4 Index >= Eckpunktzahl, 5 Anzahl ueber der Grenze, 6 kein freier Platz,
 * 7 offener chunk_begin, 8 ungueltiges Netz/Zeiger, 10 Hochladen im Backend
 * fehlgeschlagen.
 * ============================================================ */
#include <string.h>
#define MOO_NETZ_PLAETZE 8192
#define MOO_NETZ_MAX_ECKEN 16777216ull
#define MOO_NETZ_MAX_INDIZES 50331648ull
static unsigned char g_netz_platz[MOO_NETZ_PLAETZE];
static int g_netz_anzahl = 0;

/* Platzbuch gilt je Backendkontext: Erstellen, Attach und Detach/Schliessen
 * leeren es explizit (keine Zeigeridentitaet als Generation, Review c7071266). */
static void netz_front_zuruecksetzen(void) {
    memset(g_netz_platz, 0, sizeof g_netz_platz);
    g_netz_anzahl = 0;
    g_chunk_offen = 0;
}

static int netz_gueltig(int id) {
    return id >= 0 && id < MOO_NETZ_PLAETZE && g_netz_platz[id];
}

int moo_3d_netz_erstellen(void) {
    if (!g_backend || !g_ctx) return -1;
    if (!g_backend->chunk_upload) return -2;
    int id = g_backend->chunk_create(g_ctx);
    if (id < 0) return -6;
    if (id >= MOO_NETZ_PLAETZE) { g_backend->chunk_delete(g_ctx, id); return -6; }
    if (!g_netz_platz[id]) { g_netz_platz[id] = 1; g_netz_anzahl++; }
    return id;
}

int moo_3d_netz_hochladen(int id, const float* ecken9, uint64_t ecken,
                          const uint32_t* indizes, uint64_t index_anzahl) {
    if (!g_backend || !g_ctx) return 1;
    if (!g_backend->chunk_upload) return 2;
    if (g_chunk_offen) return 7;
    if (!netz_gueltig(id)) return 8;
    if (ecken > MOO_NETZ_MAX_ECKEN || index_anzahl > MOO_NETZ_MAX_INDIZES) return 5;
    if (ecken > 0 && !ecken9) return 8;
    if (index_anzahl > 0) {
        if (!indizes) return 8;
        if (index_anzahl % 3 != 0) return 3;
        for (uint64_t i = 0; i < index_anzahl; i++) {
            if ((uint64_t)indizes[i] >= ecken) return 4;
        }
    } else if (ecken % 3 != 0) {
        return 3;
    }
    return g_backend->chunk_upload(g_ctx, id, ecken9, (int)ecken,
                                   index_anzahl > 0 ? indizes : NULL, (int)index_anzahl);
}

int moo_3d_netz_zeichnen(int id, const float* m16) {
    if (!g_backend || !g_ctx) return 1;
    if (!netz_gueltig(id)) return 0; /* Nach Freigabe: sicheres Nichtstun (E5). */
    if (m16) {
        if (!g_backend->mult_matrix || !g_backend->push_matrix || !g_backend->pop_matrix) return 2;
        g_backend->push_matrix(g_ctx);
        g_backend->mult_matrix(g_ctx, m16);
        g_backend->chunk_draw(g_ctx, id);
        g_backend->pop_matrix(g_ctx);
    } else {
        g_backend->chunk_draw(g_ctx, id);
    }
    return 0;
}

int moo_3d_netz_zeichnen_hier(int id) {
    return moo_3d_netz_zeichnen(id, NULL);
}

/* mat4 by value (Moo: struct aus vier vec4-Spalten, 64 Byte, Spaltenordnung);
 * gleiches Speicherbild wie float[16]. */
typedef struct { float m[16]; } MooNetzMat4;
int moo_3d_netz_zeichnen_mat(int id, MooNetzMat4 m) {
    return moo_3d_netz_zeichnen(id, m.m);
}

int moo_3d_netz_freigeben(int id) {
    if (!g_backend || !g_ctx) return 1;
    if (!netz_gueltig(id)) return 0; /* doppelte Freigabe: sicheres Nichtstun */
    g_backend->chunk_delete(g_ctx, id);
    g_netz_platz[id] = 0;
    g_netz_anzahl--;
    return 0;
}

int moo_3d_netz_belegt(void) {
    return g_netz_anzahl;
}

/* ========================================================
 * Screenshot — dispatcht ueber Backend-Vtable.
 * Funktioniert in jedem Backend das screenshot_bmp implementiert
 * (gl33 + vulkan). gl21 hat einen no-op-Stub.
 * ======================================================== */
MooValue moo_3d_screenshot_bmp(MooValue window, MooValue path) {
    (void)window;
    if (path.tag != MOO_STRING) return moo_bool(false);
    if (!g_backend || !g_ctx || !g_backend->screenshot_bmp) return moo_bool(false);
    int rc = g_backend->screenshot_bmp(g_ctx, MV_STR(path)->chars);
    return moo_bool(rc != 0);
}

/* ========================================================
 * Frame-Grab (Plan-008 A3A) — dispatcht ueber Backend-Vtable grab_rgba.
 * Liefert malloc'ten RGBA8-top-left-Buffer (Aufrufer free) + Dims, oder NULL.
 * NULL bedeutet: Backend bietet keinen Readback (test-Layer wirft dann klar).
 * ======================================================== */
uint8_t* moo_3d_grab_rgba(MooValue window, int* out_w, int* out_h) {
    (void)window;
    if (!g_backend || !g_ctx || !g_backend->grab_rgba) return NULL;
    return g_backend->grab_rgba(g_ctx, out_w, out_h);
}

/* ========================================================
 * Test-Sim — programmatische Maus-Eingaben fuer Selbsttests.
 * ======================================================== */
void moo_3d_simulate_mouse_pos(MooValue win, MooValue x, MooValue y) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->simulate_mouse_pos) return;
    g_backend->simulate_mouse_pos(g_ctx, (float)MV_NUM(x), (float)MV_NUM(y));
}

void moo_3d_simulate_mouse_button(MooValue win, MooValue button, MooValue pressed) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->simulate_mouse_button) return;
    int btn = 0;
    if (button.tag == MOO_STRING) {
        const char* s = MV_STR(button)->chars;
        if (strcmp(s, "links") == 0 || strcmp(s, "left") == 0 || strcmp(s, "l") == 0) btn = 0;
        else if (strcmp(s, "rechts") == 0 || strcmp(s, "right") == 0 || strcmp(s, "r") == 0) btn = 1;
        else if (strcmp(s, "mitte") == 0 || strcmp(s, "middle") == 0 || strcmp(s, "m") == 0) btn = 2;
    } else if (button.tag == MOO_NUMBER) {
        btn = (int)MV_NUM(button);
    }
    int p = 0;
    if (pressed.tag == MOO_BOOL) p = MV_BOOL(pressed) ? 1 : 0;
    else if (pressed.tag == MOO_NUMBER) p = MV_NUM(pressed) != 0 ? 1 : 0;
    g_backend->simulate_mouse_button(g_ctx, btn, p);
}

void moo_3d_simulate_scroll(MooValue win, MooValue dy) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->simulate_scroll) return;
    g_backend->simulate_scroll(g_ctx, (float)MV_NUM(dy));
}

/* Tastatur-Sim (Tri-State, S3). key als Name-String (gleiche Konvention wie
 * raum_taste/key_pressed). pressed: bool oder Zahl (!=0 => gedrueckt). */
void moo_3d_simulate_key(MooValue win, MooValue key, MooValue pressed) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->simulate_key) return;
    if (key.tag != MOO_STRING) return;
    int p = 0;
    if (pressed.tag == MOO_BOOL) p = MV_BOOL(pressed) ? 1 : 0;
    else if (pressed.tag == MOO_NUMBER) p = MV_NUM(pressed) != 0 ? 1 : 0;
    g_backend->simulate_key(g_ctx, MV_STR(key)->chars, p);
}

/* Maus-Delta-Sim (consume-on-read, S4), SEPARAT von simulate_mouse_pos. */
void moo_3d_simulate_mouse_delta(MooValue win, MooValue dx, MooValue dy) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->simulate_mouse_delta) return;
    g_backend->simulate_mouse_delta(g_ctx, (float)MV_NUM(dx), (float)MV_NUM(dy));
}

/* Setzt alle Sim-States zurueck (Tastatur Tri-State + Maus-Delta + Pos + Button
 * + Wheel), damit Sim das Normalspiel nicht dauerhaft blockiert. */
void moo_3d_simulate_reset(MooValue win) {
    (void)win;
    if (!g_backend || !g_ctx || !g_backend->simulate_reset) return;
    g_backend->simulate_reset(g_ctx);
}

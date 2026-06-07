/**
 * moo_test_api.c — Einheitlicher, backend-agnostischer Test-API-Layer (Plan-008 A2).
 *
 * Alle test_*-Builtins dispatchen ueber window.tag auf die passende
 * Backend-Implementierung:
 *   MOO_WINDOW         -> 2D / SDL (moo_simulate_* / moo_screenshot in moo_graphics.c)
 *   MOO_WINDOW3D       -> 3D-Vtable (moo_3d_simulate_* / moo_3d_screenshot_bmp)
 *   MOO_WINDOW_HYBRID  -> 3D-Input-Bridge (gl33) + Hybrid-Screenshot
 *
 * Vorbild fuer den Tag-Dispatch ist moo_screenshot() in moo_graphics.c.
 *
 * Wichtige Entscheidungen:
 *  - Die alten raum_sim_* (3D) und moo_simulate_* (2D) bleiben als Aliases im
 *    Codegen erhalten — KEINE Breaking Changes. Dieser Layer fuegt nur die
 *    backend-agnostische test_*-Variante hinzu.
 *  - test_screenshot wirft bei gl21 (no-op-Screenshot-Stub, S5) ein moo_throw,
 *    NIEMALS still false — Tests brauchen klare Fehler.
 *  - test_sim_reset setzt ALLE Sim-States zurueck (3D-Vtable simulate_reset);
 *    fuer 2D ist die SDL-PushEvent-API zustandslos (kein Reset noetig).
 *
 * Dieser File wird nur in 3D-Builds kompiliert (build.rs, 3D-Feature-Block),
 * weil er auf moo_graphics.c (SDL-2D) und die 3D-Vtable angewiesen ist. In
 * Non-3D-Builds existieren weder Fenster-Tags noch diese Symbole; die Rust-
 * Bindings deklarieren die test_*-Symbole trotzdem immer (Gating rein auf
 * C-Link-Ebene via build.rs, gleiches Muster wie Voxel/3D).
 */

#include "moo_runtime.h"
#include <string.h>
#include <stdlib.h>

/* ---- Forward-Decls (in moo_graphics.c / moo_3d.c / moo_hybrid.c definiert) ---- */
extern MooValue moo_string_new(const char* s);
extern MooValue moo_bool(bool b);
extern MooValue moo_number(double n);
extern MooValue moo_none(void);
extern void     moo_throw(MooValue v);
extern MooValue moo_dict_new(void);
extern void     moo_dict_set(MooValue dict, MooValue key, MooValue value);

/* 2D (SDL) — moo_graphics.c */
extern MooValue moo_screenshot(MooValue window, MooValue path);
extern void     moo_simulate_key(MooValue key, MooValue pressed);
extern void     moo_simulate_mouse(MooValue x, MooValue y, MooValue click);

/* 3D-Vtable-Dispatcher — moo_3d.c */
extern MooValue moo_3d_screenshot_bmp(MooValue window, MooValue path);
extern MooValue moo_3d_is_open(MooValue win);
extern void     moo_3d_simulate_mouse_pos(MooValue win, MooValue x, MooValue y);
extern void     moo_3d_simulate_mouse_button(MooValue win, MooValue button, MooValue pressed);
extern void     moo_3d_simulate_scroll(MooValue win, MooValue dy);
extern void     moo_3d_simulate_key(MooValue win, MooValue key, MooValue pressed);
extern void     moo_3d_simulate_mouse_delta(MooValue win, MooValue dx, MooValue dy);
extern void     moo_3d_simulate_reset(MooValue win);
/* Frame-Grab-Dispatcher (Plan-008 A3A). moo_3d_grab_rgba -> moo_3d.c (Vtable);
 * moo_hybrid_grab_rgba -> moo_hybrid.c. Beide liefern malloc'ten RGBA8-top-left-
 * Buffer (Aufrufer free) + Dims, oder NULL. */
extern uint8_t* moo_3d_grab_rgba(MooValue win, int* out_w, int* out_h);

/* SDL fuer 2D-Fensterabmessungen + open-Flag. Nur in 3D-Builds verfuegbar,
 * wo moo_graphics.c (SDL2) mitkompiliert wird. */
#include <SDL2/SDL.h>

/* MooWindow-Layout muss mit moo_graphics.c uebereinstimmen. */
typedef struct {
    int32_t       refcount;
    SDL_Window*   window;
    SDL_Renderer* renderer;
    bool          open;
} MooTestSdlWindow;

/* Hybrid-Screenshot + is_open existieren nur im gl33-Build. */
#ifdef MOO_HAS_GL33
extern MooValue moo_hybrid_screenshot_bmp(MooValue window, MooValue path);
extern MooValue moo_hybrid_is_open(MooValue win);
extern uint8_t* moo_hybrid_grab_rgba(MooValue window, int* out_w, int* out_h);
#endif

/* Liefert den Namen des aktiven 3D-Backends. Auswahl erfolgt in moo_3d_create()
 * ueber die Umgebungsvariable MOO_3D_BACKEND (Default gl33) — dieselbe Quelle
 * hier auszuwerten ist konsistent mit der tatsaechlichen Backend-Wahl. */
static const char* test_active_3d_backend(void) {
    const char* b = getenv("MOO_3D_BACKEND");
    return (b && *b) ? b : "gl33";
}

/* ============================================================
 * Eingabe-Simulation (tag-dispatch)
 * ============================================================ */

/* Tastatur-Sim (Tri-State fuer 3D). 2D nutzt die SDL-PushEvent-API
 * (moo_simulate_key kennt kein Fenster-Argument). */
void moo_test_sim_taste(MooValue win, MooValue taste, MooValue gedrueckt) {
    if (win.tag == MOO_WINDOW3D || win.tag == MOO_WINDOW_HYBRID) {
        moo_3d_simulate_key(win, taste, gedrueckt);
    } else if (win.tag == MOO_WINDOW) {
        moo_simulate_key(taste, gedrueckt);
    }
}

/* Absolute Mausposition. 2D: moo_simulate_mouse(x,y,click=false). */
void moo_test_sim_maus_pos(MooValue win, MooValue x, MooValue y) {
    if (win.tag == MOO_WINDOW3D || win.tag == MOO_WINDOW_HYBRID) {
        moo_3d_simulate_mouse_pos(win, x, y);
    } else if (win.tag == MOO_WINDOW) {
        moo_simulate_mouse(x, y, moo_bool(false));
    }
}

/* Maustaste gedrueckt/losgelassen. 2D: ein Klick an aktueller Pos wird via
 * moo_simulate_mouse(click=true) emuliert wenn gedrueckt; up ist zustandslos. */
void moo_test_sim_maus_taste(MooValue win, MooValue taste, MooValue gedrueckt) {
    if (win.tag == MOO_WINDOW3D || win.tag == MOO_WINDOW_HYBRID) {
        moo_3d_simulate_mouse_button(win, taste, gedrueckt);
    } else if (win.tag == MOO_WINDOW) {
        /* 2D-Alt-API kann Klick nur als Down+Up-Paar an einer Position; ohne
         * Positions-Argument hier neutral (0,0). Fuer praezise 2D-Klicks
         * test_sim_maus_pos vorher aufrufen + maus_simulieren nutzen. */
        int down = (gedrueckt.tag == MOO_BOOL) ? MV_BOOL(gedrueckt)
                 : (gedrueckt.tag == MOO_NUMBER) ? (MV_NUM(gedrueckt) != 0) : 1;
        if (down) moo_simulate_mouse(moo_number(0), moo_number(0), moo_bool(true));
    }
}

/* Mausrad. dy>0 = hoch. 2D-SDL-Alt-API hat kein Wheel — no-op fuer 2D. */
void moo_test_sim_maus_rad(MooValue win, MooValue dy) {
    if (win.tag == MOO_WINDOW3D || win.tag == MOO_WINDOW_HYBRID) {
        moo_3d_simulate_scroll(win, dy);
    }
    /* MOO_WINDOW: 2D-Alt-API bietet keine Wheel-Simulation. */
}

/* Maus-Delta (consume-on-read, fuer Kamera-Look). Nur 3D/Hybrid sinnvoll
 * (relative Bewegung). 2D nutzt absolute Positionen. */
void moo_test_sim_maus_delta(MooValue win, MooValue dx, MooValue dy) {
    if (win.tag == MOO_WINDOW3D || win.tag == MOO_WINDOW_HYBRID) {
        moo_3d_simulate_mouse_delta(win, dx, dy);
    }
    /* MOO_WINDOW: kein relatives Delta-Modell. */
}

/* Setzt alle Sim-States zurueck (PFLICHT — damit Sim das Normalspiel nicht
 * dauerhaft blockiert). 3D: Vtable simulate_reset (Tastatur Tri-State + Maus-
 * Delta + Pos + Button + Wheel). 2D-SDL-PushEvent ist zustandslos. */
void moo_test_sim_reset(MooValue win) {
    if (win.tag == MOO_WINDOW3D || win.tag == MOO_WINDOW_HYBRID) {
        moo_3d_simulate_reset(win);
    }
    /* MOO_WINDOW: keine persistenten Sim-States. */
}

/* ============================================================
 * Screenshot (tag-dispatch wie moo_screenshot) — S5: nie still false
 * ============================================================ */

MooValue moo_test_screenshot(MooValue win, MooValue pfad) {
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_string_new("test_screenshot: Pfad muss ein String sein"));
        return moo_bool(false);
    }

    if (win.tag == MOO_WINDOW3D) {
        /* S5: gl21-Backend hat einen no-op-Screenshot-Stub. Klar werfen statt
         * still false zurueckzugeben — Tests brauchen einen eindeutigen Fehler. */
        if (strcmp(test_active_3d_backend(), "gl21") == 0) {
            moo_throw(moo_string_new(
                "test_screenshot: gl21-Backend unterstuetzt keine Screenshots "
                "(MOO_3D_BACKEND=gl33 oder vulkan verwenden)"));
            return moo_bool(false);
        }
        MooValue r = moo_3d_screenshot_bmp(win, pfad);
        if (r.tag == MOO_BOOL && !MV_BOOL(r)) {
            moo_throw(moo_string_new("test_screenshot: 3D-Screenshot fehlgeschlagen"));
            return moo_bool(false);
        }
        return r;
    }

    if (win.tag == MOO_WINDOW_HYBRID) {
#ifdef MOO_HAS_GL33
        MooValue r = moo_hybrid_screenshot_bmp(win, pfad);
        if (r.tag == MOO_BOOL && !MV_BOOL(r)) {
            moo_throw(moo_string_new("test_screenshot: Hybrid-Screenshot fehlgeschlagen"));
            return moo_bool(false);
        }
        return r;
#else
        moo_throw(moo_string_new(
            "test_screenshot: Hybrid-Fenster nur im gl33-Build verfuegbar"));
        return moo_bool(false);
#endif
    }

    if (win.tag == MOO_WINDOW) {
        MooValue r = moo_screenshot(win, pfad);
        if (r.tag == MOO_BOOL && !MV_BOOL(r)) {
            moo_throw(moo_string_new("test_screenshot: 2D-Screenshot fehlgeschlagen"));
            return moo_bool(false);
        }
        return r;
    }

    moo_throw(moo_string_new("test_screenshot: kein gueltiges Fenster"));
    return moo_bool(false);
}

/* ============================================================
 * Frame-Grab / Pixel (Plan-008 A3A) — opaker MOO_FRAME-Heap-Typ.
 * STANDARD: RGBA8, top-left origin (Y-Flip backend-uebergreifend einheitlich).
 * Pixeldaten NIE als moo-Liste (siehe moo_frame.c).
 * ============================================================ */

/* 2D-SDL-Grab via SDL_RenderReadPixels (liefert bereits top-left). Konvertiert
 * nach RGBA8. NULL bei Fehler. Buffer via malloc (Aufrufer free). */
static uint8_t* test_grab_sdl(MooValue win, int* out_w, int* out_h) {
    if (win.tag != MOO_WINDOW) return NULL;
    MooTestSdlWindow* mw = (MooTestSdlWindow*)moo_val_as_ptr(win);
    if (!mw || !mw->renderer) return NULL;
    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(mw->renderer, &w, &h) != 0) return NULL;
    if (w <= 0 || h <= 0) return NULL;
    uint8_t* buf = (uint8_t*)malloc((size_t)w * (size_t)h * 4);
    if (!buf) return NULL;
    /* ABGR8888 in SDL-Byte-Reihenfolge entspricht R,G,B,A im Speicher (LE). */
    if (SDL_RenderReadPixels(mw->renderer, NULL, SDL_PIXELFORMAT_ABGR8888,
                             buf, w * 4) != 0) {
        free(buf);
        return NULL;
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return buf;
}

/* test_frame_grab(win) -> MOO_FRAME. Wirft bei fehlendem Readback (S5: nie
 * still). Der Backend-Grab liefert garantiert RGBA8 top-left. */
MooValue moo_test_frame_grab(MooValue win) {
    int w = 0, h = 0;
    uint8_t* px = NULL;

    if (win.tag == MOO_WINDOW3D) {
        px = moo_3d_grab_rgba(win, &w, &h);
        if (!px) {
            moo_throw(moo_string_new(
                "test_frame_grab: 3D-Backend unterstuetzt keinen Frame-Grab "
                "(MOO_3D_BACKEND=gl33/gl21/vulkan verwenden)"));
            return moo_none();
        }
    } else if (win.tag == MOO_WINDOW_HYBRID) {
#ifdef MOO_HAS_GL33
        px = moo_hybrid_grab_rgba(win, &w, &h);
        if (!px) {
            moo_throw(moo_string_new("test_frame_grab: Hybrid-Frame-Grab fehlgeschlagen"));
            return moo_none();
        }
#else
        moo_throw(moo_string_new("test_frame_grab: Hybrid-Fenster nur im gl33-Build"));
        return moo_none();
#endif
    } else if (win.tag == MOO_WINDOW) {
        px = test_grab_sdl(win, &w, &h);
        if (!px) {
            moo_throw(moo_string_new("test_frame_grab: 2D-Frame-Grab fehlgeschlagen"));
            return moo_none();
        }
    } else {
        moo_throw(moo_string_new("test_frame_grab: kein gueltiges Fenster"));
        return moo_none();
    }

    /* moo_frame_new_take uebernimmt px (free bei moo_release/Fehler). */
    MooValue frame = moo_frame_new_take(w, h, px);
    if (frame.tag != MOO_FRAME) {
        moo_throw(moo_string_new("test_frame_grab: Frame-Erzeugung fehlgeschlagen"));
        return moo_none();
    }
    return frame;
}

/* test_pixel(frame_oder_win, x, y) -> Dict {rot,gruen,blau,alpha}.
 * Direktes MOO_FRAME: aus dem Frame lesen. Fenster: kurz grabben, lesen, frei. */
MooValue moo_test_pixel(MooValue frame_oder_win, MooValue x, MooValue y) {
    if (frame_oder_win.tag == MOO_FRAME) {
        return moo_frame_read_pixel(frame_oder_win, x, y);
    }
    if (x.tag != MOO_NUMBER || y.tag != MOO_NUMBER) {
        moo_throw(moo_string_new("test_pixel: x und y muessen Zahlen sein"));
        return moo_none();
    }
    MooValue frame = moo_test_frame_grab(frame_oder_win);
    if (frame.tag != MOO_FRAME) {
        /* moo_test_frame_grab hat bereits geworfen. */
        return moo_none();
    }
    MooFrame* f = MV_FRAME(frame);
    int ix = (int)MV_NUM(x);
    int iy = (int)MV_NUM(y);
    if (ix < 0 || iy < 0 || ix >= f->width || iy >= f->height) {
        moo_release(frame);
        moo_throw(moo_string_new("test_pixel: Koordinate ausserhalb des Frames"));
        return moo_none();
    }
    MooValue result = moo_frame_pixel_dict(f, ix, iy);
    moo_release(frame);
    return result;
}

/* ============================================================
 * Fenster-Info → Dict { breite, hoehe, backend, offen }
 * ============================================================
 * breite/hoehe:
 *   - 2D (SDL): echte Renderer-Output-Groesse.
 *   - 3D/Hybrid: -1 (unbekannt) — der opake 3D-Ctx hat keinen portablen
 *     Size-Getter in der Backend-Vtable. Die echten Framebuffer-Dimensionen
 *     liefert test_frame_grab()/MOO_FRAME (Plan-008 A3A). -1 statt 0, damit
 *     ein fehlender Wert nicht wie eine gueltige Groesse aussieht.
 */
MooValue moo_test_fenster_info(MooValue win) {
    MooValue dict = moo_dict_new();
    double breite = -1.0, hoehe = -1.0;
    const char* backend = "unbekannt";
    bool offen = false;

    if (win.tag == MOO_WINDOW) {
        backend = "sdl2";
        MooTestSdlWindow* mw = (MooTestSdlWindow*)moo_val_as_ptr(win);
        if (mw) {
            offen = mw->open;
            if (mw->renderer) {
                int w = 0, h = 0;
                SDL_GetRendererOutputSize(mw->renderer, &w, &h);
                breite = (double)w;
                hoehe  = (double)h;
            }
        }
    } else if (win.tag == MOO_WINDOW3D) {
        backend = test_active_3d_backend();
        MooValue o = moo_3d_is_open(win);
        offen = (o.tag == MOO_BOOL) ? MV_BOOL(o) : false;
        /* Echte Framebuffer-Dims (Plan-008 A3A): ueber die Grab-Infrastruktur
         * ermitteln. Ein kurzer Grab liefert die Backend-Dims; der Buffer wird
         * sofort wieder freigegeben. */
        int gw = 0, gh = 0;
        uint8_t* px = moo_3d_grab_rgba(win, &gw, &gh);
        if (px) { breite = (double)gw; hoehe = (double)gh; free(px); }
    } else if (win.tag == MOO_WINDOW_HYBRID) {
        backend = "hybrid-gl33";
#ifdef MOO_HAS_GL33
        MooValue o = moo_hybrid_is_open(win);
        offen = (o.tag == MOO_BOOL) ? MV_BOOL(o) : false;
        int gw = 0, gh = 0;
        uint8_t* px = moo_hybrid_grab_rgba(win, &gw, &gh);
        if (px) { breite = (double)gw; hoehe = (double)gh; free(px); }
#endif
    }

    moo_dict_set(dict, moo_string_new("breite"),  moo_number(breite));
    moo_dict_set(dict, moo_string_new("hoehe"),   moo_number(hoehe));
    moo_dict_set(dict, moo_string_new("backend"), moo_string_new(backend));
    moo_dict_set(dict, moo_string_new("offen"),   moo_bool(offen));
    return dict;
}

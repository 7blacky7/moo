/**
 * moo_3d.c — Thin Dispatcher fuer moo 3D-Rendering.
 * Konvertiert MooValue → native C-Typen und delegiert an das aktive Backend.
 */

#include "moo_runtime.h"
#include "moo_3d_backend.h"
#include <stdlib.h>
#include <string.h>

// Vorwaertsdeklarationen
extern MooValue moo_string_new(const char* s);
extern MooValue moo_bool(bool b);
extern MooValue moo_number(double n);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);

// === Aktives Backend ===
static Moo3DBackend* g_backend = NULL;
static void* g_ctx = NULL;

/* P6: extern hook — Hybrid-Renderer kann sich als aktives 3D-Backend
 * registrieren, sodass raum_*-Calls auf das Hybrid-Fenster zeichnen. */
void moo_3d_attach_external(void* backend, void* ctx) {
    g_backend = (Moo3DBackend*)backend;
    g_ctx = ctx;
}

// === Farb-Helfer (Backend-agnostisch) ===
typedef struct { float r, g, b; } Color3;

static Color3 parse_color3(MooValue c) {
    Color3 col = {1.0f, 1.0f, 1.0f};
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

// ============================================================
// Fenster
// ============================================================

MooValue moo_3d_create(MooValue title, MooValue w, MooValue h) {
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
    (void)win;
    if (!g_backend || !g_ctx) return;
    g_backend->close(g_ctx);
    g_ctx = NULL;
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

void moo_3d_chunk_begin(MooValue chunk_id) {
    if (!g_backend || !g_ctx) return;
    g_backend->chunk_begin(g_ctx, (int)moo_as_number(chunk_id));
}

void moo_3d_chunk_end(void) {
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

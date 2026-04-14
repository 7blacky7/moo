/**
 * moo_hybrid.c — Hybrid 2D+3D Renderer fuer moo (M5).
 *
 * Ein Fenster, ein GL-Context, ein Z-Buffer. Erlaubt 2D-Sprites/Quads
 * und 3D-Primitive in derselben Szene mit korrektem Z-Test.
 *
 * Z-Konvention: Welt-Einheiten (z=0.0 = ground, z=1.0 = ein Tile hoch).
 * Runtime mappt intern z/Z_RANGE → GL Clip-Range 0..1.
 *
 * Phase P2: Window + Frame-Pump + Stubs (kein Rendering).
 * Phase P3-P5: Quad-Shader + rect/line/circle/sprite_z.
 * Phase P6: 3D-Bridge zu moo_3d.c (raum_*-Calls auf Hybrid-Window).
 */

#include "moo_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GLFW + glad: identische Includes wie moo_3d_gl33.c */
#ifdef MOO_HAS_GL33
#include "glad/include/glad/glad.h"
#include <GLFW/glfw3.h>
#define MOO_HYBRID_HAS_GL 1
#else
#define MOO_HYBRID_HAS_GL 0
#endif

extern MooValue moo_string_new(const char* s);
extern MooValue moo_bool(bool b);
extern MooValue moo_number(double n);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);

/* ============================================================
 * Hybrid-Window-Struct
 * ============================================================ */

#define MOO_HYBRID_Z_RANGE 100.0f  /* Welt-Z 0..100 → GL Clip 0..1 */

typedef struct {
#if MOO_HYBRID_HAS_GL
    GLFWwindow* window;
#else
    void* window;
#endif
    int width;
    int height;
    bool open;
    /* P3: Quad-Renderer (VAO/VBO/Shader) — wird in Phase 3 ausgefuellt */
    unsigned int quad_vao;
    unsigned int quad_vbo;
    unsigned int quad_shader;
} MooHybridWindow;

static int hybrid_glfw_initialized = 0;

/* ============================================================
 * Window-Lifecycle (P2)
 * ============================================================ */

MooValue moo_hybrid_create(MooValue title, MooValue w, MooValue h) {
#if !MOO_HYBRID_HAS_GL
    moo_throw(moo_string_new("Hybrid-Renderer benoetigt gl33-Feature (cargo build --features gl33)"));
    return moo_none();
#else
    if (!hybrid_glfw_initialized) {
        if (!glfwInit()) {
            moo_throw(moo_string_new("glfwInit fehlgeschlagen"));
            return moo_none();
        }
        hybrid_glfw_initialized = 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    const char* t = (title.tag == MOO_STRING) ? MV_STR(title)->chars : "moo Hybrid";
    int width = (w.tag == MOO_NUMBER) ? (int)MV_NUM(w) : 800;
    int height = (h.tag == MOO_NUMBER) ? (int)MV_NUM(h) : 600;

    GLFWwindow* glfw_win = glfwCreateWindow(width, height, t, NULL, NULL);
    if (!glfw_win) {
        moo_throw(moo_string_new("Hybrid-Window create fehlgeschlagen"));
        return moo_none();
    }
    glfwMakeContextCurrent(glfw_win);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        glfwDestroyWindow(glfw_win);
        moo_throw(moo_string_new("glad Loader fehlgeschlagen"));
        return moo_none();
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, width, height);

    MooHybridWindow* mh = (MooHybridWindow*)calloc(1, sizeof(MooHybridWindow));
    mh->window = glfw_win;
    mh->width = width;
    mh->height = height;
    mh->open = true;

    MooValue result;
    result.tag = MOO_WINDOW_HYBRID;
    moo_val_set_ptr(&result, mh);
    return result;
#endif
}

MooValue moo_hybrid_is_open(MooValue win) {
#if !MOO_HYBRID_HAS_GL
    (void)win;
    return moo_bool(false);
#else
    if (win.tag != MOO_WINDOW_HYBRID) return moo_bool(false);
    MooHybridWindow* mh = (MooHybridWindow*)moo_val_as_ptr(win);
    if (!mh || !mh->open) return moo_bool(false);
    return moo_bool(!glfwWindowShouldClose(mh->window));
#endif
}

void moo_hybrid_clear(MooValue win, MooValue r, MooValue g, MooValue b) {
#if !MOO_HYBRID_HAS_GL
    (void)win; (void)r; (void)g; (void)b;
#else
    if (win.tag != MOO_WINDOW_HYBRID) return;
    MooHybridWindow* mh = (MooHybridWindow*)moo_val_as_ptr(win);
    if (!mh || !mh->open) return;
    glfwMakeContextCurrent(mh->window);
    glClearColor((float)MV_NUM(r), (float)MV_NUM(g), (float)MV_NUM(b), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
#endif
}

void moo_hybrid_update(MooValue win) {
#if !MOO_HYBRID_HAS_GL
    (void)win;
#else
    if (win.tag != MOO_WINDOW_HYBRID) return;
    MooHybridWindow* mh = (MooHybridWindow*)moo_val_as_ptr(win);
    if (!mh || !mh->open) return;
    glfwSwapBuffers(mh->window);
    glfwPollEvents();
#endif
}

void moo_hybrid_close(MooValue win) {
#if !MOO_HYBRID_HAS_GL
    (void)win;
#else
    if (win.tag != MOO_WINDOW_HYBRID) return;
    MooHybridWindow* mh = (MooHybridWindow*)moo_val_as_ptr(win);
    if (!mh) return;
    if (mh->window) {
        glfwDestroyWindow(mh->window);
        mh->window = NULL;
    }
    mh->open = false;
    free(mh);
#endif
}

/* ============================================================
 * 2D-Primitives mit Welt-Z (P3-P5: Stubs, werden gefuellt)
 * ============================================================ */

void moo_hybrid_rect_z(MooValue win, MooValue x, MooValue y, MooValue z, MooValue w, MooValue h, MooValue color) {
    /* TODO P3: Quad-Shader + glDrawArrays */
    (void)win; (void)x; (void)y; (void)z; (void)w; (void)h; (void)color;
}

void moo_hybrid_line_z(MooValue win, MooValue x1, MooValue y1, MooValue x2, MooValue y2, MooValue z, MooValue color) {
    /* TODO P4: GL_LINES */
    (void)win; (void)x1; (void)y1; (void)x2; (void)y2; (void)z; (void)color;
}

void moo_hybrid_circle_z(MooValue win, MooValue cx, MooValue cy, MooValue z, MooValue r, MooValue color) {
    /* TODO P4: tessellated triangle fan */
    (void)win; (void)cx; (void)cy; (void)z; (void)r; (void)color;
}

void moo_hybrid_sprite_z(MooValue win, MooValue id, MooValue x, MooValue y, MooValue z, MooValue w, MooValue h) {
    /* TODO P5: textured quad + sprite-Texture-Lookup aus moo_sprite.c */
    (void)win; (void)id; (void)x; (void)y; (void)z; (void)w; (void)h;
}

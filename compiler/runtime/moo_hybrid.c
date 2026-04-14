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
 * Farb-Helfer (Hex-String "#RRGGBB" → 0..1 RGB)
 * ============================================================ */

typedef struct { float r, g, b, a; } HybridColor;

static HybridColor parse_hybrid_color(MooValue c) {
    HybridColor col = {1.0f, 1.0f, 1.0f, 1.0f};
    if (c.tag != MOO_STRING) return col;
    const char* s = MV_STR(c)->chars;
    if (s[0] == '#' && (strlen(s) == 7 || strlen(s) == 9)) {
        unsigned int hex;
        if (sscanf(s + 1, "%06x", &hex) == 1) {
            col.r = ((hex >> 16) & 0xFF) / 255.0f;
            col.g = ((hex >> 8) & 0xFF) / 255.0f;
            col.b = (hex & 0xFF) / 255.0f;
        }
        if (strlen(s) == 9) {
            unsigned int alpha;
            if (sscanf(s + 7, "%02x", &alpha) == 1) col.a = alpha / 255.0f;
        }
        return col;
    }
    if (strcmp(s, "rot") == 0 || strcmp(s, "red") == 0) { col.r=1; col.g=0; col.b=0; }
    else if (strcmp(s, "gruen") == 0 || strcmp(s, "green") == 0) { col.r=0; col.g=1; col.b=0; }
    else if (strcmp(s, "blau") == 0 || strcmp(s, "blue") == 0) { col.r=0; col.g=0; col.b=1; }
    else if (strcmp(s, "weiss") == 0 || strcmp(s, "white") == 0) { col.r=1; col.g=1; col.b=1; }
    else if (strcmp(s, "schwarz") == 0 || strcmp(s, "black") == 0) { col.r=0; col.g=0; col.b=0; }
    else if (strcmp(s, "gelb") == 0 || strcmp(s, "yellow") == 0) { col.r=1; col.g=1; col.b=0; }
    return col;
}

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

#if MOO_HYBRID_HAS_GL
/* ============================================================
 * Quad-Shader (P3): Pixel-Coord-Quads mit Welt-Z + Color
 * Vertex-Shader: nimmt 2D-Quad in Pixel-Coords + z, mappt ueber
 *                ortho-Projektion auf NDC. z wird normalisiert auf -1..1.
 * Fragment-Shader: einheitliche Farbe (Phase P5: optional Textur)
 * ============================================================ */

static const char* HYBRID_QUAD_VS =
    "#version 330 core\n"
    "layout (location = 0) in vec2 a_pos;\n"
    "uniform vec2 u_screen;\n"      /* viewport (w,h) */
    "uniform vec4 u_rect;\n"        /* x, y, w, h (Pixel) */
    "uniform float u_z;\n"           /* Welt-Z 0..MOO_HYBRID_Z_RANGE */
    "uniform float u_z_range;\n"
    "void main() {\n"
    "    /* a_pos in 0..1 → Pixel via Rect */\n"
    "    vec2 px = u_rect.xy + a_pos * u_rect.zw;\n"
    "    /* Pixel → NDC (-1..1), y invertiert (0=oben) */\n"
    "    vec2 ndc = vec2((px.x / u_screen.x) * 2.0 - 1.0,\n"
    "                    1.0 - (px.y / u_screen.y) * 2.0);\n"
    "    /* Welt-Z 0..range → -1..1 (nahe Z=0 vorne in der Szene) */\n"
    "    float zclip = (u_z / u_z_range) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc, zclip, 1.0);\n"
    "}\n";

static const char* HYBRID_QUAD_FS =
    "#version 330 core\n"
    "uniform vec4 u_color;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    frag = u_color;\n"
    "}\n";

static unsigned int compile_shader(GLenum type, const char* src, const char* label) {
    unsigned int sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, NULL);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), NULL, log);
        fprintf(stderr, "Hybrid shader (%s) compile error: %s\n", label, log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static unsigned int build_quad_program(void) {
    unsigned int vs = compile_shader(GL_VERTEX_SHADER, HYBRID_QUAD_VS, "vs");
    unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, HYBRID_QUAD_FS, "fs");
    if (!vs || !fs) return 0;
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "Hybrid shader link error: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

static void init_quad_buffers(MooHybridWindow* mh) {
    /* Unit-Quad in 0..1 (vertex-shader skaliert auf u_rect) */
    float verts[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
    };
    glGenVertexArrays(1, &mh->quad_vao);
    glGenBuffers(1, &mh->quad_vbo);
    glBindVertexArray(mh->quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, mh->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    mh->quad_shader = build_quad_program();
}
#endif

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
    init_quad_buffers(mh);

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
#if !MOO_HYBRID_HAS_GL
    (void)win; (void)x; (void)y; (void)z; (void)w; (void)h; (void)color;
#else
    if (win.tag != MOO_WINDOW_HYBRID) return;
    MooHybridWindow* mh = (MooHybridWindow*)moo_val_as_ptr(win);
    if (!mh || !mh->open || !mh->quad_shader) return;
    glfwMakeContextCurrent(mh->window);
    HybridColor col = parse_hybrid_color(color);
    glUseProgram(mh->quad_shader);
    glUniform2f(glGetUniformLocation(mh->quad_shader, "u_screen"), (float)mh->width, (float)mh->height);
    glUniform4f(glGetUniformLocation(mh->quad_shader, "u_rect"),
        (float)MV_NUM(x), (float)MV_NUM(y), (float)MV_NUM(w), (float)MV_NUM(h));
    glUniform1f(glGetUniformLocation(mh->quad_shader, "u_z"), (float)MV_NUM(z));
    glUniform1f(glGetUniformLocation(mh->quad_shader, "u_z_range"), MOO_HYBRID_Z_RANGE);
    glUniform4f(glGetUniformLocation(mh->quad_shader, "u_color"), col.r, col.g, col.b, col.a);
    glBindVertexArray(mh->quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
#endif
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

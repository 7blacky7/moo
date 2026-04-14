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
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#define MOO_HYBRID_HAS_GL 1
#else
#define MOO_HYBRID_HAS_GL 0
#endif

extern MooValue moo_string_new(const char* s);
extern MooValue moo_bool(bool b);
extern MooValue moo_number(double n);
extern MooValue moo_none(void);
extern void moo_throw(MooValue v);

/* P6: 3D-Bridge — bei Hybrid-Window-Init binden wir den gl33-Backend an
 * unsere GLFW-Window, sodass raum_*-Calls in dieselbe Szene zeichnen. */
#ifdef MOO_HAS_GL33
extern void* gl33_init_ctx_from_window(void* win, int w, int h);
extern void moo_3d_attach_external(void* backend, void* ctx);
extern struct Moo3DBackend moo_backend_gl33;  /* deklariert in moo_3d_backend.h */
#endif

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
    /* P3: Unit-Quad fuer Rectangles */
    unsigned int quad_vao;
    unsigned int quad_vbo;
    unsigned int quad_shader;
    /* P4: Raw-2D-Pixel-Vertices fuer Lines/Circles (dynamisch) */
    unsigned int raw_vao;
    unsigned int raw_vbo;
    unsigned int raw_shader;
    /* P5: Textured-Quad fuer Sprites */
    unsigned int tex_vao;
    unsigned int tex_vbo;
    unsigned int tex_shader;
} MooHybridWindow;

static int hybrid_glfw_initialized = 0;
static int hybrid_img_initialized = 0;

/* P5: Sprite-Slot-Pool (eigener Pfad, nicht moo_sprite.c-SDL_Texture) */
#define MAX_HYBRID_SPRITES 256
typedef struct {
    unsigned int gl_tex;
    int w;
    int h;
    bool used;
} HybridSprite;
static HybridSprite g_hybrid_sprites[MAX_HYBRID_SPRITES] = {0};

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

/* Raw-2D-Shader: nimmt vec2 a_pos in PIXEL-Koordinaten + u_z + u_color.
 * Gemeinsam fuer Lines (GL_LINES) und Circle-Triangle-Fan (GL_TRIANGLE_FAN). */
static const char* HYBRID_RAW_VS =
    "#version 330 core\n"
    "layout (location = 0) in vec2 a_pos;\n"
    "uniform vec2 u_screen;\n"
    "uniform float u_z;\n"
    "uniform float u_z_range;\n"
    "void main() {\n"
    "    vec2 ndc = vec2((a_pos.x / u_screen.x) * 2.0 - 1.0,\n"
    "                    1.0 - (a_pos.y / u_screen.y) * 2.0);\n"
    "    float zclip = (u_z / u_z_range) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc, zclip, 1.0);\n"
    "}\n";

static unsigned int build_raw_program(void) {
    unsigned int vs = compile_shader(GL_VERTEX_SHADER, HYBRID_RAW_VS, "raw_vs");
    unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, HYBRID_QUAD_FS, "raw_fs");
    if (!vs || !fs) return 0;
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

static void init_raw_buffers(MooHybridWindow* mh) {
    glGenVertexArrays(1, &mh->raw_vao);
    glGenBuffers(1, &mh->raw_vbo);
    glBindVertexArray(mh->raw_vao);
    glBindBuffer(GL_ARRAY_BUFFER, mh->raw_vbo);
    /* Initial leer; pro Aufruf via glBufferData neu hochladen */
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 2 * 256, NULL, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
    mh->raw_shader = build_raw_program();
}

/* P5: Textured-Quad-Shader (Pos+UV in 0..1, Texture, Welt-Z) */
static const char* HYBRID_TEX_VS =
    "#version 330 core\n"
    "layout (location = 0) in vec2 a_pos;\n"
    "layout (location = 1) in vec2 a_uv;\n"
    "uniform vec2 u_screen;\n"
    "uniform vec4 u_rect;\n"
    "uniform float u_z;\n"
    "uniform float u_z_range;\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 px = u_rect.xy + a_pos * u_rect.zw;\n"
    "    vec2 ndc = vec2((px.x / u_screen.x) * 2.0 - 1.0,\n"
    "                    1.0 - (px.y / u_screen.y) * 2.0);\n"
    "    float zclip = (u_z / u_z_range) * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc, zclip, 1.0);\n"
    "    v_uv = a_uv;\n"
    "}\n";

static const char* HYBRID_TEX_FS =
    "#version 330 core\n"
    "in vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    frag = texture(u_tex, v_uv);\n"
    "}\n";

static void init_tex_buffers(MooHybridWindow* mh) {
    /* Quad mit UV (Pos in 0..1 + UV in 0..1; UV.y invertiert fuer SDL-Surface-Origin) */
    float verts[] = {
        /*  pos       uv   */
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f,
        0.0f, 1.0f,  0.0f, 1.0f,
    };
    glGenVertexArrays(1, &mh->tex_vao);
    glGenBuffers(1, &mh->tex_vbo);
    glBindVertexArray(mh->tex_vao);
    glBindBuffer(GL_ARRAY_BUFFER, mh->tex_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    unsigned int vs = compile_shader(GL_VERTEX_SHADER, HYBRID_TEX_VS, "tex_vs");
    unsigned int fs = compile_shader(GL_FRAGMENT_SHADER, HYBRID_TEX_FS, "tex_fs");
    if (!vs || !fs) return;
    mh->tex_shader = glCreateProgram();
    glAttachShader(mh->tex_shader, vs);
    glAttachShader(mh->tex_shader, fs);
    glLinkProgram(mh->tex_shader);
    glDeleteShader(vs);
    glDeleteShader(fs);
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
    init_raw_buffers(mh);
    init_tex_buffers(mh);

    /* P6: gl33-Backend an Hybrid-Window binden → raum_*-Calls werken auf
     * dem gleichen GL-Context und benutzen den gleichen Z-Buffer. */
    void* gl33_ctx = gl33_init_ctx_from_window(glfw_win, width, height);
    if (gl33_ctx) {
        moo_3d_attach_external(&moo_backend_gl33, gl33_ctx);
    }

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
#if !MOO_HYBRID_HAS_GL
    (void)win; (void)x1; (void)y1; (void)x2; (void)y2; (void)z; (void)color;
#else
    if (win.tag != MOO_WINDOW_HYBRID) return;
    MooHybridWindow* mh = (MooHybridWindow*)moo_val_as_ptr(win);
    if (!mh || !mh->open || !mh->raw_shader) return;
    glfwMakeContextCurrent(mh->window);
    HybridColor col = parse_hybrid_color(color);
    float verts[4] = {
        (float)MV_NUM(x1), (float)MV_NUM(y1),
        (float)MV_NUM(x2), (float)MV_NUM(y2),
    };
    glUseProgram(mh->raw_shader);
    glUniform2f(glGetUniformLocation(mh->raw_shader, "u_screen"), (float)mh->width, (float)mh->height);
    glUniform1f(glGetUniformLocation(mh->raw_shader, "u_z"), (float)MV_NUM(z));
    glUniform1f(glGetUniformLocation(mh->raw_shader, "u_z_range"), MOO_HYBRID_Z_RANGE);
    glUniform4f(glGetUniformLocation(mh->raw_shader, "u_color"), col.r, col.g, col.b, col.a);
    glBindVertexArray(mh->raw_vao);
    glBindBuffer(GL_ARRAY_BUFFER, mh->raw_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_LINES, 0, 2);
    glBindVertexArray(0);
#endif
}

void moo_hybrid_circle_z(MooValue win, MooValue cx, MooValue cy, MooValue z, MooValue r, MooValue color) {
#if !MOO_HYBRID_HAS_GL
    (void)win; (void)cx; (void)cy; (void)z; (void)r; (void)color;
#else
    if (win.tag != MOO_WINDOW_HYBRID) return;
    MooHybridWindow* mh = (MooHybridWindow*)moo_val_as_ptr(win);
    if (!mh || !mh->open || !mh->raw_shader) return;
    glfwMakeContextCurrent(mh->window);
    HybridColor col = parse_hybrid_color(color);
    /* Triangle-Fan: Mittelpunkt + 32 Randpunkte */
    int seg = 32;
    float verts[2 * (32 + 2)];
    float fcx = (float)MV_NUM(cx);
    float fcy = (float)MV_NUM(cy);
    float fr = (float)MV_NUM(r);
    verts[0] = fcx;
    verts[1] = fcy;
    for (int i = 0; i <= seg; i++) {
        float a = (float)i * 6.2831853f / (float)seg;
        verts[2 + i * 2 + 0] = fcx + cosf(a) * fr;
        verts[2 + i * 2 + 1] = fcy + sinf(a) * fr;
    }
    glUseProgram(mh->raw_shader);
    glUniform2f(glGetUniformLocation(mh->raw_shader, "u_screen"), (float)mh->width, (float)mh->height);
    glUniform1f(glGetUniformLocation(mh->raw_shader, "u_z"), (float)MV_NUM(z));
    glUniform1f(glGetUniformLocation(mh->raw_shader, "u_z_range"), MOO_HYBRID_Z_RANGE);
    glUniform4f(glGetUniformLocation(mh->raw_shader, "u_color"), col.r, col.g, col.b, col.a);
    glBindVertexArray(mh->raw_vao);
    glBindBuffer(GL_ARRAY_BUFFER, mh->raw_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
    glDrawArrays(GL_TRIANGLE_FAN, 0, seg + 2);
    glBindVertexArray(0);
#endif
}

void moo_hybrid_sprite_z(MooValue win, MooValue id, MooValue x, MooValue y, MooValue z, MooValue w, MooValue h) {
    /* TODO P5: textured quad + sprite-Texture-Lookup aus moo_sprite.c */
    (void)win; (void)id; (void)x; (void)y; (void)z; (void)w; (void)h;
}

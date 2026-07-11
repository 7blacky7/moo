/**
 * moo_3d_gl21.c — OpenGL 2.1 Immediate Mode Backend fuer moo.
 * Implementiert das Moo3DBackend Interface aus moo_3d_backend.h.
 */

#include "moo_3d_backend.h"
#include "moo_3d_math.h"
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

// === GL21 Context ===
typedef struct {
    GLFWwindow* window;
    int width;
    int height;
    double last_mouse_x;
    double last_mouse_y;
    int mouse_captured;
    double scroll_acc_x;
    double scroll_acc_y;
    /* Test-Sim */
    int sim_pos_active;
    float sim_x, sim_y;
    int sim_button[3];
    /* Tastatur-Sim Tri-State: -1=nicht simuliert, 0=up, 1=down (pro GLFW-Keycode). */
    int8_t sim_keys[GLFW_KEY_LAST + 1];
    /* Maus-Delta-Sim (consume-on-read), SEPARAT von sim_x/sim_y (Pos). */
    float sim_delta_x, sim_delta_y;
} GL21Context;

static void gl21_scroll_callback(GLFWwindow* w, double xoff, double yoff);

// === Chunk Display List System ===
#define MAX_CHUNKS 256
typedef struct {
    GLuint list_id;
    bool is_compiled;
    bool is_used;
} ChunkSlot;
static ChunkSlot g_chunks[MAX_CHUNKS];
static int g_active_chunk = -1;

static int glfw_initialized = 0;
/* Globale Transparenz — Single-Context-Annahme wie g_ctx in moo_3d.c. */
static float gl21_alpha = 1.0f;

// ============================================================
// Lifecycle
// ============================================================

static void* gl21_create_window(const char* title, int w, int h) {
    if (!glfw_initialized) {
        if (!glfwInit()) return NULL;
        glfw_initialized = 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* win = glfwCreateWindow(w, h, title, NULL, NULL);
    if (!win) return NULL;

    glfwMakeContextCurrent(win);
    glViewport(0, 0, w, h);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    /* Fixed-Function Fog */
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_EXP);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glFogf(GL_FOG_DENSITY, 0.025f);
    float fogColor[] = {0.85f, 0.87f, 0.90f, 1.0f};
    glFogfv(GL_FOG_COLOR, fogColor);

    float light_pos[] = {5.0f, 10.0f, 5.0f, 1.0f};
    float light_amb[] = {0.3f, 0.3f, 0.3f, 1.0f};
    float light_dif[] = {0.8f, 0.8f, 0.8f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, light_pos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, light_amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, light_dif);

    // Default-Perspektive
    float proj[16];
    mat4_perspective(proj, 60.0f, (float)w / (float)h, 0.1f, 100.0f);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    GL21Context* ctx = (GL21Context*)malloc(sizeof(GL21Context));
    ctx->window = win;
    ctx->width = w;
    ctx->height = h;
    ctx->last_mouse_x = 0;
    ctx->last_mouse_y = 0;
    ctx->mouse_captured = 0;
    ctx->scroll_acc_x = 0;
    ctx->scroll_acc_y = 0;
    ctx->sim_pos_active = 0;
    ctx->sim_x = ctx->sim_y = 0.0f;
    ctx->sim_button[0] = ctx->sim_button[1] = ctx->sim_button[2] = 0;
    /* Tastatur-Sim auf "nicht simuliert" (-1); Maus-Delta-Akku auf 0. */
    memset(ctx->sim_keys, -1, sizeof(ctx->sim_keys));
    ctx->sim_delta_x = ctx->sim_delta_y = 0.0f;
    glfwSetWindowUserPointer(win, ctx);
    glfwSetScrollCallback(win, gl21_scroll_callback);
    return ctx;
}

static void gl21_close(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return;
    glfwDestroyWindow(ctx->window);
    ctx->window = NULL;
}

static int gl21_is_open(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx || !ctx->window) return 0;
    return !glfwWindowShouldClose(ctx->window);
}

// ============================================================
// Frame
// ============================================================

static void gl21_clear(void* vctx, float r, float g, float b) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return;
    glfwMakeContextCurrent(ctx->window);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void gl21_swap(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return;
    glfwSwapBuffers(ctx->window);
    glfwPollEvents();
}

// ============================================================
// Kamera
// ============================================================

static void gl21_perspective(void* vctx, float fov, float near_val, float far_val) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return;
    glfwMakeContextCurrent(ctx->window);

    float m[16];
    float aspect = (float)ctx->width / (float)ctx->height;
    mat4_perspective(m, fov, aspect, near_val, far_val);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(m);
    glMatrixMode(GL_MODELVIEW);
}

static void gl21_camera(void* vctx, float ex, float ey, float ez, float lx, float ly, float lz) {
    (void)vctx;
    float m[16];
    mat4_lookat(m, ex, ey, ez, lx, ly, lz, 0.0f, 1.0f, 0.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
}

// ============================================================
// Transform
// ============================================================

static void gl21_push_matrix(void* vctx) {
    (void)vctx;
    glPushMatrix();
}

static void gl21_pop_matrix(void* vctx) {
    (void)vctx;
    glPopMatrix();
}

static void gl21_translate(void* vctx, float x, float y, float z) {
    (void)vctx;
    glTranslatef(x, y, z);
}

static void gl21_rotate(void* vctx, float angle, float ax, float ay, float az) {
    (void)vctx;
    glRotatef(angle, ax, ay, az);
}

// ============================================================
// Zeichnen
// ============================================================

static void gl21_cube(void* vctx, float x, float y, float z, float size, float r, float g, float b) {
    (void)vctx;
    glColor4f(r, g, b, gl21_alpha);
    float s = size / 2.0f;

    glBegin(GL_QUADS);
    // Vorne
    glNormal3f(0, 0, 1);
    glVertex3f(x-s, y-s, z+s); glVertex3f(x+s, y-s, z+s);
    glVertex3f(x+s, y+s, z+s); glVertex3f(x-s, y+s, z+s);
    // Hinten
    glNormal3f(0, 0, -1);
    glVertex3f(x+s, y-s, z-s); glVertex3f(x-s, y-s, z-s);
    glVertex3f(x-s, y+s, z-s); glVertex3f(x+s, y+s, z-s);
    // Oben
    glNormal3f(0, 1, 0);
    glVertex3f(x-s, y+s, z+s); glVertex3f(x+s, y+s, z+s);
    glVertex3f(x+s, y+s, z-s); glVertex3f(x-s, y+s, z-s);
    // Unten
    glNormal3f(0, -1, 0);
    glVertex3f(x-s, y-s, z-s); glVertex3f(x+s, y-s, z-s);
    glVertex3f(x+s, y-s, z+s); glVertex3f(x-s, y-s, z+s);
    // Rechts
    glNormal3f(1, 0, 0);
    glVertex3f(x+s, y-s, z+s); glVertex3f(x+s, y-s, z-s);
    glVertex3f(x+s, y+s, z-s); glVertex3f(x+s, y+s, z+s);
    // Links
    glNormal3f(-1, 0, 0);
    glVertex3f(x-s, y-s, z-s); glVertex3f(x-s, y-s, z+s);
    glVertex3f(x-s, y+s, z+s); glVertex3f(x-s, y+s, z-s);
    glEnd();
}

static void gl21_sphere(void* vctx, float x, float y, float z, float radius, float r, float g, float b, int detail) {
    (void)vctx;
    glColor4f(r, g, b, gl21_alpha);

    int slices = detail;
    if (slices < 4) slices = 4;
    if (slices > 64) slices = 64;
    int stacks = slices;

    for (int i = 0; i < stacks; i++) {
        float lat0 = (float)M_PI * (-0.5f + (float)i / stacks);
        float lat1 = (float)M_PI * (-0.5f + (float)(i + 1) / stacks);
        float y0 = sinf(lat0), yr0 = cosf(lat0);
        float y1 = sinf(lat1), yr1 = cosf(lat1);

        glBegin(GL_TRIANGLE_STRIP);
        for (int j = 0; j <= slices; j++) {
            float lng = 2.0f * (float)M_PI * (float)j / slices;
            float xn = cosf(lng), zn = sinf(lng);

            glNormal3f(xn * yr1, y1, zn * yr1);
            glVertex3f(x + radius * xn * yr1, y + radius * y1, z + radius * zn * yr1);
            glNormal3f(xn * yr0, y0, zn * yr0);
            glVertex3f(x + radius * xn * yr0, y + radius * y0, z + radius * zn * yr0);
        }
        glEnd();
    }
}

static void gl21_triangle_colors(void* vctx,
                                 float x1, float y1, float z1, float r1, float g1, float b1,
                                 float x2, float y2, float z2, float r2, float g2, float b2,
                                 float x3, float y3, float z3, float r3, float g3, float b3) {
    (void)vctx;

    float ax = x2 - x1, ay = y2 - y1, az = z2 - z1;
    float bx = x3 - x1, by = y3 - y1, bz = z3 - z1;
    float nx = ay*bz - az*by;
    float ny = az*bx - ax*bz;
    float nz = ax*by - ay*bx;
    float len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len > 0) { nx /= len; ny /= len; nz /= len; }

    glBegin(GL_TRIANGLES);
    glNormal3f(nx, ny, nz);
    glColor4f(r1, g1, b1, gl21_alpha);
    glVertex3f(x1, y1, z1);
    glColor4f(r2, g2, b2, gl21_alpha);
    glVertex3f(x2, y2, z2);
    glColor4f(r3, g3, b3, gl21_alpha);
    glVertex3f(x3, y3, z3);
    glEnd();
}

static void gl21_triangle(void* vctx, float x1, float y1, float z1,
                           float x2, float y2, float z2,
                           float x3, float y3, float z3,
                           float r, float g, float b) {
    (void)vctx;
    glColor4f(r, g, b, gl21_alpha);

    // Normale berechnen
    float ax = x2 - x1, ay = y2 - y1, az = z2 - z1;
    float bx = x3 - x1, by = y3 - y1, bz = z3 - z1;
    float nx = ay*bz - az*by;
    float ny = az*bx - ax*bz;
    float nz = ax*by - ay*bx;
    float len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len > 0) { nx /= len; ny /= len; nz /= len; }

    glBegin(GL_TRIANGLES);
    glNormal3f(nx, ny, nz);
    glVertex3f(x1, y1, z1);
    glVertex3f(x2, y2, z2);
    glVertex3f(x3, y3, z3);
    glEnd();
}

/* Tasten-String -> GLFW-Keycode. Gemeinsame Konvention fuer key_pressed UND
 * simulate_key, damit der Tri-State konsistent indexiert wird. 0 = unbekannt. */
static int gl21_keycode(const char* name) {
    if (!name) return 0;
    int glfw_key = 0;
    if (strcmp(name, "oben") == 0 || strcmp(name, "up") == 0) glfw_key = GLFW_KEY_UP;
    else if (strcmp(name, "unten") == 0 || strcmp(name, "down") == 0) glfw_key = GLFW_KEY_DOWN;
    else if (strcmp(name, "links") == 0 || strcmp(name, "left") == 0) glfw_key = GLFW_KEY_LEFT;
    else if (strcmp(name, "rechts") == 0 || strcmp(name, "right") == 0) glfw_key = GLFW_KEY_RIGHT;
    else if (strcmp(name, "leertaste") == 0 || strcmp(name, "space") == 0) glfw_key = GLFW_KEY_SPACE;
    else if (strcmp(name, "escape") == 0) glfw_key = GLFW_KEY_ESCAPE;
    else if (strcmp(name, "shift") == 0) glfw_key = GLFW_KEY_LEFT_SHIFT;
    else if (strlen(name) == 1 && name[0] >= 'a' && name[0] <= 'z')
        glfw_key = GLFW_KEY_A + (name[0] - 'a');
    return glfw_key;
}

static int gl21_key_pressed(void* vctx, const char* name) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx || !name) return 0;

    int glfw_key = gl21_keycode(name);
    if (glfw_key <= 0 || glfw_key > GLFW_KEY_LAST) return 0;

    /* Tri-State: Sim-State ZUERST. -1 = nicht simuliert -> echter Input. */
    if (ctx->sim_keys[glfw_key] >= 0) return ctx->sim_keys[glfw_key];

    if (!ctx->window) return 0;
    return glfwGetKey(ctx->window, glfw_key) == GLFW_PRESS;
}

// ============================================================
// Chunks (Display Lists)
// ============================================================

static int gl21_chunk_create(void* vctx) {
    (void)vctx;
    for (int i = 0; i < MAX_CHUNKS; i++) {
        if (!g_chunks[i].is_used) {
            GLuint id = glGenLists(1);
            if (id == 0) return -1;
            g_chunks[i].list_id = id;
            g_chunks[i].is_compiled = false;
            g_chunks[i].is_used = true;
            return i;
        }
    }
    return -1;
}

static void gl21_chunk_begin(void* vctx, int id) {
    (void)vctx;
    if (id < 0 || id >= MAX_CHUNKS || !g_chunks[id].is_used) return;
    if (g_active_chunk != -1) return;
    glNewList(g_chunks[id].list_id, GL_COMPILE);
    g_active_chunk = id;
}

static void gl21_chunk_end(void* vctx) {
    (void)vctx;
    if (g_active_chunk == -1) return;
    glEndList();
    g_chunks[g_active_chunk].is_compiled = true;
    g_active_chunk = -1;
}

static void gl21_chunk_draw(void* vctx, int id) {
    (void)vctx;
    if (id < 0 || id >= MAX_CHUNKS || !g_chunks[id].is_used || !g_chunks[id].is_compiled)
        return;
    glCallList(g_chunks[id].list_id);
}

static void gl21_chunk_delete(void* vctx, int id) {
    (void)vctx;
    if (id < 0 || id >= MAX_CHUNKS || !g_chunks[id].is_used) return;
    glDeleteLists(g_chunks[id].list_id, 1);
    g_chunks[id].is_compiled = false;
    g_chunks[id].is_used = false;
}

// ============================================================
// Maus
// ============================================================

static void gl21_capture_mouse(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx || !ctx->window) return;
    glfwSetInputMode(ctx->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    /* Raw-Mouse-Motion: umgeht Desktop-Maus-Beschleunigung/Pointer-Ballistik
       (sprunghaftes Look unter XWayland/KWin + NVIDIA). Fallback: wenn die
       Plattform es nicht unterstuetzt (glfwRawMouseMotionSupported() == false),
       bleibt es beim normalen CURSOR_DISABLED-Verhalten — kein Fehler. */
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(ctx->window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwGetCursorPos(ctx->window, &ctx->last_mouse_x, &ctx->last_mouse_y);
    ctx->mouse_captured = 1;
}

static float gl21_mouse_dx(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return 0.0f;
    /* Sim-Delta zuerst (consume-on-read), wie der Wheel-Akkumulator. */
    if (ctx->sim_delta_x != 0.0f) {
        float v = ctx->sim_delta_x;
        ctx->sim_delta_x = 0.0f;
        return v;
    }
    if (!ctx->window || !ctx->mouse_captured) return 0.0f;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    float dx = (float)(cx - ctx->last_mouse_x);
    ctx->last_mouse_x = cx;
    return dx;
}

static float gl21_mouse_dy(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return 0.0f;
    if (ctx->sim_delta_y != 0.0f) {
        float v = ctx->sim_delta_y;
        ctx->sim_delta_y = 0.0f;
        return v;
    }
    if (!ctx->window || !ctx->mouse_captured) return 0.0f;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    float dy = (float)(cy - ctx->last_mouse_y);
    ctx->last_mouse_y = cy;
    return dy;
}

static void gl21_release_mouse(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx || !ctx->window) return;
    glfwSetInputMode(ctx->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    ctx->mouse_captured = 0;
}

static float gl21_mouse_x(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx || !ctx->window) return 0.0f;
    if (ctx->sim_pos_active) return ctx->sim_x;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    return (float)cx;
}

static float gl21_mouse_y(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx || !ctx->window) return 0.0f;
    if (ctx->sim_pos_active) return ctx->sim_y;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    return (float)cy;
}

static int gl21_mouse_button(void* vctx, int btn) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx || !ctx->window) return 0;
    if (btn >= 0 && btn < 3 && ctx->sim_button[btn]) return 1;
    int glfw_btn = (btn == 0) ? GLFW_MOUSE_BUTTON_LEFT
                  : (btn == 1) ? GLFW_MOUSE_BUTTON_RIGHT
                  : GLFW_MOUSE_BUTTON_MIDDLE;
    return glfwGetMouseButton(ctx->window, glfw_btn) == GLFW_PRESS ? 1 : 0;
}

static void gl21_simulate_mouse_pos(void* vctx, float x, float y) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return;
    ctx->sim_pos_active = 1;
    ctx->sim_x = x;
    ctx->sim_y = y;
}

static void gl21_simulate_mouse_button(void* vctx, int btn, int pressed) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx || btn < 0 || btn >= 3) return;
    ctx->sim_button[btn] = pressed ? 1 : 0;
}

static void gl21_simulate_scroll(void* vctx, float dy) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return;
    ctx->scroll_acc_y += dy;
}

static void gl21_simulate_key(void* vctx, const char* key, int pressed) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return;
    int glfw_key = gl21_keycode(key);
    if (glfw_key <= 0 || glfw_key > GLFW_KEY_LAST) return;
    ctx->sim_keys[glfw_key] = pressed ? 1 : 0;
}

static void gl21_simulate_mouse_delta(void* vctx, float dx, float dy) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return;
    ctx->sim_delta_x += dx;
    ctx->sim_delta_y += dy;
}

static void gl21_simulate_reset(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return;
    memset(ctx->sim_keys, -1, sizeof(ctx->sim_keys));
    ctx->sim_delta_x = 0.0f;
    ctx->sim_delta_y = 0.0f;
    ctx->sim_pos_active = 0;
    ctx->sim_button[0] = ctx->sim_button[1] = ctx->sim_button[2] = 0;
    ctx->scroll_acc_y = 0;
}

static float gl21_mouse_wheel(void* vctx) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx) return 0.0f;
    float v = (float)ctx->scroll_acc_y;
    ctx->scroll_acc_y = 0;
    return v;
}

/* gl21 hat noch keinen Screenshot-Helper — Stub (test_screenshot wirft fuer
 * gl21 bewusst, siehe moo_test_api.c S5). Frame-Grab geht aber via glReadPixels
 * problemlos, daher unten gl21_grab_rgba (keine Throw-Notwendigkeit). */
static int gl21_screenshot_bmp(void* vctx, const char* path) {
    (void)vctx; (void)path;
    return 0;
}

/* Frame-Grab (Plan-008 A3A): aktueller Framebuffer als RGBA8, top-left origin.
 * gl21 nutzt eine GLFW-GL-Window wie gl33 -> glReadPixels (bottom-left) +
 * einheitlicher Y-Flip. Buffer via malloc, Aufrufer free; NULL bei Fehler. */
static uint8_t* gl21_grab_rgba(void* vctx, int* out_w, int* out_h) {
    GL21Context* ctx = (GL21Context*)vctx;
    if (!ctx || !ctx->window) return NULL;
    glfwMakeContextCurrent(ctx->window);
    int w = ctx->width;
    int h = ctx->height;
    if (w <= 0 || h <= 0) return NULL;
    uint8_t* buf = (uint8_t*)malloc((size_t)w * (size_t)h * 4);
    if (!buf) return NULL;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    uint8_t* row = (uint8_t*)malloc((size_t)w * 4);
    if (row) {
        for (int y = 0; y < h / 2; y++) {
            uint8_t* a = buf + (size_t)y * (size_t)w * 4;
            uint8_t* b = buf + (size_t)(h - 1 - y) * (size_t)w * 4;
            memcpy(row, a, (size_t)w * 4);
            memcpy(a, b, (size_t)w * 4);
            memcpy(b, row, (size_t)w * 4);
        }
        free(row);
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return buf;
}

static void gl21_scroll_callback(GLFWwindow* w, double xoff, double yoff) {
    GL21Context* ctx = (GL21Context*)glfwGetWindowUserPointer(w);
    if (!ctx) return;
    ctx->scroll_acc_x += xoff;
    ctx->scroll_acc_y += yoff;
}

// ============================================================
// Fog + Licht
// ============================================================

static void gl21_set_fog_density(void* vctx) {
    /* GL21 nutzt glFog — density wird als float Parameter übergeben */
}
static void gl21_set_fog_density_f(void* vctx, float density) {
    (void)vctx;
    glFogf(GL_FOG_DENSITY, density);
}
static void gl21_set_alpha(void* vctx, float level) {
    (void)vctx;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    gl21_alpha = level;
    glDepthMask(level >= 0.999f ? GL_TRUE : GL_FALSE);
}

static void gl21_set_fog_color(void* vctx, float r, float g, float b) {
    (void)vctx;
    float c[4] = { r, g, b, 1.0f };
    glFogfv(GL_FOG_COLOR, c);
}
static void gl21_set_light_dir(void* vctx, float x, float y, float z) {
    (void)vctx;
    float pos[] = {x, y, z, 0.0f}; /* w=0 → directional light */
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    /* Diffuse-Farbe basierend auf Sonnenhöhe: hoch=weiss, tief=orange/rot */
    float intensity = y > 0.0f ? y : 0.0f;
    float dif_r = 0.3f + 0.7f * intensity;
    float dif_g = 0.2f + 0.6f * intensity;
    float dif_b = 0.1f + 0.7f * intensity;
    float dif[] = {dif_r, dif_g, dif_b, 1.0f};
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
}
static void gl21_set_ambient(void* vctx, float level) {
    (void)vctx;
    float amb[] = {level, level, level, 1.0f};
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
}

// ============================================================
// Backend-Struct Export
// ============================================================

Moo3DBackend moo_backend_gl21 = {
    .create_window = gl21_create_window,
    .close         = gl21_close,
    .is_open       = gl21_is_open,
    .clear         = gl21_clear,
    .swap          = gl21_swap,
    .perspective   = gl21_perspective,
    .camera        = gl21_camera,
    .push_matrix   = gl21_push_matrix,
    .pop_matrix    = gl21_pop_matrix,
    .translate     = gl21_translate,
    .rotate        = gl21_rotate,
    .cube          = gl21_cube,
    .sphere        = gl21_sphere,
    .triangle      = gl21_triangle,
    .triangle_colors = gl21_triangle_colors,
    .key_pressed   = gl21_key_pressed,
    .capture_mouse = gl21_capture_mouse,
    .release_mouse = gl21_release_mouse,
    .mouse_dx      = gl21_mouse_dx,
    .mouse_dy      = gl21_mouse_dy,
    .mouse_x       = gl21_mouse_x,
    .mouse_y       = gl21_mouse_y,
    .mouse_button  = gl21_mouse_button,
    .mouse_wheel   = gl21_mouse_wheel,
    .set_fog_density = gl21_set_fog_density_f,
    .set_fog_color   = gl21_set_fog_color,
    .set_alpha       = gl21_set_alpha,
    .set_light_dir   = gl21_set_light_dir,
    .set_ambient     = gl21_set_ambient,
    .chunk_create  = gl21_chunk_create,
    .chunk_begin   = gl21_chunk_begin,
    .chunk_end     = gl21_chunk_end,
    .chunk_draw    = gl21_chunk_draw,
    .chunk_delete  = gl21_chunk_delete,
    .screenshot_bmp = gl21_screenshot_bmp,
    .grab_rgba      = gl21_grab_rgba,
    .simulate_mouse_pos = gl21_simulate_mouse_pos,
    .simulate_mouse_button = gl21_simulate_mouse_button,
    .simulate_scroll = gl21_simulate_scroll,
    .simulate_key = gl21_simulate_key,
    .simulate_mouse_delta = gl21_simulate_mouse_delta,
    .simulate_reset = gl21_simulate_reset,
};

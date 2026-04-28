/**
 * moo_3d_gl33.c — OpenGL 3.3 Core Profile Backend fuer moo.
 * Implementiert Moo3DBackend mit VBOs, Shadern, Fog und Lighting.
 * Integriert: Shader (gl33_shaders), Mesh (gl33_mesh), Math (3d_math).
 */

#include "moo_3d_backend.h"
#include "moo_3d_math.h"
#include "moo_3d_gl33_shaders.h"
#include "moo_3d_gl33_mesh.h"

#include "glad/include/glad/glad.h"
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

/* ========================================================
 * GL33 Context
 * ======================================================== */

typedef struct {
    GLFWwindow* window;
    int width;
    int height;
    /* Shader */
    GLuint program;
    GL33Uniforms uniforms;
    /* Matrices */
    float projection[16];
    MooMatrixStack modelview;
    /* Chunks */
    GL33Chunk chunks[MAX_GL33_CHUNKS];
    /* Active chunk building */
    int active_chunk;
    MeshBuilder builder;
    /* Immediate-mode fallback VAO/VBO for non-chunked draws */
    GLuint imm_vao;
    GLuint imm_vbo;
    /* Light + Fog defaults */
    float light_dir[3];
    float fog_color[3];
    float fog_dist;
    double last_mouse_x;
    double last_mouse_y;
    int mouse_captured;
    double scroll_acc_x;
    double scroll_acc_y;
} GL33Context;

static void gl33_scroll_callback(GLFWwindow* w, double xoff, double yoff);

/* ========================================================
 * Helpers
 * ======================================================== */

static void gl33_update_mvp(GL33Context* ctx) {
    float mvp[16];
    mat4_multiply(mvp, ctx->projection, ctx->modelview.current);
    glUseProgram(ctx->program);
    gl33_upload_matrix(ctx->uniforms.mvp, mvp);
    gl33_upload_matrix(ctx->uniforms.model, ctx->modelview.current);
}

static void gl33_upload_lighting(GL33Context* ctx) {
    glUseProgram(ctx->program);
    gl33_upload_vec3(ctx->uniforms.light_dir,
                     ctx->light_dir[0], ctx->light_dir[1], ctx->light_dir[2]);
    gl33_upload_vec3(ctx->uniforms.fog_color,
                     ctx->fog_color[0], ctx->fog_color[1], ctx->fog_color[2]);
    gl33_upload_float(ctx->uniforms.fog_dist, ctx->fog_dist);
}

/* Draw vertices immediately (non-chunked) */
static void gl33_draw_immediate(GL33Context* ctx, MeshBuilder* mb) {
    gl33_update_mvp(ctx);

    glBindVertexArray(ctx->imm_vao);
    glBindBuffer(GL_ARRAY_BUFFER, ctx->imm_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 mb->count * (int)sizeof(MooVertex),
                 mb->vertices, GL_STREAM_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          MOO_VERTEX_STRIDE, (void*)(size_t)MOO_VERTEX_POS_OFFSET);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          MOO_VERTEX_STRIDE, (void*)(size_t)MOO_VERTEX_COLOR_OFFSET);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE,
                          MOO_VERTEX_STRIDE, (void*)(size_t)MOO_VERTEX_NORMAL_OFFSET);

    glDrawArrays(GL_TRIANGLES, 0, mb->count);
    glBindVertexArray(0);
}

/* ========================================================
 * Backend Functions — Lifecycle
 * ======================================================== */

void* gl33_init_ctx_from_window(void* win_void, int w, int h);  /* P6 forward */

static void* gl33_create_window(const char* title, int w, int h) {
    if (!glfwInit()) {
        fprintf(stderr, "moo GL33: glfwInit fehlgeschlagen\n");
        return NULL;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* win = glfwCreateWindow(w, h, title, NULL, NULL);
    if (!win) {
        fprintf(stderr, "moo GL33: Fenster konnte nicht erstellt werden\n");
        return NULL;
    }
    glfwMakeContextCurrent(win);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "moo GL33: GLAD Init fehlgeschlagen\n");
        glfwDestroyWindow(win);
        return NULL;
    }

    return gl33_init_ctx_from_window(win, w, h);
}

/* P6: extern callable — Initialisiert GL33Context auf einem bereits
 * existierenden GLFWwindow (z.B. dem Hybrid-Renderer-Fenster). Erlaubt
 * dass raum_*-Calls in dieselbe Szene zeichnen wie hybrid_*. */
void* gl33_init_ctx_from_window(void* win_void, int w, int h) {
    GLFWwindow* win = (GLFWwindow*)win_void;

    GL33Context* ctx = (GL33Context*)calloc(1, sizeof(GL33Context));
    ctx->window = win;
    ctx->width = w;
    ctx->height = h;

    /* Shader */
    ctx->program = gl33_create_program();
    if (!ctx->program) {
        fprintf(stderr, "moo GL33: Shader-Programm fehlgeschlagen\n");
        free(ctx);
        glfwDestroyWindow(win);
        return NULL;
    }
    ctx->uniforms = gl33_get_uniforms(ctx->program);

    /* Matrices */
    mat4_identity(ctx->projection);
    moo_matrix_stack_init(&ctx->modelview);

    /* Chunks */
    gl33_chunk_system_init(ctx->chunks);
    ctx->active_chunk = -1;

    /* Mouse + Scroll */
    ctx->scroll_acc_x = 0;
    ctx->scroll_acc_y = 0;
    glfwSetWindowUserPointer(ctx->window, ctx);
    glfwSetScrollCallback(ctx->window, gl33_scroll_callback);
    mesh_builder_init(&ctx->builder);

    /* Immediate-mode fallback VAO/VBO */
    glGenVertexArrays(1, &ctx->imm_vao);
    glGenBuffers(1, &ctx->imm_vbo);

    /* Default light + fog */
    ctx->light_dir[0] = -0.7f;
    ctx->light_dir[1] = 0.4f;
    ctx->light_dir[2] = 0.5f;
    // Horizont-Farbe: Passt zu welten.moo Clear-Color (0.53, 0.81, 0.92)
    ctx->fog_color[0] = 0.85f;
    ctx->fog_color[1] = 0.87f;
    ctx->fog_color[2] = 0.90f;
    ctx->fog_dist = 20.0f;

    /* GL State */
    glEnable(GL_DEPTH_TEST);
    glViewport(0, 0, w, h);

    glUseProgram(ctx->program);
    gl33_upload_lighting(ctx);

    return ctx;
}

static void gl33_close(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;

    /* Cleanup chunks */
    for (int i = 0; i < MAX_GL33_CHUNKS; i++)
        gl33_chunk_delete(ctx->chunks, i);

    mesh_builder_free(&ctx->builder);

    if (ctx->imm_vbo) glDeleteBuffers(1, &ctx->imm_vbo);
    if (ctx->imm_vao) glDeleteVertexArrays(1, &ctx->imm_vao);
    if (ctx->program) glDeleteProgram(ctx->program);
    if (ctx->window)  glfwDestroyWindow(ctx->window);

    free(ctx);
}

static int gl33_is_open(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window) return 0;
    return !glfwWindowShouldClose(ctx->window);
}

/* ========================================================
 * Backend Functions — Frame
 * ======================================================== */

static void gl33_clear(void* vctx, float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    glfwMakeContextCurrent(ctx->window);
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    /* Reset modelview to identity each frame */
    moo_matrix_stack_init(&ctx->modelview);
}

static void gl33_swap(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    glfwSwapBuffers(ctx->window);
    glfwPollEvents();
}

/* ========================================================
 * Backend Functions — Kamera
 * ======================================================== */

static void gl33_perspective(void* vctx, float fov, float near_val, float far_val) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    float aspect = (float)ctx->width / (float)ctx->height;
    mat4_perspective(ctx->projection, fov, aspect, near_val, far_val);
}

static void gl33_camera(void* vctx,
                        float ex, float ey, float ez,
                        float lx, float ly, float lz) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    mat4_lookat(ctx->modelview.current, ex, ey, ez, lx, ly, lz, 0, 1, 0);
}

/* ========================================================
 * Backend Functions — Transform
 * ======================================================== */

static void gl33_push_matrix(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (ctx) moo_matrix_stack_push(&ctx->modelview);
}

static void gl33_pop_matrix(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (ctx) moo_matrix_stack_pop(&ctx->modelview);
}

static void gl33_translate(void* vctx, float x, float y, float z) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (ctx) mat4_translate(ctx->modelview.current, x, y, z);
}

static void gl33_rotate(void* vctx, float angle, float ax, float ay, float az) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (ctx) mat4_rotate(ctx->modelview.current, angle, ax, ay, az);
}

/* ========================================================
 * Backend Functions — Zeichnen
 * ======================================================== */

static void gl33_cube(void* vctx,
                      float x, float y, float z, float size,
                      float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;

    if (ctx->active_chunk >= 0) {
        /* Building a chunk — just collect vertices */
        mesh_builder_add_cube(&ctx->builder, x, y, z, size, r, g, b);
        return;
    }

    /* Immediate draw */
    MeshBuilder tmp;
    mesh_builder_init(&tmp);
    mesh_builder_add_cube(&tmp, x, y, z, size, r, g, b);
    gl33_draw_immediate(ctx, &tmp);
    mesh_builder_free(&tmp);
}

static void gl33_sphere(void* vctx,
                        float x, float y, float z, float radius,
                        float r, float g, float b, int detail) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;

    int slices = detail > 0 ? detail : 16;
    int stacks = slices;

    MeshBuilder* mb;
    MeshBuilder tmp;
    if (ctx->active_chunk >= 0) {
        mb = &ctx->builder;
    } else {
        mesh_builder_init(&tmp);
        mb = &tmp;
    }

    for (int i = 0; i < stacks; i++) {
        float theta1 = (float)i / stacks * (float)M_PI;
        float theta2 = (float)(i + 1) / stacks * (float)M_PI;
        for (int j = 0; j < slices; j++) {
            float phi1 = (float)j / slices * 2.0f * (float)M_PI;
            float phi2 = (float)(j + 1) / slices * 2.0f * (float)M_PI;

            float p[4][3], n[4][3];
            float st[] = {sinf(theta1), sinf(theta2)};
            float ct[] = {cosf(theta1), cosf(theta2)};
            float sp[] = {sinf(phi1), sinf(phi2)};
            float cp[] = {cosf(phi1), cosf(phi2)};

            n[0][0] = st[0]*cp[0]; n[0][1] = ct[0]; n[0][2] = st[0]*sp[0];
            n[1][0] = st[0]*cp[1]; n[1][1] = ct[0]; n[1][2] = st[0]*sp[1];
            n[2][0] = st[1]*cp[1]; n[2][1] = ct[1]; n[2][2] = st[1]*sp[1];
            n[3][0] = st[1]*cp[0]; n[3][1] = ct[1]; n[3][2] = st[1]*sp[0];

            for (int k = 0; k < 4; k++) {
                p[k][0] = x + radius * n[k][0];
                p[k][1] = y + radius * n[k][1];
                p[k][2] = z + radius * n[k][2];
            }

            /* 2 triangles per quad */
            mesh_builder_add_vertex(mb, p[0][0],p[0][1],p[0][2], r,g,b, n[0][0],n[0][1],n[0][2]);
            mesh_builder_add_vertex(mb, p[1][0],p[1][1],p[1][2], r,g,b, n[1][0],n[1][1],n[1][2]);
            mesh_builder_add_vertex(mb, p[2][0],p[2][1],p[2][2], r,g,b, n[2][0],n[2][1],n[2][2]);
            mesh_builder_add_vertex(mb, p[0][0],p[0][1],p[0][2], r,g,b, n[0][0],n[0][1],n[0][2]);
            mesh_builder_add_vertex(mb, p[2][0],p[2][1],p[2][2], r,g,b, n[2][0],n[2][1],n[2][2]);
            mesh_builder_add_vertex(mb, p[3][0],p[3][1],p[3][2], r,g,b, n[3][0],n[3][1],n[3][2]);
        }
    }

    if (ctx->active_chunk < 0) {
        gl33_draw_immediate(ctx, mb);
        mesh_builder_free(&tmp);
    }
}

static void gl33_triangle(void* vctx,
                          float x1, float y1, float z1,
                          float x2, float y2, float z2,
                          float x3, float y3, float z3,
                          float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;

    /* Compute face normal */
    float e1x = x2-x1, e1y = y2-y1, e1z = z2-z1;
    float e2x = x3-x1, e2y = y3-y1, e2z = z3-z1;
    float nx = e1y*e2z - e1z*e2y;
    float ny = e1z*e2x - e1x*e2z;
    float nz = e1x*e2y - e1y*e2x;
    float len = sqrtf(nx*nx + ny*ny + nz*nz);
    if (len > 0) { nx /= len; ny /= len; nz /= len; }

    MeshBuilder* mb;
    MeshBuilder tmp;
    if (ctx->active_chunk >= 0) {
        mb = &ctx->builder;
    } else {
        mesh_builder_init(&tmp);
        mb = &tmp;
    }

    mesh_builder_add_vertex(mb, x1, y1, z1, r, g, b, nx, ny, nz);
    mesh_builder_add_vertex(mb, x2, y2, z2, r, g, b, nx, ny, nz);
    mesh_builder_add_vertex(mb, x3, y3, z3, r, g, b, nx, ny, nz);

    if (ctx->active_chunk < 0) {
        gl33_draw_immediate(ctx, mb);
        mesh_builder_free(&tmp);
    }
}

/* ========================================================
 * Backend Functions — Input
 * ======================================================== */

static int gl33_key_pressed(void* vctx, const char* key) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window || !key) return 0;

    int glfw_key = 0;
    if (key[1] == '\0') {
        char c = key[0];
        if (c >= 'a' && c <= 'z') glfw_key = GLFW_KEY_A + (c - 'a');
        else if (c >= 'A' && c <= 'Z') glfw_key = GLFW_KEY_A + (c - 'A');
        else if (c >= '0' && c <= '9') glfw_key = GLFW_KEY_0 + (c - '0');
        else if (c == ' ') glfw_key = GLFW_KEY_SPACE;
    } else {
        if (strcmp(key, "hoch") == 0 || strcmp(key, "up") == 0) glfw_key = GLFW_KEY_UP;
        else if (strcmp(key, "runter") == 0 || strcmp(key, "down") == 0) glfw_key = GLFW_KEY_DOWN;
        else if (strcmp(key, "links") == 0 || strcmp(key, "left") == 0) glfw_key = GLFW_KEY_LEFT;
        else if (strcmp(key, "rechts") == 0 || strcmp(key, "right") == 0) glfw_key = GLFW_KEY_RIGHT;
        else if (strcmp(key, "leer") == 0 || strcmp(key, "space") == 0) glfw_key = GLFW_KEY_SPACE;
        else if (strcmp(key, "enter") == 0) glfw_key = GLFW_KEY_ENTER;
        else if (strcmp(key, "esc") == 0 || strcmp(key, "escape") == 0) glfw_key = GLFW_KEY_ESCAPE;
        else if (strcmp(key, "shift") == 0) glfw_key = GLFW_KEY_LEFT_SHIFT;
    }

    if (glfw_key == 0) return 0;
    return glfwGetKey(ctx->window, glfw_key) == GLFW_PRESS;
}

/* ========================================================
 * Backend Functions — Chunks (VBO-cached)
 * ======================================================== */

static int gl33_chunk_create(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return -1;
    return gl33_chunk_alloc(ctx->chunks);
}

static void gl33_chunk_begin(void* vctx, int id) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || id < 0 || id >= MAX_GL33_CHUNKS) return;
    if (ctx->active_chunk >= 0) {
        fprintf(stderr, "moo GL33: chunk_begin verschachtelt (aktiv: %d)\n", ctx->active_chunk);
        return;
    }
    ctx->active_chunk = id;
    mesh_builder_reset(&ctx->builder);
}

static void gl33_chunk_end(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || ctx->active_chunk < 0) return;

    gl33_chunk_upload(ctx->chunks, ctx->active_chunk, &ctx->builder);
    ctx->active_chunk = -1;
}

static void gl33_chunk_draw_fn(void* vctx, int id) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;

    gl33_update_mvp(ctx);
    gl33_chunk_draw(ctx->chunks, id);
}

static void gl33_chunk_delete_fn(void* vctx, int id) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    gl33_chunk_delete(ctx->chunks, id);
}

/* ========================================================
 * Maus
 * ======================================================== */

static void gl33_capture_mouse(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window) return;
    glfwSetInputMode(ctx->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwGetCursorPos(ctx->window, &ctx->last_mouse_x, &ctx->last_mouse_y);
    ctx->mouse_captured = 1;
}

static float gl33_mouse_dx(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window || !ctx->mouse_captured) return 0.0f;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    float dx = (float)(cx - ctx->last_mouse_x);
    ctx->last_mouse_x = cx;
    return dx;
}

static float gl33_mouse_dy(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window || !ctx->mouse_captured) return 0.0f;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    float dy = (float)(cy - ctx->last_mouse_y);
    ctx->last_mouse_y = cy;
    return dy;
}

static void gl33_release_mouse(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window) return;
    glfwSetInputMode(ctx->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    ctx->mouse_captured = 0;
}

static float gl33_mouse_x(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window) return 0.0f;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    return (float)cx;
}

static float gl33_mouse_y(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window) return 0.0f;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    return (float)cy;
}

static int gl33_mouse_button(void* vctx, int btn) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window) return 0;
    int glfw_btn = (btn == 0) ? GLFW_MOUSE_BUTTON_LEFT
                  : (btn == 1) ? GLFW_MOUSE_BUTTON_RIGHT
                  : GLFW_MOUSE_BUTTON_MIDDLE;
    return glfwGetMouseButton(ctx->window, glfw_btn) == GLFW_PRESS ? 1 : 0;
}

static float gl33_mouse_wheel(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return 0.0f;
    float v = (float)ctx->scroll_acc_y;
    ctx->scroll_acc_y = 0;
    return v;
}

static void gl33_scroll_callback(GLFWwindow* w, double xoff, double yoff) {
    GL33Context* ctx = (GL33Context*)glfwGetWindowUserPointer(w);
    if (!ctx) return;
    ctx->scroll_acc_x += xoff;
    ctx->scroll_acc_y += yoff;
}

/* ========================================================
 * Fog + Licht
 * ======================================================== */

static void gl33_set_fog_density(void* vctx, float density) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->fog_dist = 1.0f / (density > 0.001f ? density : 0.001f);
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.fog_dist, ctx->fog_dist);
}

static void gl33_set_light_dir(void* vctx, float x, float y, float z) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->light_dir[0] = x; ctx->light_dir[1] = y; ctx->light_dir[2] = z;
    glUseProgram(ctx->program);
    gl33_upload_vec3(ctx->uniforms.light_dir, x, y, z);
}

static void gl33_set_ambient(void* vctx, float level) {
    (void)vctx; (void)level;
    /* GL33 ambient ist im Shader hardcoded (0.15) — TODO: Uniform */
}

/* ========================================================
 * Backend Export
 * ======================================================== */

Moo3DBackend moo_backend_gl33 = {
    .create_window = gl33_create_window,
    .close         = gl33_close,
    .is_open       = gl33_is_open,
    .clear         = gl33_clear,
    .swap          = gl33_swap,
    .perspective   = gl33_perspective,
    .camera        = gl33_camera,
    .push_matrix   = gl33_push_matrix,
    .pop_matrix    = gl33_pop_matrix,
    .translate     = gl33_translate,
    .rotate        = gl33_rotate,
    .cube          = gl33_cube,
    .sphere        = gl33_sphere,
    .triangle      = gl33_triangle,
    .key_pressed   = gl33_key_pressed,
    .capture_mouse = gl33_capture_mouse,
    .release_mouse = gl33_release_mouse,
    .mouse_dx      = gl33_mouse_dx,
    .mouse_dy      = gl33_mouse_dy,
    .mouse_x       = gl33_mouse_x,
    .mouse_y       = gl33_mouse_y,
    .mouse_button  = gl33_mouse_button,
    .mouse_wheel   = gl33_mouse_wheel,
    .set_fog_density = gl33_set_fog_density,
    .set_light_dir   = gl33_set_light_dir,
    .set_ambient     = gl33_set_ambient,
    .chunk_create  = gl33_chunk_create,
    .chunk_begin   = gl33_chunk_begin,
    .chunk_end     = gl33_chunk_end,
    .chunk_draw    = gl33_chunk_draw_fn,
    .chunk_delete  = gl33_chunk_delete_fn,
};

/* ========================================================
 * Screenshot — glReadPixels + SDL_SaveBMP (k3 Bug-Fix)
 * Backend-spezifisch: castet ctx zu GL33Context (gl33-Layout).
 * Andere Backends muessen eigene screenshot-Helper bringen.
 * ======================================================== */
#include <SDL2/SDL.h>
#include <stdlib.h>

int gl33_screenshot_bmp(void* ctx_void, const char* path) {
    if (!ctx_void || !path) return 0;
    GL33Context* ctx = (GL33Context*)ctx_void;
    if (!ctx->window) return 0;
    glfwMakeContextCurrent(ctx->window);
    int w = ctx->width;
    int h = ctx->height;
    if (w <= 0 || h <= 0) return 0;
    unsigned char* buf = (unsigned char*)malloc((size_t)w * h * 4);
    if (!buf) return 0;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    /* Y-flip (glReadPixels liefert bottom-up, BMP via SDL nimmt top-down). */
    unsigned char* row = (unsigned char*)malloc((size_t)w * 4);
    if (row) {
        for (int y = 0; y < h / 2; y++) {
            unsigned char* a = buf + (size_t)y * w * 4;
            unsigned char* b = buf + (size_t)(h - 1 - y) * w * 4;
            memcpy(row, a, (size_t)w * 4);
            memcpy(a, b, (size_t)w * 4);
            memcpy(b, row, (size_t)w * 4);
        }
        free(row);
    }
    SDL_Surface* surf = SDL_CreateRGBSurfaceFrom(buf, w, h, 32, w * 4,
        0x000000FFu, 0x0000FF00u, 0x00FF0000u, 0xFF000000u);
    if (!surf) { free(buf); return 0; }
    int rc = SDL_SaveBMP(surf, path);
    SDL_FreeSurface(surf);
    free(buf);
    return rc == 0 ? 1 : 0;
}

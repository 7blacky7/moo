/**
 * moo_3d_gl33.c — OpenGL 3.3 Core Profile Backend fuer moo.
 * Implementiert Moo3DBackend mit VBOs, Shadern, Fog und Lighting.
 * Integriert: Shader (gl33_shaders), Mesh (gl33_mesh), Math (3d_math).
 */

#include "moo_runtime.h"
#include "moo_3d_backend.h"
#include "moo_3d_math.h"
#include "moo_3d_gl33_shaders.h"
#include "moo_3d_gl33_mesh.h"

#include "glad/include/glad/glad.h"
#include <GLFW/glfw3.h>
#include "moo_glfw_platform.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "moo_3d_ao_types.h"

/* ========================================================
 * GL33 Context
 * ======================================================== */

/* Licht-Stufe 4: aufgezeichneter chunk_zeichne-Aufruf fuer den Schatten-Pass. */
typedef struct {
    int id;
    float model[16];
} GL33ShadowItem;

typedef struct {
    GLFWwindow* window;
    int width;
    int height;
    /* Shader */
    GLuint program;
    GL33Uniforms uniforms;
    int render_time_fixed;  /* calloc: Live-Modus bleibt der Default */
    /* Opt-in linear HDR scene; finalized output is code-value sRGB. */
    int hdr_on, hdr_finalized;
    GLuint hdr_fbo, hdr_color, hdr_depth;
    int hdr_w, hdr_h;
    float hdr_exposure;
    GLuint hdr_program;
    int hdr_resources_started;
    double render_time_seconds;
    struct {
        int on, initialized, levels, w, h;
        GLuint program, fbo[32], tex[32];
        float minimum, maximum, key, tau, mean_luminance;
        double last_time;
    } exposure;
    float light_linear[8];
    float pbr[3]; /* enabled, metallic, perceptual roughness */
    GL33AOState ao;
    /* Matrices */
    float projection[16];
    float view[16];          /* Kamera (raum_kamera); getrennt von Model, damit Licht im Weltraum rechnet */
    MooMatrixStack modelview; /* Model-Matrix (verschiebe/rotiere/push/pop) */
    struct {
        GLuint fbo, color, depth;
        int width, height, valid, on;
        MooWaterSettings settings;
    } water;
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
    struct {
        int on, locations_ready;
        float density, base_height, falloff, sun_strength;
        GLint on_loc, additive_loc, profile_loc;
    } height_fog;
    float ambient;
    float alpha;
    float wave[3];   /* amp, freq, speed (raum_wellen) */
    float eye[3];        /* Kamera-Position (Specular) */
    float light_color[3];
    float spec[2];       /* strength, power (raum_glanz) */
    float tint[3];       /* raum_farbton, multipliziert die Vertex-Farbe */
    float extra_dir[3][3];  /* raum_licht_hinzufuegen idx 1..3 */
    float extra_col[3][3];  /* Farbe 0 = Licht aus */
    float point_pos[4][3];  /* raum_punktlicht idx 0..3 (Weltkoordinaten) */
    float point_col[4][3];
    float point_rad[4];     /* Radius 0 = aus */
    float emissive;         /* raum_leuchten */
    int blend_additive;     /* raum_mischmodus */
    int smooth_chunks;      /* raum_glaetten: Normalen beim chunk_ende mitteln */
    GLuint textures[64];    /* ERDE-15: Slot 1..63, 0 = keine Textur */
    int tex_active;         /* aktiver Slot fuer folgende Draws */
    float tex_scale;        /* Wiederholungen pro Welteinheit */
    /* Licht-Stufe 4: Shadow-Map */
    float look[3];          /* Kamera-Blickpunkt = Zentrum des Schattenbereichs */
    int shadow_on;
    int shadow_ready;       /* Map aus dem Vorframe gueltig */
    int shadow_size;
    float shadow_range;
    GLuint shadow_fbo;
    GLuint shadow_tex;
    GLuint shadow_prog;
    GLint shadow_u_mvp;
    float light_space[16];
    GL33ShadowItem* shadow_items;
    int shadow_count;
    int shadow_cap;
    /* ERDE-16: Bloom-Post-Pass */
    float bloom_strength;   /* 0 = aus */
    float bloom_threshold;
    int bloom_w, bloom_h;   /* Groesse des Backbuffers, fuer den die Ziele angelegt sind */
    GLuint bloom_scene_tex;
    GLuint bloom_tex[2];
    GLuint bloom_fbo[2];
    GLuint bloom_vao;
    GLuint bloom_prog_bright, bloom_prog_blur, bloom_prog_comp;
    int bloom_done;         /* Post-Pass in diesem Frame schon angewendet (Grab vor Swap) */
    int bloom_passes;       /* ERDE-17: Blur-Durchgaenge 1..8 = Radius des Hofs */
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
} GL33Context;

#include "moo_3d_ao_gl33.h"
#include "moo_3d_water_gl33.h"
#include "moo_3d_fog_gl33.h"

#include "moo_3d_hdr_gl33.h"
#include "moo_3d_pbr_gl33.h"

static void gl33_scroll_callback(GLFWwindow* w, double xoff, double yoff);

/* Forward decl — Definition steht weiter unten in der Screenshot-Section. */
int gl33_screenshot_bmp(void* ctx_void, const char* path);

/* ========================================================
 * Helpers
 * ======================================================== */

/* Normal-Matrix (column-major mat3) = transpose(inverse(M3x3)) = Kofaktoren/det.
 * Bei (fast) singulaerem M faellt sie auf M3x3 zurueck. */
static void gl33_normal_matrix(const float* m, float* out) {
    float a = m[0], b = m[4], c = m[8];
    float d = m[1], e = m[5], f = m[9];
    float g = m[2], h = m[6], i = m[10];
    float c00 = e*i - f*h, c01 = -(d*i - f*g), c02 = d*h - e*g;
    float c10 = -(b*i - c*h), c11 = a*i - c*g, c12 = -(a*h - b*g);
    float c20 = b*f - c*e, c21 = -(a*f - c*d), c22 = a*e - b*d;
    float det = a*c00 + b*c01 + c*c02;
    if (fabsf(det) < 1e-12f) {
        out[0] = a; out[1] = d; out[2] = g;
        out[3] = b; out[4] = e; out[5] = h;
        out[6] = c; out[7] = f; out[8] = i;
        return;
    }
    float inv = 1.0f / det;
    out[0] = c00*inv; out[1] = c10*inv; out[2] = c20*inv;
    out[3] = c01*inv; out[4] = c11*inv; out[5] = c21*inv;
    out[6] = c02*inv; out[7] = c12*inv; out[8] = c22*inv;
}

static void gl33_update_mvp(GL33Context* ctx) {
    /* mvp = P * V * M; uModel = M, damit Normalen und WorldPos im Weltraum liegen. */
    float vm[16], mvp[16], nm[9];
    mat4_multiply(vm, ctx->view, ctx->modelview.current);
    mat4_multiply(mvp, ctx->projection, vm);
    gl33_normal_matrix(ctx->modelview.current, nm);
    glUseProgram(ctx->program);
    gl33_upload_matrix(ctx->uniforms.mvp, mvp);
    gl33_upload_matrix(ctx->uniforms.model, ctx->modelview.current);
    glUniformMatrix3fv(ctx->uniforms.normal_mat, 1, GL_FALSE, nm);
    gl33_fog_upload(ctx);
    gl33_water_upload(ctx);
}

static void gl33_upload_lighting(GL33Context* ctx) {
    glUseProgram(ctx->program);
    gl33_upload_vec3(ctx->uniforms.light_dir,
                     ctx->light_dir[0], ctx->light_dir[1], ctx->light_dir[2]);
    gl33_upload_vec3(ctx->uniforms.fog_color,
                     ctx->fog_color[0], ctx->fog_color[1], ctx->fog_color[2]);
    gl33_upload_float(ctx->uniforms.fog_dist, ctx->fog_dist);
    gl33_upload_float(ctx->uniforms.ambient, ctx->ambient);
    gl33_upload_float(ctx->uniforms.alpha, ctx->alpha);
    gl33_upload_vec3(ctx->uniforms.eye_pos, ctx->eye[0], ctx->eye[1], ctx->eye[2]);
    gl33_upload_vec3(ctx->uniforms.light_color, ctx->light_color[0], ctx->light_color[1], ctx->light_color[2]);
    gl33_upload_float(ctx->uniforms.spec_strength, ctx->spec[0]);
    gl33_upload_float(ctx->uniforms.spec_power, ctx->spec[1]);
    gl33_upload_vec3(ctx->uniforms.tint, ctx->tint[0], ctx->tint[1], ctx->tint[2]);
    glUniform3fv(ctx->uniforms.extra_dir, 3, &ctx->extra_dir[0][0]);
    glUniform3fv(ctx->uniforms.extra_col, 3, &ctx->extra_col[0][0]);
    glUniform1fv(ctx->uniforms.light_linear,8,ctx->light_linear);
    glUniform3fv(ctx->uniforms.pbr,1,ctx->pbr);
    glUniform3fv(ctx->uniforms.point_pos, 4, &ctx->point_pos[0][0]);
    glUniform3fv(ctx->uniforms.point_col, 4, &ctx->point_col[0][0]);
    glUniform1fv(ctx->uniforms.point_rad, 4, ctx->point_rad);
    gl33_upload_float(ctx->uniforms.emissive, ctx->emissive);
    gl33_upload_float(ctx->uniforms.unlit, 0.0f);
    gl33_upload_float(ctx->uniforms.tex_on, ctx->tex_active > 0 ? 1.0f : 0.0f);
    gl33_upload_float(ctx->uniforms.tex_scale, ctx->tex_scale);
    gl33_upload_float(ctx->uniforms.shadow_on, (ctx->shadow_on && ctx->shadow_ready) ? 1.0f : 0.0f);
    gl33_upload_matrix(ctx->uniforms.light_space, ctx->light_space);
}

/* Draw vertices immediately (non-chunked) mit beliebigem Primitiv-Typ */
static void gl33_draw_immediate_mode(GL33Context* ctx, MeshBuilder* mb, GLenum mode) {
    if (!gl33_scene_draw_allowed(ctx)) return;
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

    glDrawArrays(mode, 0, mb->count);
    glBindVertexArray(0);
}

static void gl33_draw_immediate(GL33Context* ctx, MeshBuilder* mb) {
    gl33_draw_immediate_mode(ctx, mb, GL_TRIANGLES);
}

/* ========================================================
 * Backend Functions — Lifecycle
 * ======================================================== */

void* gl33_init_ctx_from_window(void* win_void, int w, int h);  /* P6 forward */

static void* gl33_create_window(const char* title, int w, int h) {
    moo_glfw_platform_hint();
    if (!glfwInit()) {
        fprintf(stderr, "moo GL33: glfwInit fehlgeschlagen\n");
        return NULL;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

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
    ctx->hdr_exposure = 1.0f;

    /* Shader */
    ctx->program = gl33_create_program();
    if (!ctx->program) {
        fprintf(stderr, "moo GL33: Shader-Programm fehlgeschlagen\n");
        free(ctx);
        glfwDestroyWindow(win);
        return NULL;
    }
    ctx->uniforms = gl33_get_uniforms(ctx->program);
    ctx->tex_scale = 1.0f;
    glUseProgram(ctx->program);
    glUniform1i(ctx->uniforms.tex, 1);  /* Textur-Unit 1 (Unit 0 nutzt der Hybrid-Text) */
    glUniform1i(ctx->uniforms.shadow_map, 2);  /* Shadow-Map auf Unit 2 */
    mat4_identity(ctx->light_space);

    /* Matrices */
    mat4_identity(ctx->projection);
    mat4_identity(ctx->view);
    moo_matrix_stack_init(&ctx->modelview);

    /* Chunks */
    gl33_chunk_system_init(ctx->chunks);
    ctx->active_chunk = -1;

    /* Mouse + Scroll */
    ctx->scroll_acc_x = 0;
    ctx->scroll_acc_y = 0;
    /* Tastatur-Sim auf "nicht simuliert" (-1); sim_delta_* sind via calloc bereits 0. */
    memset(ctx->sim_keys, -1, sizeof(ctx->sim_keys));
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
    // Horizont-Farbe: Passt zu welten.moos Clear-Color (0.53, 0.81, 0.92)
    ctx->fog_color[0] = 0.85f;
    ctx->fog_color[1] = 0.87f;
    ctx->fog_color[2] = 0.90f;
    /* Nebel rechnet in Welteinheiten: ohne raum_nebel praktisch aus. */
    ctx->fog_dist = 1.0e9f;
    ctx->ambient = 0.15f;
    ctx->alpha = 1.0f;
    ctx->light_color[0] = 1.0f; ctx->light_color[1] = 1.0f; ctx->light_color[2] = 1.0f;
    ctx->spec[0] = 0.0f; ctx->spec[1] = 32.0f;
    ctx->tint[0] = 1.0f; ctx->tint[1] = 1.0f; ctx->tint[2] = 1.0f;
    ctx->eye[0] = 0.0f; ctx->eye[1] = 0.0f; ctx->eye[2] = 0.0f;
    /* Alpha-Blending fuer raum_transparenz — alpha=1 verhaelt sich opak. */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

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

    glfwMakeContextCurrent(ctx->window);
    gl33_hdr_release(ctx);

    /* Cleanup chunks */
    for (int i = 0; i < MAX_GL33_CHUNKS; i++)
        gl33_chunk_delete(ctx->chunks, i);

    mesh_builder_free(&ctx->builder);

    if (ctx->imm_vbo) glDeleteBuffers(1, &ctx->imm_vbo);
    if (ctx->imm_vao) glDeleteVertexArrays(1, &ctx->imm_vao);
    for (int t = 1; t < 64; t++)
        if (ctx->textures[t]) glDeleteTextures(1, &ctx->textures[t]);
    if (ctx->shadow_fbo) glDeleteFramebuffers(1, &ctx->shadow_fbo);
    if (ctx->bloom_fbo[0]) glDeleteFramebuffers(2, ctx->bloom_fbo);
    if (ctx->bloom_tex[0]) glDeleteTextures(2, ctx->bloom_tex);
    if (ctx->bloom_scene_tex) glDeleteTextures(1, &ctx->bloom_scene_tex);
    if (ctx->bloom_vao) glDeleteVertexArrays(1, &ctx->bloom_vao);
    if (ctx->bloom_prog_bright) glDeleteProgram(ctx->bloom_prog_bright);
    if (ctx->bloom_prog_blur) glDeleteProgram(ctx->bloom_prog_blur);
    if (ctx->bloom_prog_comp) glDeleteProgram(ctx->bloom_prog_comp);
    if (ctx->shadow_tex) glDeleteTextures(1, &ctx->shadow_tex);
    if (ctx->shadow_prog) glDeleteProgram(ctx->shadow_prog);
    free(ctx->shadow_items);
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

void gl33_clear(void* vctx, float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    glfwMakeContextCurrent(ctx->window);
    if (ctx->hdr_on) {
        ctx->hdr_finalized=1; /* Failed target creation must keep draws closed. */
        ctx->water.valid=0;
        gl33_set_water(ctx,NULL);
        int w=0,h=0; glfwGetFramebufferSize(ctx->window,&w,&h);
        if (!gl33_hdr_targets(ctx,w,h)) return;
        if (!gl33_ao_targets(ctx)) { gl33_hdr_error("AO: Shader oder Framebuffer fehlgeschlagen"); return; }
        ctx->ao.opaque_done=ctx->ao.view_locked=0;
        ctx->hdr_resources_started=1;
        ctx->hdr_finalized=0; ctx->bloom_done=0;
        glBindFramebuffer(GL_FRAMEBUFFER,ctx->hdr_fbo);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
        glViewport(0,0,w,h); glDisable(GL_FRAMEBUFFER_SRGB); glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA,ctx->blend_additive?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);
        r=gl33_srgb_decode(r); g=gl33_srgb_decode(g); b=gl33_srgb_decode(b);
    }
    glClearColor(r,g,b,1.0f); glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    if (ctx->hdr_on && ctx->ao.on) {
        const GLenum buffers[2]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};
        const GLfloat zero[4]={0,0,0,0};
        glDrawBuffers(2,buffers); glClearBufferfv(GL_COLOR,1,zero);
    }
    mat4_identity(ctx->view);
    moo_matrix_stack_init(&ctx->modelview);
}

static void gl33_render_shadow_map(GL33Context* ctx);
static void gl33_render_bloom(GL33Context* ctx);

/* ERDE-16: Post-Pass (Bloom) hoechstens einmal pro Frame anwenden. Extern, damit
 * test_frame_grab/Screenshots (auch im Hybrid-Fenster) das finale Bild sehen. */
void gl33_apply_post(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    if (ctx->hdr_on) { gl33_scene_end(ctx); return; }
    if (ctx->bloom_done) return;
    gl33_render_bloom(ctx);
    ctx->bloom_done = 1;
}

static int gl33_renderer_info(void* vctx, MooRendererInfo* info) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window || !info) return 0;
    GLFWwindow* previous = glfwGetCurrentContext();
    if (previous != ctx->window) glfwMakeContextCurrent(ctx->window);
    const char* vendor = (const char*)glGetString(GL_VENDOR);
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* version = (const char*)glGetString(GL_VERSION);
    if (previous != ctx->window) glfwMakeContextCurrent(previous);
    if (!vendor || !renderer || !version || !*vendor || !*renderer || !*version)
        return 0;
    *info = (MooRendererInfo){"gl33", "OpenGL", vendor, renderer, version};
    return 1;
}

static void gl33_set_render_time(void* vctx, int fixed, float seconds) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    glfwMakeContextCurrent(ctx->window);
    if (ctx->render_time_fixed != fixed) ctx->exposure.initialized=0;
    ctx->render_time_fixed = fixed;
    ctx->render_time_seconds = seconds;
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.time, fixed ? seconds : (float)glfwGetTime());
}

/* Zeit-Uniform fuer Wellen-Animation (wirkt ab dem naechsten Frame).
 * Extern, damit auch hybrid_aktualisieren die Zeit weiterschaltet.
 * Rendert ausserdem die Shadow-Map aus den Chunk-Draws dieses Frames. */
void gl33_tick_time(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    gl33_render_shadow_map(ctx);
    gl33_apply_post(ctx);
    ctx->bloom_done = 0;  /* naechster Frame */
    glUseProgram(ctx->program);
    if (!ctx->render_time_fixed)
        gl33_upload_float(ctx->uniforms.time, (float)glfwGetTime());
}

static void gl33_swap(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    gl33_tick_time(ctx);
    glfwSwapBuffers(ctx->window);
    glfwPollEvents();
}

static void gl33_triangle_colors(void*, float,float,float,float,float,float, float,float,float,float,float,float, float,float,float,float,float,float);

/* Fullscreen-Himmel; Depth-, Matrix- und Lichtzustand werden restauriert.
 * Der Himmel ist unbeleuchtet: Umgebungslicht, Lichtfarbe, Glanz, Nebel und
 * Wellen wirken nicht auf ihn, damit die Zenit-/Horizontfarben exakt ankommen. */
static void gl33_sky(void* vctx, float zr, float zg, float zb, float hr, float hg, float hb) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    /* Validate before temporary sky state, but lock matrices only for real depth writes. */
    if (ctx->ao.on) {
        if (!ctx->hdr_on || !ctx->hdr_fbo || ctx->hdr_finalized || ctx->ao.opaque_done ||
            ctx->water.on || (!ctx->blend_additive && ctx->alpha<.999f)) {
            gl33_hdr_error("AO: Himmel nur vor opakem Abschluss zeichnen"); return;
        }
    } else if (!gl33_scene_draw_allowed(ctx)) return;
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
    float p[16], v[16], m[16], light[3], lcol[3], tint[3];
    float fog = ctx->fog_dist, amb = ctx->ambient, spec = ctx->spec[0];
    memcpy(p, ctx->projection, sizeof(p)); memcpy(v, ctx->view, sizeof(v)); memcpy(m, ctx->modelview.current, sizeof(m));
    memcpy(light, ctx->light_dir, sizeof(light)); memcpy(lcol, ctx->light_color, sizeof(lcol)); memcpy(tint, ctx->tint, sizeof(tint));
    ctx->tint[0]=1; ctx->tint[1]=1; ctx->tint[2]=1;
    mat4_identity(ctx->projection); mat4_identity(ctx->view); mat4_identity(ctx->modelview.current);
    ctx->light_dir[0]=0; ctx->light_dir[1]=0; ctx->light_dir[2]=1;
    ctx->light_color[0]=1; ctx->light_color[1]=1; ctx->light_color[2]=1;
    ctx->ambient = 1.0f; ctx->fog_dist = 1.0e9f; ctx->spec[0] = 0.0f;
    gl33_upload_lighting(ctx);
    gl33_upload_float(ctx->uniforms.wave_amp, 0.0f);
    gl33_upload_float(ctx->uniforms.unlit, 1.0f);  /* reset durch gl33_upload_lighting unten */
    glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
    gl33_triangle_colors(ctx,-1,-1,0,hr,hg,hb, 1,-1,0,hr,hg,hb, 1,1,0,zr,zg,zb);
    gl33_triangle_colors(ctx,-1,-1,0,hr,hg,hb, 1,1,0,zr,zg,zb, -1,1,0,zr,zg,zb);
    memcpy(ctx->projection,p,sizeof(p)); memcpy(ctx->view,v,sizeof(v)); memcpy(ctx->modelview.current,m,sizeof(m));
    memcpy(ctx->light_dir,light,sizeof(light)); memcpy(ctx->light_color,lcol,sizeof(lcol)); memcpy(ctx->tint,tint,sizeof(tint));
    ctx->ambient = amb; ctx->fog_dist = fog; ctx->spec[0] = spec;
    gl33_upload_lighting(ctx);
    gl33_upload_float(ctx->uniforms.wave_amp, ctx->wave[0]);
    glDepthMask(mask); if(depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
}

/* ========================================================
 * Backend Functions — Kamera
 * ======================================================== */

static void gl33_perspective(void* vctx, float fov, float near_val, float far_val) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    float aspect = ctx->hdr_on && ctx->hdr_w>0 && ctx->hdr_h>0
        ? (float)ctx->hdr_w / (float)ctx->hdr_h
        : (float)ctx->width / (float)ctx->height;
    mat4_perspective(ctx->projection, fov, aspect, near_val, far_val);
}

static void gl33_camera(void* vctx,
                        float ex, float ey, float ez,
                        float lx, float ly, float lz) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    /* Kamera nur in die View-Matrix; Model startet danach bei Identitaet. */
    mat4_lookat(ctx->view, ex, ey, ez, lx, ly, lz, 0, 1, 0);
    mat4_identity(ctx->modelview.current);
    /* Kamera-Position fuer Specular (raum_glanz) */
    ctx->eye[0] = ex; ctx->eye[1] = ey; ctx->eye[2] = ez;
    ctx->look[0] = lx; ctx->look[1] = ly; ctx->look[2] = lz;
    glUseProgram(ctx->program);
    gl33_upload_vec3(ctx->uniforms.eye_pos, ex, ey, ez);
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

static void gl33_scale(void* vctx, float sx, float sy, float sz) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (ctx) mat4_scale(ctx->modelview.current, sx, sy, sz);
}

/* ERDE-7: idx 0 = Hauptlicht (wie raum_licht + raum_lichtfarbe), idx 1..3 = Zusatzlichter.
 * Richtung zeigt ZUM Licht (wie raum_licht). Farbe schwarz schaltet ab. */
static void gl33_set_extra_light(void* vctx, int idx, float x, float y, float z, float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || idx < 0 || idx > 3) return;
    ctx->light_linear[idx]=0;
    if (idx == 0) {
        ctx->light_dir[0] = x; ctx->light_dir[1] = y; ctx->light_dir[2] = z;
        ctx->light_color[0] = r; ctx->light_color[1] = g; ctx->light_color[2] = b;
    } else {
        float* d = ctx->extra_dir[idx - 1];
        float* c = ctx->extra_col[idx - 1];
        d[0] = x; d[1] = y; d[2] = z;
        c[0] = r; c[1] = g; c[2] = b;
    }
    gl33_upload_lighting(ctx);
}

/* ERDE-7: Punktlicht idx 0..3 in Weltkoordinaten, linear-quadratischer Abfall
 * bis radius. radius <= 0 schaltet es ab. */
static void gl33_set_point_light(void* vctx, int idx, float x, float y, float z, float radius, float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || idx < 0 || idx > 3) return;
    ctx->light_linear[idx+4]=0;
    ctx->point_pos[idx][0] = x; ctx->point_pos[idx][1] = y; ctx->point_pos[idx][2] = z;
    ctx->point_col[idx][0] = r; ctx->point_col[idx][1] = g; ctx->point_col[idx][2] = b;
    ctx->point_rad[idx] = radius > 0.0f ? radius : 0.0f;
    gl33_upload_lighting(ctx);
}

/* ERDE-8: Eigenleuchten fuer folgende Draws (0 = aus); addiert base*staerke. */
static void gl33_set_emissive(void* vctx, float strength) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->emissive = strength > 0.0f ? strength : 0.0f;
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.emissive, ctx->emissive);
}

/* ERDE-8: 1 = additiv (Glow/Partikel, schreibt keine Tiefe), 0 = normales Alpha-Blending. */
static void gl33_set_blend_mode(void* vctx, int additive) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->blend_additive = additive ? 1 : 0;
    if (ctx->blend_additive) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(ctx->alpha >= 0.999f ? GL_TRUE : GL_FALSE);
    }
}

/* ERDE-10: Linien/Punkte sind unbeleuchtet (Sterne, Blitze, Debug-Achsen),
 * ohne Nebel und Wellen, und werden immer sofort gezeichnet (nicht in Chunks). */
static void gl33_draw_unlit(GL33Context* ctx, MeshBuilder* mb, GLenum mode) {
    if (!gl33_scene_draw_allowed(ctx)) return;
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.unlit, 1.0f);
    gl33_upload_float(ctx->uniforms.wave_amp, 0.0f);
    gl33_draw_immediate_mode(ctx, mb, mode);
    gl33_upload_float(ctx->uniforms.unlit, 0.0f);
    gl33_upload_float(ctx->uniforms.wave_amp, ctx->wave[0]);
}

static void gl33_line(void* vctx, float x1, float y1, float z1,
                      float x2, float y2, float z2, float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    if (!gl33_scene_draw_allowed(ctx)) return;
    MeshBuilder tmp;
    mesh_builder_init(&tmp);
    mesh_builder_add_vertex(&tmp, x1, y1, z1, r, g, b, 0.0f, 1.0f, 0.0f);
    mesh_builder_add_vertex(&tmp, x2, y2, z2, r, g, b, 0.0f, 1.0f, 0.0f);
    gl33_draw_unlit(ctx, &tmp, GL_LINES);
    mesh_builder_free(&tmp);
}

/* size in Pixeln (glPointSize, Core-Profil ohne PROGRAM_POINT_SIZE). */
static void gl33_point(void* vctx, float x, float y, float z, float size,
                       float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    if (!gl33_scene_draw_allowed(ctx)) return;
    MeshBuilder tmp;
    mesh_builder_init(&tmp);
    mesh_builder_add_vertex(&tmp, x, y, z, r, g, b, 0.0f, 1.0f, 0.0f);
    glPointSize(size > 1.0f ? size : 1.0f);
    gl33_draw_unlit(ctx, &tmp, GL_POINTS);
    mesh_builder_free(&tmp);
}

/* ERDE-15: RGBA8-Pixel (Zeile 0 = oben) als wiederholbare Textur mit Mipmaps.
 * Liefert Slot 1..63 oder -1, wenn kein Slot frei ist. */
static int gl33_texture_create(void* vctx, const uint8_t* rgba, int w, int h) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !rgba || w <= 0 || h <= 0) return -1;
    ctx->hdr_resources_started=1;
    int slot = -1;
    for (int t = 1; t < 64; t++) if (!ctx->textures[t]) { slot = t; break; }
    if (slot < 0) return -1;
    glfwMakeContextCurrent(ctx->window);
    glGenTextures(1, &ctx->textures[slot]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx->textures[slot]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, ctx->hdr_on ? GL_SRGB8_ALPHA8 : GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    /* Aktive Textur wiederherstellen, damit folgende Draws unveraendert bleiben. */
    glBindTexture(GL_TEXTURE_2D, ctx->tex_active > 0 ? ctx->textures[ctx->tex_active] : 0);
    glActiveTexture(GL_TEXTURE0);
    return slot;
}

/* ERDE-15: id <= 0 schaltet die Textur ab; scale = Wiederholungen pro Welteinheit. */
static void gl33_set_texture(void* vctx, int id, float scale) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->tex_active = (id > 0 && id < 64 && ctx->textures[id]) ? id : 0;
    ctx->tex_scale = scale > 0.0f ? scale : 1.0f;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ctx->tex_active ? ctx->textures[ctx->tex_active] : 0);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.tex_on, ctx->tex_active ? 1.0f : 0.0f);
    gl33_upload_float(ctx->uniforms.tex_scale, ctx->tex_scale);
}

static const char* GL33_SHADOW_VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "uniform mat4 uLightMVP;\n"
    "void main() { gl_Position = uLightMVP * vec4(aPos, 1.0); }\n";
static const char* GL33_SHADOW_FS =
    "#version 330 core\n"
    "void main() { }\n";

/* Licht-Stufe 4: Depth-Textur + FBO (neu) anlegen. */
static int gl33_shadow_create(GL33Context* ctx, int size) {
    if (ctx->shadow_fbo) { glDeleteFramebuffers(1, &ctx->shadow_fbo); ctx->shadow_fbo = 0; }
    if (ctx->shadow_tex) { glDeleteTextures(1, &ctx->shadow_tex); ctx->shadow_tex = 0; }
    if (!ctx->shadow_prog) {
        GLuint vs = gl33_compile_shader(GL_VERTEX_SHADER, GL33_SHADOW_VS);
        GLuint fs = gl33_compile_shader(GL_FRAGMENT_SHADER, GL33_SHADOW_FS);
        if (!vs || !fs) return 0;
        ctx->shadow_prog = glCreateProgram();
        glAttachShader(ctx->shadow_prog, vs);
        glAttachShader(ctx->shadow_prog, fs);
        glLinkProgram(ctx->shadow_prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        ctx->shadow_u_mvp = glGetUniformLocation(ctx->shadow_prog, "uLightMVP");
    }
    glGenTextures(1, &ctx->shadow_tex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ctx->shadow_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glActiveTexture(GL_TEXTURE0);
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGenFramebuffers(1, &ctx->shadow_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->shadow_tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    int ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    if (!ok) {
        fprintf(stderr, "moo GL33: Shadow-FBO unvollstaendig, Schatten aus\n");
        return 0;
    }
    ctx->shadow_size = size;
    return 1;
}

/* raum_schatten: size 256..4096 (Default 2048), range = halbe Kantenlaenge des
 * Schattenbereichs um den Kamera-Blickpunkt (Default 60 Einheiten). */
static void gl33_set_shadow(void* vctx, int on, int size, float range) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    if (size <= 0) size = 2048;
    if (size < 256) size = 256;
    if (size > 4096) size = 4096;
    ctx->shadow_range = range > 0.0f ? range : 60.0f;
    ctx->shadow_ready = 0;
    ctx->shadow_count = 0;
    ctx->shadow_on = 0;
    if (on) {
        glfwMakeContextCurrent(ctx->window);
        if (ctx->shadow_fbo && ctx->shadow_size == size) ctx->shadow_on = 1;
        else ctx->shadow_on = gl33_shadow_create(ctx, size);
    }
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.shadow_on, 0.0f);
}

/* Depth-Pass aus Hauptlicht-Richtung ueber die aufgezeichneten Chunk-Draws. */
static void gl33_render_shadow_map(GL33Context* ctx) {
    if (!ctx->shadow_on || !ctx->shadow_fbo || !ctx->shadow_prog) return;
    float lx = ctx->light_dir[0], ly = ctx->light_dir[1], lz = ctx->light_dir[2];
    float len = sqrtf(lx * lx + ly * ly + lz * lz);
    if (len < 1e-6f) { lx = 0.0f; ly = 1.0f; lz = 0.0f; len = 1.0f; }
    lx /= len; ly /= len; lz /= len;
    float r = ctx->shadow_range;
    float cx = ctx->look[0], cy = ctx->look[1], cz = ctx->look[2];
    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    if (fabsf(ly) > 0.99f) { uy = 0.0f; uz = 1.0f; }
    float lv[16], lp[16];
    mat4_lookat(lv, cx + lx * r * 2.0f, cy + ly * r * 2.0f, cz + lz * r * 2.0f, cx, cy, cz, ux, uy, uz);
    mat4_ortho(lp, -r, r, -r, r, 0.05f, r * 4.0f);
    mat4_multiply(ctx->light_space, lp, lv);

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLboolean mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
    glViewport(0, 0, ctx->shadow_size, ctx->shadow_size);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    glUseProgram(ctx->shadow_prog);
    for (int i = 0; i < ctx->shadow_count; i++) {
        float m[16];
        mat4_multiply(m, ctx->light_space, ctx->shadow_items[i].model);
        glUniformMatrix4fv(ctx->shadow_u_mvp, 1, GL_FALSE, m);
        gl33_chunk_draw(ctx->chunks, ctx->shadow_items[i].id);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glDepthMask(mask);
    if (!depth) glDisable(GL_DEPTH_TEST);

    ctx->shadow_count = 0;
    ctx->shadow_ready = 1;
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ctx->shadow_tex);
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.shadow_on, 1.0f);
    gl33_upload_matrix(ctx->uniforms.light_space, ctx->light_space);
}

/* ERDE-16: Vollbild-Dreieck ohne Vertexdaten (gl_VertexID). */
static const char* GL33_POST_VS =
    "#version 330 core\n"
    "out vec2 vUV;\n"
    "void main() {\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    vUV = p;\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";
static const char* GL33_BLOOM_BRIGHT_FS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "uniform sampler2D uSrc;\n"
    "uniform float uThreshold;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec3 c = texture(uSrc, vUV).rgb;\n"
    "    float l = max(max(c.r, c.g), c.b);\n"
    "    float k = smoothstep(uThreshold, 1.0, l);\n"
    "    fragColor = vec4(c * k, 1.0);\n"
    "}\n";
static const char* GL33_BLOOM_BLUR_FS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "uniform sampler2D uSrc;\n"
    "uniform vec2 uDir;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
    "    vec3 c = texture(uSrc, vUV).rgb * w[0];\n"
    "    for (int i = 1; i < 5; i++) {\n"
    "        c += texture(uSrc, vUV + uDir * float(i)).rgb * w[i];\n"
    "        c += texture(uSrc, vUV - uDir * float(i)).rgb * w[i];\n"
    "    }\n"
    "    fragColor = vec4(c, 1.0);\n"
    "}\n";
static const char* GL33_BLOOM_COMP_FS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "uniform sampler2D uSrc;\n"
    "uniform float uStrength;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = vec4(texture(uSrc, vUV).rgb * uStrength, 1.0); }\n";

static GLuint gl33_post_program(const char* fs_src) {
    GLuint vs = gl33_compile_shader(GL_VERTEX_SHADER, GL33_POST_VS);
    GLuint fs = gl33_compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(p); return 0; }
    return p;
}

static GLuint gl33_post_texture(int w, int h, int hdr) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, hdr ? GL_RGBA16F : GL_RGBA8, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

/* Ziele fuer Backbuffer-Groesse w x h (Bloom auf halber Aufloesung) anlegen. */
static int gl33_bloom_targets(GL33Context* ctx, int w, int h) {
    if (ctx->bloom_w == w && ctx->bloom_h == h && ctx->bloom_fbo[0]) return 1;
    if (!ctx->bloom_prog_bright) {
        ctx->bloom_prog_bright = gl33_post_program(GL33_BLOOM_BRIGHT_FS);
        ctx->bloom_prog_blur = gl33_post_program(GL33_BLOOM_BLUR_FS);
        ctx->bloom_prog_comp = gl33_post_program(GL33_BLOOM_COMP_FS);
        glGenVertexArrays(1, &ctx->bloom_vao);
        if (!ctx->bloom_prog_bright || !ctx->bloom_prog_blur || !ctx->bloom_prog_comp) return 0;
    }
    if (ctx->bloom_fbo[0]) glDeleteFramebuffers(2, ctx->bloom_fbo);
    if (ctx->bloom_tex[0]) glDeleteTextures(2, ctx->bloom_tex);
    if (ctx->bloom_scene_tex) glDeleteTextures(1, &ctx->bloom_scene_tex);
    int hw = w / 2 > 0 ? w / 2 : 1, hh = h / 2 > 0 ? h / 2 : 1;
    glActiveTexture(GL_TEXTURE3);
    ctx->bloom_scene_tex = gl33_post_texture(w, h, ctx->hdr_on);
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGenFramebuffers(2, ctx->bloom_fbo);
    int ok = 1;
    for (int i = 0; i < 2; i++) {
        ctx->bloom_tex[i] = gl33_post_texture(hw, hh, ctx->hdr_on);
        glBindFramebuffer(GL_FRAMEBUFFER, ctx->bloom_fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->bloom_tex[i], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) ok = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    ctx->bloom_w = w;
    ctx->bloom_h = h;
    return ok;
}

/* ERDE-16: Bloom auf den fertigen Backbuffer (vor dem Swap). Stellt Programm,
 * FBO, Viewport, Blend-, Depth- und Textur-Zustand wieder her. */
static void gl33_render_bloom(GL33Context* ctx) {
    if (ctx->bloom_strength <= 0.0f) return;
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    int w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0) return;
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    if (!gl33_bloom_targets(ctx, w, h)) { ctx->bloom_strength = 0.0f; return; }
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean mask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &mask);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glBindVertexArray(ctx->bloom_vao);
    glActiveTexture(GL_TEXTURE3);

    /* 1) Backbuffer -> Szenen-Textur */
    glBindTexture(GL_TEXTURE_2D, ctx->bloom_scene_tex);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vp[0], vp[1], w, h);
    int hw = w / 2 > 0 ? w / 2 : 1, hh = h / 2 > 0 ? h / 2 : 1;
    glViewport(0, 0, hw, hh);

    /* 2) Bright-Pass -> A */
    glBindFramebuffer(GL_FRAMEBUFFER, ctx->bloom_fbo[0]);
    glUseProgram(ctx->bloom_prog_bright);
    glUniform1i(glGetUniformLocation(ctx->bloom_prog_bright, "uSrc"), 3);
    glUniform1f(glGetUniformLocation(ctx->bloom_prog_bright, "uThreshold"), ctx->bloom_threshold);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    /* 3) 2x separabler Gauss: A -> B (horizontal), B -> A (vertikal) */
    glUseProgram(ctx->bloom_prog_blur);
    glUniform1i(glGetUniformLocation(ctx->bloom_prog_blur, "uSrc"), 3);
    GLint dir = glGetUniformLocation(ctx->bloom_prog_blur, "uDir");
    int passes = ctx->bloom_passes > 0 ? ctx->bloom_passes : 2;
    for (int pass = 0; pass < passes; pass++) {
        /* ERDE-17: ab Durchgang 3 gespreizte Taps -> Hof waechst deutlich; 1-2 wie bisher. */
        float spread = pass < 2 ? 1.0f : (float)pass;
        glBindFramebuffer(GL_FRAMEBUFFER, ctx->bloom_fbo[1]);
        glBindTexture(GL_TEXTURE_2D, ctx->bloom_tex[0]);
        glUniform2f(dir, spread / (float)hw, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, ctx->bloom_fbo[0]);
        glBindTexture(GL_TEXTURE_2D, ctx->bloom_tex[1]);
        glUniform2f(dir, 0.0f, spread / (float)hh);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    /* 4) additiv auf den Backbuffer */
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glUseProgram(ctx->bloom_prog_comp);
    glUniform1i(glGetUniformLocation(ctx->bloom_prog_comp, "uSrc"), 3);
    glUniform1f(glGetUniformLocation(ctx->bloom_prog_comp, "uStrength"), ctx->bloom_strength);
    glBindTexture(GL_TEXTURE_2D, ctx->bloom_tex[0]);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    /* Zustand zurueck */
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    if (ctx->blend_additive) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (depth) glEnable(GL_DEPTH_TEST);
    glDepthMask(mask);
    glUseProgram(ctx->program);
}

/* raum_bloom: strength 0 = aus, threshold = Helligkeit (0..1), ab der es leuchtet. */
static void gl33_set_bloom(void* vctx, float strength, float threshold, int passes) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->bloom_passes = passes < 1 ? 1 : (passes > 8 ? 8 : passes);
    ctx->bloom_strength = strength > 0.0f ? strength : 0.0f;
    ctx->bloom_threshold = (threshold > 0.0f && threshold < 1.0f) ? threshold : 0.8f;
}

static void gl33_set_tint(void* vctx, float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->tint[0] = r; ctx->tint[1] = g; ctx->tint[2] = b;
    glUseProgram(ctx->program);
    gl33_upload_vec3(ctx->uniforms.tint, r, g, b);
}

/* ========================================================
 * Backend Functions — Zeichnen
 * ======================================================== */

static void gl33_cube(void* vctx,
                      float x, float y, float z, float size,
                      float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    if (ctx->active_chunk < 0 && !gl33_scene_draw_allowed(ctx)) return;

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
    if (ctx->active_chunk < 0 && !gl33_scene_draw_allowed(ctx)) return;

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

static void gl33_triangle_colors(void* vctx,
                                 float x1, float y1, float z1, float r1, float g1, float b1,
                                 float x2, float y2, float z2, float r2, float g2, float b2,
                                 float x3, float y3, float z3, float r3, float g3, float b3) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    if (ctx->active_chunk < 0 && !gl33_scene_draw_allowed(ctx)) return;

    /* Face-Normale wie gl33_triangle */
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

    mesh_builder_add_vertex(mb, x1, y1, z1, r1, g1, b1, nx, ny, nz);
    mesh_builder_add_vertex(mb, x2, y2, z2, r2, g2, b2, nx, ny, nz);
    mesh_builder_add_vertex(mb, x3, y3, z3, r3, g3, b3, nx, ny, nz);

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
    if (ctx->active_chunk < 0 && !gl33_scene_draw_allowed(ctx)) return;

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

/* Tasten-String -> GLFW-Keycode. Gemeinsame Konvention fuer key_pressed UND
 * simulate_key, damit der Tri-State konsistent indexiert wird. 0 = unbekannt. */
static int gl33_keycode(const char* key) {
    if (!key) return 0;
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
    return glfw_key;
}

static int gl33_key_pressed(void* vctx, const char* key) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !key) return 0;

    int glfw_key = gl33_keycode(key);
    if (glfw_key <= 0 || glfw_key > GLFW_KEY_LAST) return 0;

    /* Tri-State: Sim-State ZUERST. -1 = nicht simuliert -> echter Input. */
    if (ctx->sim_keys[glfw_key] >= 0) return ctx->sim_keys[glfw_key];

    if (!ctx->window) return 0;
    return glfwGetKey(ctx->window, glfw_key) == GLFW_PRESS;
}

/* ========================================================
 * Backend Functions — Chunks (VBO-cached)
 * ======================================================== */

static int gl33_chunk_create(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return -1;
    ctx->hdr_resources_started=1;
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

/* Licht-Stufe 3: Vertices an (quantisiert) gleicher Position teilen sich eine
 * gemittelte Normale, aber nur mit Nachbarn innerhalb des Knickwinkels
 * (dot >= 0.5, also < 60 Grad) — Terrain wird weich, Wuerfelkanten bleiben scharf. */
typedef struct { int32_t qx, qy, qz; int idx; } GL33PosKey;

static int32_t gl33_quantize(float v) {
    float q = v * 1024.0f;
    if (q > 2.0e9f) q = 2.0e9f;
    if (q < -2.0e9f) q = -2.0e9f;
    return (int32_t)lroundf(q);
}

static int gl33_cmp_poskey(const void* a, const void* b) {
    const GL33PosKey* ka = (const GL33PosKey*)a;
    const GL33PosKey* kb = (const GL33PosKey*)b;
    if (ka->qx != kb->qx) return ka->qx < kb->qx ? -1 : 1;
    if (ka->qy != kb->qy) return ka->qy < kb->qy ? -1 : 1;
    if (ka->qz != kb->qz) return ka->qz < kb->qz ? -1 : 1;
    return ka->idx - kb->idx;
}

static void gl33_smooth_normals(MeshBuilder* mb) {
    int n = mb->count;
    if (n < 3) return;
    GL33PosKey* keys = (GL33PosKey*)malloc(sizeof(GL33PosKey) * (size_t)n);
    float* out = (float*)malloc(sizeof(float) * 3u * (size_t)n);
    if (!keys || !out) { free(keys); free(out); return; }
    for (int i = 0; i < n; i++) {
        MooVertex* v = &mb->vertices[i];
        keys[i].qx = gl33_quantize(v->px);
        keys[i].qy = gl33_quantize(v->py);
        keys[i].qz = gl33_quantize(v->pz);
        keys[i].idx = i;
    }
    qsort(keys, (size_t)n, sizeof(GL33PosKey), gl33_cmp_poskey);
    int start = 0;
    while (start < n) {
        int end = start + 1;
        while (end < n && keys[end].qx == keys[start].qx &&
               keys[end].qy == keys[start].qy && keys[end].qz == keys[start].qz) end++;
        for (int a = start; a < end; a++) {
            MooVertex* va = &mb->vertices[keys[a].idx];
            float sx = 0.0f, sy = 0.0f, sz = 0.0f;
            for (int b = start; b < end; b++) {
                MooVertex* vb = &mb->vertices[keys[b].idx];
                float d = va->nx * vb->nx + va->ny * vb->ny + va->nz * vb->nz;
                if (d >= 0.5f) { sx += vb->nx; sy += vb->ny; sz += vb->nz; }
            }
            float len = sqrtf(sx * sx + sy * sy + sz * sz);
            float* o = &out[3 * keys[a].idx];
            if (len > 1e-6f) { o[0] = sx / len; o[1] = sy / len; o[2] = sz / len; }
            else { o[0] = va->nx; o[1] = va->ny; o[2] = va->nz; }
        }
        start = end;
    }
    for (int i = 0; i < n; i++) {
        mb->vertices[i].nx = out[3 * i];
        mb->vertices[i].ny = out[3 * i + 1];
        mb->vertices[i].nz = out[3 * i + 2];
    }
    free(keys);
    free(out);
}

static void gl33_set_smooth(void* vctx, int on) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (ctx) ctx->smooth_chunks = on ? 1 : 0;
}

static void gl33_chunk_end(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || ctx->active_chunk < 0) return;
    if (ctx->smooth_chunks) gl33_smooth_normals(&ctx->builder);

    gl33_chunk_upload(ctx->chunks, ctx->active_chunk, &ctx->builder);
    ctx->active_chunk = -1;
}

static void gl33_chunk_draw_fn(void* vctx, int id) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!gl33_scene_draw_allowed(ctx)) return;

    gl33_update_mvp(ctx);
    gl33_chunk_draw(ctx->chunks, id);
    /* Licht-Stufe 4: fuer den Schatten-Pass am Frame-Ende merken. */
    if (ctx->shadow_on && id >= 0 && id < MAX_GL33_CHUNKS) {
        if (ctx->shadow_count >= ctx->shadow_cap) {
            int cap = ctx->shadow_cap ? ctx->shadow_cap * 2 : 256;
            GL33ShadowItem* items = (GL33ShadowItem*)realloc(ctx->shadow_items, sizeof(GL33ShadowItem) * (size_t)cap);
            if (!items) return;
            ctx->shadow_items = items;
            ctx->shadow_cap = cap;
        }
        GL33ShadowItem* it = &ctx->shadow_items[ctx->shadow_count++];
        it->id = id;
        memcpy(it->model, ctx->modelview.current, sizeof(it->model));
    }
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
    /* Raw-Mouse-Motion: umgeht Desktop-Maus-Beschleunigung/Pointer-Ballistik
       (sprunghaftes Look unter XWayland/KWin + NVIDIA). Fallback: wenn die
       Plattform es nicht unterstuetzt (glfwRawMouseMotionSupported() == false),
       bleibt es beim normalen CURSOR_DISABLED-Verhalten — kein Fehler. */
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(ctx->window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwGetCursorPos(ctx->window, &ctx->last_mouse_x, &ctx->last_mouse_y);
    ctx->mouse_captured = 1;
}

static float gl33_mouse_dx(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
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

static float gl33_mouse_dy(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
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

static void gl33_release_mouse(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window) return;
    glfwSetInputMode(ctx->window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    ctx->mouse_captured = 0;
}

static float gl33_mouse_x(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window) return 0.0f;
    if (ctx->sim_pos_active) return ctx->sim_x;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    return (float)cx;
}

static float gl33_mouse_y(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window) return 0.0f;
    if (ctx->sim_pos_active) return ctx->sim_y;
    double cx, cy;
    glfwGetCursorPos(ctx->window, &cx, &cy);
    return (float)cy;
}

static int gl33_mouse_button(void* vctx, int btn) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || !ctx->window) return 0;
    if (btn >= 0 && btn < 3 && ctx->sim_button[btn]) return 1;
    int glfw_btn = (btn == 0) ? GLFW_MOUSE_BUTTON_LEFT
                  : (btn == 1) ? GLFW_MOUSE_BUTTON_RIGHT
                  : GLFW_MOUSE_BUTTON_MIDDLE;
    return glfwGetMouseButton(ctx->window, glfw_btn) == GLFW_PRESS ? 1 : 0;
}

static void gl33_simulate_mouse_pos(void* vctx, float x, float y) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->sim_pos_active = 1;
    ctx->sim_x = x;
    ctx->sim_y = y;
}

static void gl33_simulate_mouse_button(void* vctx, int btn, int pressed) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx || btn < 0 || btn >= 3) return;
    ctx->sim_button[btn] = pressed ? 1 : 0;
}

static void gl33_simulate_scroll(void* vctx, float dy) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->scroll_acc_y += dy;
}

static void gl33_simulate_key(void* vctx, const char* key, int pressed) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    int glfw_key = gl33_keycode(key);
    if (glfw_key <= 0 || glfw_key > GLFW_KEY_LAST) return;
    ctx->sim_keys[glfw_key] = pressed ? 1 : 0;
}

static void gl33_simulate_mouse_delta(void* vctx, float dx, float dy) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->sim_delta_x += dx;
    ctx->sim_delta_y += dy;
}

static void gl33_simulate_reset(void* vctx) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    memset(ctx->sim_keys, -1, sizeof(ctx->sim_keys));
    ctx->sim_delta_x = 0.0f;
    ctx->sim_delta_y = 0.0f;
    ctx->sim_pos_active = 0;
    ctx->sim_button[0] = ctx->sim_button[1] = ctx->sim_button[2] = 0;
    ctx->scroll_acc_y = 0;
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
    /* Preserve the legacy path; HDR uses the unclamped public density. */
    ctx->height_fog.density = isfinite(density) ? fminf(fmaxf(density,0),1e6f) : 0;
    /* Zero is an explicit off sentinel, not a merely distant fog plane. */
    ctx->fog_dist = density <= 0.0f ? 0.0f : 1.0f / (density > 0.001f ? density : 0.001f);
    glfwMakeContextCurrent(ctx->window);
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.fog_dist, ctx->fog_dist);
    gl33_fog_upload(ctx);
}

static void gl33_set_fog_color(void* vctx, float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->fog_color[0] = r; ctx->fog_color[1] = g; ctx->fog_color[2] = b;
    glUseProgram(ctx->program);
    gl33_upload_vec3(ctx->uniforms.fog_color, r, g, b);
}

static void gl33_set_light_dir(void* vctx, float x, float y, float z) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->light_dir[0] = x; ctx->light_dir[1] = y; ctx->light_dir[2] = z;
    glUseProgram(ctx->program);
    gl33_upload_vec3(ctx->uniforms.light_dir, x, y, z);
}

static void gl33_set_alpha(void* vctx, float level) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    ctx->alpha = level;
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.alpha, level);
    /* Transparente (und additive) Draws duerfen den Depth-Buffer nicht fuellen. */
    glDepthMask((level >= 0.999f && !ctx->blend_additive) ? GL_TRUE : GL_FALSE);
}

static void gl33_set_light_color(void* vctx, float r, float g, float b) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->light_color[0] = r; ctx->light_color[1] = g; ctx->light_color[2] = b;
    glUseProgram(ctx->program);
    ctx->light_linear[0]=0;
    glUniform1fv(ctx->uniforms.light_linear,8,ctx->light_linear);
    glUniform3fv(ctx->uniforms.pbr,1,ctx->pbr);
    gl33_upload_vec3(ctx->uniforms.light_color, r, g, b);
}

static void gl33_set_spec(void* vctx, float strength, float power) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->spec[0] = strength; ctx->spec[1] = power;
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.spec_strength, strength);
    gl33_upload_float(ctx->uniforms.spec_power, power);
}

static void gl33_set_wave(void* vctx, float amp, float freq, float speed) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->wave[0] = amp; ctx->wave[1] = freq; ctx->wave[2] = speed;
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.wave_amp, amp);
    gl33_upload_float(ctx->uniforms.wave_freq, freq);
    gl33_upload_float(ctx->uniforms.wave_speed, speed);
}

static void gl33_set_ambient(void* vctx, float level) {
    GL33Context* ctx = (GL33Context*)vctx;
    if (!ctx) return;
    ctx->ambient = level;
    glUseProgram(ctx->program);
    gl33_upload_float(ctx->uniforms.ambient, level);
}

/* ========================================================
 * Backend Export
 * ======================================================== */

/* Forward-Decls fuer Helfer die nach der Vtable definiert sind. */
int gl33_screenshot_bmp(void* ctx_void, const char* path);
static uint8_t* gl33_grab_rgba(void* ctx_void, int* out_w, int* out_h);

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
    .triangle_colors = gl33_triangle_colors,
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
    .set_height_fog = gl33_set_height_fog,
    .set_fog_color   = gl33_set_fog_color,
    .sky             = gl33_sky,
    .set_alpha       = gl33_set_alpha,
    .set_wave        = gl33_set_wave,
    .set_light_color = gl33_set_light_color,
    .set_spec        = gl33_set_spec,
    .set_light_dir   = gl33_set_light_dir,
    .set_ambient     = gl33_set_ambient,
    .chunk_create  = gl33_chunk_create,
    .chunk_begin   = gl33_chunk_begin,
    .chunk_end     = gl33_chunk_end,
    .chunk_draw    = gl33_chunk_draw_fn,
    .chunk_delete  = gl33_chunk_delete_fn,
    .screenshot_bmp = gl33_screenshot_bmp,
    .grab_rgba      = gl33_grab_rgba,
    .simulate_mouse_pos = gl33_simulate_mouse_pos,
    .simulate_mouse_button = gl33_simulate_mouse_button,
    .simulate_scroll = gl33_simulate_scroll,
    .simulate_key = gl33_simulate_key,
    .simulate_mouse_delta = gl33_simulate_mouse_delta,
    .simulate_reset = gl33_simulate_reset,
    .scale          = gl33_scale,
    .set_tint       = gl33_set_tint,
    .set_extra_light = gl33_set_extra_light,
    .set_point_light = gl33_set_point_light,
    .set_emissive    = gl33_set_emissive,
    .set_blend_mode  = gl33_set_blend_mode,
    .line            = gl33_line,
    .point           = gl33_point,
    .set_smooth      = gl33_set_smooth,
    .texture_create  = gl33_texture_create,
    .set_texture     = gl33_set_texture,
    .set_shadow      = gl33_set_shadow,
    .set_bloom       = gl33_set_bloom,
    .set_render_time = gl33_set_render_time,
    .set_hdr         = gl33_set_hdr,
    .set_exposure    = gl33_set_exposure,
    .set_auto_exposure = gl33_set_auto_exposure,
    .exposure_value = gl33_exposure_value,
    .set_linear_light = gl33_set_linear_light,
    .set_pbr = gl33_set_pbr,
    .set_ao = gl33_set_ao,
    .set_ao_debug = gl33_set_ao_debug,
    .opaque_end = gl33_opaque_end,
    .scene_end       = gl33_scene_end,
    .renderer_info   = gl33_renderer_info,
    .water_background = gl33_water_background,
    .set_water = gl33_set_water,
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
    gl33_apply_post(ctx);
    int w = ctx->width;
    int h = ctx->height;
    if (ctx->hdr_on) {
        glfwGetFramebufferSize(ctx->window,&w,&h);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0); glReadBuffer(GL_BACK);
    }
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

/* Frame-Grab (Plan-008 A3A): aktueller Framebuffer als RGBA8, top-left origin.
 * glReadPixels liefert bottom-left -> hier einheitlich Y-flippen (gleiche
 * Konvention wie vulkan, das bereits top-down liefert). Buffer via malloc,
 * Aufrufer free. */
static uint8_t* gl33_grab_rgba(void* ctx_void, int* out_w, int* out_h) {
    if (!ctx_void) return NULL;
    GL33Context* ctx = (GL33Context*)ctx_void;
    if (!ctx->window) return NULL;
    glfwMakeContextCurrent(ctx->window);
    gl33_apply_post(ctx);
    int w = ctx->width;
    int h = ctx->height;
    if (ctx->hdr_on) {
        glfwGetFramebufferSize(ctx->window,&w,&h);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,0); glReadBuffer(GL_BACK);
    }
    if (w <= 0 || h <= 0) return NULL;
    uint8_t* buf = (uint8_t*)malloc((size_t)w * (size_t)h * 4);
    if (!buf) return NULL;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
    /* Y-Flip: glReadPixels-Origin ist bottom-left, MOO_FRAME ist top-left. */
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

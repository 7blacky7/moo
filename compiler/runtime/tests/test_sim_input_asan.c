/**
 * test_sim_input_asan.c - Standalone-ASan-Harness fuer die 3D-Input-Sim
 * (Plan-008 A1a Tastatur Tri-State + A1b Maus-Delta consume-on-read).
 *
 * Strategie wie die anderen Runtime-Harnesses: wir linken den ECHTEN
 * Dispatcher moo_3d.c (haengt nur an moo_runtime.h + moo_3d_backend.h, KEIN
 * GL/GLFW/Vulkan) und registrieren ueber moo_3d_attach_external() einen
 * eigenen, malloc'ten Fake-Backend-Kontext. Dieser Stub repliziert exakt die
 * Tri-State- bzw. consume-on-read-Semantik der echten Backends (gl21/gl33/
 * vulkan), sodass der Vertrag der Vtable-Erweiterung end-to-end ueber den
 * Dispatcher geprueft wird. Der malloc'te Kontext + die Strings laufen durch
 * ASan -> Leak-Check.
 *
 * Geprueft:
 *   1. NULL-Backend / NULL-Ctx: moo_3d_simulate_* sind sichere no-ops (kein
 *      Crash, kein UB) wenn kein Backend registriert ist.
 *   2. Tastatur Tri-State: nicht-simulierte Taste -> echter Input (Stub liefert
 *      konfigurierbaren glfwGetKey-Ersatz); simulate_key(.,1) -> Override true;
 *      simulate_key(.,0) -> Override false; reset -> echter Input wieder aktiv.
 *   3. Maus-Delta consume-on-read: simulate_mouse_delta(5,3) -> erster
 *      mouse_dx=5 / mouse_dy=3, zweiter Read=0 (konsumiert); Akkumulation
 *      mehrerer Deltas; reset nullt den Akkumulator.
 *   4. Absolute Maus-Pos (simulate_mouse_pos) bleibt vom Delta UNBERUEHRT.
 *
 * Kompilieren/Ausfuehren (eigenstaendig, ohne GL):
 *   gcc -fsanitize=address -g -std=c11 -Wall -Wextra -I.. \
 *       test_sim_input_asan.c ../moo_3d.c -lm -o /tmp/t_sim_input
 *   ASAN_OPTIONS=detect_leaks=1 /tmp/t_sim_input
 */

#include "moo_runtime.h"
#include "moo_3d_backend.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ===================== moo-Runtime-Stubs ===================== */
/* Nur die Symbole, die moo_3d.c extern referenziert. Der Dispatcher ruft im
 * Sim-Pfad keine davon auf ausser MV_*-Inlines + die Backend-Pointer; sie
 * muessen aber linkbar sein. */

MooValue moo_number(double n) { MooValue v; v.tag = MOO_NUMBER; moo_val_set_double(&v, n); return v; }
MooValue moo_bool(bool b)     { MooValue v; v.tag = MOO_BOOL;   v.data = (uint64_t)b; return v; }
MooValue moo_none(void)       { MooValue v; v.tag = MOO_NONE;   v.data = 0; return v; }
void     moo_throw(MooValue v) { (void)v; fprintf(stderr, "unerwarteter moo_throw\n"); abort(); }
double   moo_as_number(MooValue v) { return moo_val_as_double(v); }

/* String-Arena: Sim-Keys werden als MOO_STRING uebergeben; tracken + am Ende
 * freigeben (ASan-clean). */
#define MAX_TRACKED_STRINGS 256
static MooString* g_strings[MAX_TRACKED_STRINGS];
static int        g_string_count = 0;

MooValue moo_string_new(const char* s) {
    MooString* str = (MooString*)malloc(sizeof(MooString));
    str->refcount = 1;
    str->length = (int32_t)strlen(s);
    str->capacity = str->length + 1;
    str->chars = (char*)malloc((size_t)str->capacity);
    memcpy(str->chars, s, (size_t)str->capacity);
    if (g_string_count < MAX_TRACKED_STRINGS) g_strings[g_string_count++] = str;
    MooValue v; v.tag = MOO_STRING; moo_val_set_ptr(&v, str);
    return v;
}
static void free_tracked_strings(void) {
    for (int i = 0; i < g_string_count; i++) {
        free(g_strings[i]->chars);
        free(g_strings[i]);
    }
    g_string_count = 0;
}

static MooValue mv_str(const char* s)   { return moo_string_new(s); }
static MooValue mv_num(double n)         { MooValue v; v.tag = MOO_NUMBER; moo_val_set_double(&v, n); return v; }
static MooValue mv_bool(bool b)          { MooValue v; v.tag = MOO_BOOL;   v.data = (uint64_t)b; return v; }
static MooValue mv_win(void)             { MooValue v; v.tag = MOO_WINDOW3D; v.data = 1; return v; }

/* ===================== Fake-Backend-Stub ===================== */
/* Repliziert die echte Sim-Semantik: int8_t sim_keys[] Tri-State (-1/0/1),
 * float sim_delta_x/y consume-on-read, sim_pos_active + sim_x/y. Der "echte
 * Input" wird durch real_key (konfigurierbar) und real_pos simuliert. */

#define FAKE_KEY_COUNT 128

typedef struct {
    int8_t sim_keys[FAKE_KEY_COUNT];  /* -1 = nicht simuliert, 0 = up, 1 = down */
    int    real_key_down;             /* "echter" glfwGetKey-Ersatz fuer Test-Taste */
    int    test_keycode;              /* Index der Test-Taste */
    float  sim_delta_x, sim_delta_y;  /* consume-on-read Akkumulator */
    int    sim_pos_active;
    float  sim_x, sim_y;
    float  real_x, real_y;            /* "echte" absolute Pos */
    float  last_r, last_g, last_b;     /* zuletzt vom Dispatcher dekodierte Farbe */
    int    triangle_calls;
} FakeCtx;

/* Mini-Keycode: nur 'w' und 'a' fuer den Test; gleiche Idee wie gl33_keycode. */
static int fake_keycode(const char* key) {
    if (!key) return 0;
    if (key[1] == '\0' && key[0] >= 'a' && key[0] <= 'z') return 1 + (key[0] - 'a');
    return 0;
}

static int fake_key_pressed(void* vctx, const char* key) {
    FakeCtx* c = (FakeCtx*)vctx;
    if (!c || !key) return 0;
    int kc = fake_keycode(key);
    if (kc <= 0 || kc >= FAKE_KEY_COUNT) return 0;
    /* Tri-State: Sim-State ZUERST. */
    if (c->sim_keys[kc] >= 0) return c->sim_keys[kc];
    /* nicht simuliert -> "echter" Input (nur fuer die definierte Test-Taste). */
    if (kc == c->test_keycode) return c->real_key_down;
    return 0;
}

static float fake_mouse_dx(void* vctx) {
    FakeCtx* c = (FakeCtx*)vctx;
    if (!c) return 0.0f;
    if (c->sim_delta_x != 0.0f) { float v = c->sim_delta_x; c->sim_delta_x = 0.0f; return v; }
    return 0.0f; /* kein echtes Fenster im Harness */
}
static float fake_mouse_dy(void* vctx) {
    FakeCtx* c = (FakeCtx*)vctx;
    if (!c) return 0.0f;
    if (c->sim_delta_y != 0.0f) { float v = c->sim_delta_y; c->sim_delta_y = 0.0f; return v; }
    return 0.0f;
}
static float fake_mouse_x(void* vctx) {
    FakeCtx* c = (FakeCtx*)vctx;
    if (!c) return 0.0f;
    return c->sim_pos_active ? c->sim_x : c->real_x;
}
static float fake_mouse_y(void* vctx) {
    FakeCtx* c = (FakeCtx*)vctx;
    if (!c) return 0.0f;
    return c->sim_pos_active ? c->sim_y : c->real_y;
}

static void fake_triangle(void* vctx,
                          float x1, float y1, float z1,
                          float x2, float y2, float z2,
                          float x3, float y3, float z3,
                          float r, float g, float b) {
    FakeCtx* c = (FakeCtx*)vctx;
    (void)x1; (void)y1; (void)z1;
    (void)x2; (void)y2; (void)z2;
    (void)x3; (void)y3; (void)z3;
    if (!c) return;
    c->last_r = r; c->last_g = g; c->last_b = b;
    c->triangle_calls++;
}

static void fake_simulate_key(void* vctx, const char* key, int pressed) {
    FakeCtx* c = (FakeCtx*)vctx;
    if (!c) return;
    int kc = fake_keycode(key);
    if (kc <= 0 || kc >= FAKE_KEY_COUNT) return;
    c->sim_keys[kc] = pressed ? 1 : 0;
}
static void fake_simulate_mouse_pos(void* vctx, float x, float y) {
    FakeCtx* c = (FakeCtx*)vctx;
    if (!c) return;
    c->sim_pos_active = 1; c->sim_x = x; c->sim_y = y;
}
static void fake_simulate_mouse_delta(void* vctx, float dx, float dy) {
    FakeCtx* c = (FakeCtx*)vctx;
    if (!c) return;
    c->sim_delta_x += dx; c->sim_delta_y += dy;
}
static void fake_simulate_reset(void* vctx) {
    FakeCtx* c = (FakeCtx*)vctx;
    if (!c) return;
    memset(c->sim_keys, -1, sizeof(c->sim_keys));
    c->sim_delta_x = 0.0f; c->sim_delta_y = 0.0f;
    c->sim_pos_active = 0;
}

static Moo3DBackend g_fake_backend = {
    .triangle             = fake_triangle,
    .key_pressed          = fake_key_pressed,
    .mouse_dx             = fake_mouse_dx,
    .mouse_dy             = fake_mouse_dy,
    .mouse_x              = fake_mouse_x,
    .mouse_y              = fake_mouse_y,
    .simulate_mouse_pos   = fake_simulate_mouse_pos,
    .simulate_key         = fake_simulate_key,
    .simulate_mouse_delta = fake_simulate_mouse_delta,
    .simulate_reset       = fake_simulate_reset,
};

/* Dispatcher-Hook aus moo_3d.c */
extern void moo_3d_attach_external(void* backend, void* ctx);

/* ===================== Test-Harness ===================== */

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (Zeile %d)\n", msg, __LINE__); g_fail = 1; } \
    else         { printf("  ok: %s\n", msg); } \
} while (0)

int main(void) {
    printf("== test_sim_input_asan (Plan-008 A1a/A1b) ==\n");

    /* --- 1. NULL-Backend: Dispatcher muss sichere no-ops sein. --- */
    moo_3d_attach_external(NULL, NULL);
    moo_3d_simulate_key(mv_win(), mv_str("w"), mv_bool(true));
    moo_3d_simulate_mouse_delta(mv_win(), mv_num(5.0), mv_num(3.0));
    moo_3d_simulate_reset(mv_win());
    CHECK(1, "NULL-Backend: simulate_* ohne Crash (no-op)");

    /* Fake-Backend registrieren (malloc -> ASan beobachtet Lifetime). */
    FakeCtx* ctx = (FakeCtx*)malloc(sizeof(FakeCtx));
    memset(ctx, 0, sizeof(*ctx));
    memset(ctx->sim_keys, -1, sizeof(ctx->sim_keys)); /* nicht simuliert */
    ctx->test_keycode = fake_keycode("w");
    ctx->real_key_down = 0;
    moo_3d_attach_external(&g_fake_backend, ctx);

    /* --- 2. Farben: String- und neuer 0xRRGGBB-Zahlenpfad end-to-end. --- */
    MooValue z = mv_num(0.0);
    moo_3d_triangle(mv_win(), z, z, z, z, z, z, z, z, z, mv_num(0x12ABEF));
    CHECK(ctx->triangle_calls == 1 &&
          ctx->last_r == 0x12 / 255.0f &&
          ctx->last_g == 0xAB / 255.0f &&
          ctx->last_b == 0xEF / 255.0f,
          "Farbe: Zahl 0x12ABEF wird exakt als RGB dekodiert");
    moo_3d_triangle(mv_win(), z, z, z, z, z, z, z, z, z, mv_str("#12ABEF"));
    CHECK(ctx->triangle_calls == 2 &&
          ctx->last_r == 0x12 / 255.0f &&
          ctx->last_g == 0xAB / 255.0f &&
          ctx->last_b == 0xEF / 255.0f,
          "Farbe: Hex-String bleibt regressionsfrei");
    moo_3d_triangle(mv_win(), z, z, z, z, z, z, z, z, z, mv_num(1.5));
    CHECK(ctx->triangle_calls == 3 &&
          ctx->last_r == 1.0f && ctx->last_g == 1.0f && ctx->last_b == 1.0f,
          "Farbe: gebrochene Zahl behaelt sicheren Weiss-Default");

    /* --- 3. Tastatur Tri-State --- */
    /* (a) nicht simuliert + echter Input aus -> false */
    CHECK(g_fake_backend.key_pressed(ctx, "w") == 0,
          "Tri-State: nicht simuliert, echter Input aus -> false");

    /* (b) nicht simuliert + echter Input an -> true (Fallthrough auf echten Input) */
    ctx->real_key_down = 1;
    CHECK(g_fake_backend.key_pressed(ctx, "w") == 1,
          "Tri-State: nicht simuliert -> echter Input durchgereicht");

    /* (c) simulate down ueberschreibt echten Input (auch wenn echter aus waere) */
    ctx->real_key_down = 0;
    moo_3d_simulate_key(mv_win(), mv_str("w"), mv_bool(true));
    CHECK(g_fake_backend.key_pressed(ctx, "w") == 1,
          "Tri-State: simulate down -> Override true");

    /* (d) simulate up ueberschreibt echten Input (auch wenn echter an waere) */
    ctx->real_key_down = 1;
    moo_3d_simulate_key(mv_win(), mv_str("w"), mv_num(0.0));
    CHECK(g_fake_backend.key_pressed(ctx, "w") == 0,
          "Tri-State: simulate up -> Override false (echter Input an, aber unterdrueckt)");

    /* (e) reset -> echter Input wieder aktiv */
    moo_3d_simulate_reset(mv_win());
    CHECK(g_fake_backend.key_pressed(ctx, "w") == 1,
          "Tri-State: nach reset -> echter Input (an) wieder aktiv");
    ctx->real_key_down = 0;
    CHECK(g_fake_backend.key_pressed(ctx, "w") == 0,
          "Tri-State: nach reset -> echter Input (aus) wieder aktiv");

    /* --- 3. Maus-Delta consume-on-read --- */
    moo_3d_simulate_mouse_delta(mv_win(), mv_num(5.0), mv_num(3.0));
    float dx1 = g_fake_backend.mouse_dx(ctx);
    float dy1 = g_fake_backend.mouse_dy(ctx);
    CHECK(dx1 == 5.0f && dy1 == 3.0f, "Delta: erster Read liefert (5,3)");
    float dx2 = g_fake_backend.mouse_dx(ctx);
    float dy2 = g_fake_backend.mouse_dy(ctx);
    CHECK(dx2 == 0.0f && dy2 == 0.0f, "Delta: zweiter Read liefert (0,0) (consume-on-read)");

    /* Akkumulation mehrerer Deltas vor dem Auslesen */
    moo_3d_simulate_mouse_delta(mv_win(), mv_num(2.0), mv_num(-1.0));
    moo_3d_simulate_mouse_delta(mv_win(), mv_num(3.0), mv_num(4.0));
    CHECK(g_fake_backend.mouse_dx(ctx) == 5.0f, "Delta: akkumuliert dx (2+3=5)");
    CHECK(g_fake_backend.mouse_dy(ctx) == 3.0f, "Delta: akkumuliert dy (-1+4=3)");

    /* reset nullt den Akkumulator */
    moo_3d_simulate_mouse_delta(mv_win(), mv_num(9.0), mv_num(9.0));
    moo_3d_simulate_reset(mv_win());
    CHECK(g_fake_backend.mouse_dx(ctx) == 0.0f && g_fake_backend.mouse_dy(ctx) == 0.0f,
          "Delta: reset nullt den Akkumulator");

    /* --- 4. Absolute Maus-Pos bleibt vom Delta UNBERUEHRT --- */
    ctx->real_x = 100.0f; ctx->real_y = 200.0f;
    moo_3d_simulate_mouse_delta(mv_win(), mv_num(50.0), mv_num(50.0));
    CHECK(g_fake_backend.mouse_x(ctx) == 100.0f && g_fake_backend.mouse_y(ctx) == 200.0f,
          "Pos: Delta-Sim beruehrt absolute Pos NICHT");
    g_fake_backend.simulate_mouse_pos(ctx, 640.0f, 360.0f);
    CHECK(g_fake_backend.mouse_x(ctx) == 640.0f && g_fake_backend.mouse_y(ctx) == 360.0f,
          "Pos: simulate_mouse_pos getrennt aktiv");

    /* --- Cleanup (ASan: kein Leak) --- */
    moo_3d_attach_external(NULL, NULL);
    free(ctx);
    free_tracked_strings();

    if (g_fail) { printf("== FEHLGESCHLAGEN ==\n"); return 1; }
    printf("== ALLE TESTS BESTANDEN ==\n");
    return 0;
}

/**
 * test_frame_tensor_asan.c — KI-MULTI-V1: Frame↔Tensor-Brücke (ASan+UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: ueber run_sanitize.sh (Eintrag in EXTRA_HARNESSES).
 * Quell-Satz: moo_frame_tensor.c + Frame- und Tensor-Familie (siehe
 * run_sanitize.sh) — OHNE moo_error.c (Test-throw-Modell wie test_tensor_asan).
 *
 * PRUEFT:
 *   1. tensor_aus_frame: Form [h,b,4], Werte exakt px/255, top-left origin
 *   2. "grau": Form [h,b,1], Rec.-601-Formel exakt
 *   3. Roundtrip frame -> tensor -> frame BYTE-IDENTISCH
 *   4. frame_aus_tensor: [h,b,1]-Grau repliziert Kanaele, Alpha=255;
 *      Clamp ausserhalb 0..1
 *   5. Fehlerpfade werfen sauber (kein Frame, unbekannter Modus, ndim=1)
 *   6. Refcount-Lifecycle: alles released -> ASan detect_leaks=1 clean
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <math.h>

/* --- Test-throw-Modell (ersetzt moo_error.c) ------------------------------ */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    moo_release(error);
    moo_error_flag = 1;
}
static void fehler_reset(void) { moo_error_flag = 0; }

/* --- Stubs fuer moo_release()-Dispatch-Ziele, die wir nicht linken -------- */
void moo_socket_free(void* p)       { (void)p; }
void moo_thread_free(void* p)       { (void)p; }
void moo_channel_free(void* p)      { (void)p; }
void moo_db_free(void* p)           { (void)p; }
void moo_db_stmt_free(void* p)      { (void)p; }
void moo_window_free(void* p)       { (void)p; }
void moo_web_free(void* p)          { (void)p; }
void moo_voxel_free(void* p)        { (void)p; }

static int checks = 0;
#define CHECK(cond, name) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (Zeile %d)\n", name, __LINE__); return 1; } \
    checks++; \
} while (0)

/* Deterministischer 3x2-RGBA-Testframe (top-left origin). */
static MooValue test_frame(void) {
    const int W = 3, H = 2;
    uint8_t* px = (uint8_t*)malloc((size_t)W * H * 4);
    uint8_t werte[] = {
        255, 0, 0, 255,    0, 255, 0, 255,    0, 0, 255, 255,   /* Zeile 0: R G B */
        10, 20, 30, 40,    128, 128, 128, 255, 255, 255, 255, 0 /* Zeile 1 */
    };
    memcpy(px, werte, sizeof(werte));
    return moo_frame_new_take(W, H, px);
}

int main(void) {
    /* ---- 1. tensor_aus_frame: Form + Werte + Origin ---- */
    MooValue frame = test_frame();
    CHECK(frame.tag == MOO_FRAME, "Testframe erzeugt");
    MooValue t = moo_tensor_aus_frame(frame, moo_none());
    CHECK(t.tag == MOO_TENSOR, "tensor_aus_frame liefert Tensor");
    MooTensor* tp = MV_TENSOR(t);
    CHECK(tp->ndim == 3 && tp->shape[0] == 2 && tp->shape[1] == 3 && tp->shape[2] == 4, "Form [2,3,4]");
    CHECK(fabsf(tp->data[0] - 1.0f) < 1e-6f, "Pixel(0,0) Rot = 1.0");
    CHECK(fabsf(tp->data[1]) < 1e-6f, "Pixel(0,0) Gruen = 0");
    /* Pixel (x=1, y=1) = (128,128,128,255): base = (1*3+1)*4 */
    CHECK(fabsf(tp->data[(1 * 3 + 1) * 4 + 0] - 128.0f / 255.0f) < 1e-6f, "Pixel(1,1) top-left origin");
    CHECK(fabsf(tp->data[(1 * 3 + 0) * 4 + 3] - 40.0f / 255.0f) < 1e-6f, "Alpha-Kanal uebernommen");

    /* ---- 2. Grau: Form [h,b,1] + Rec.-601-Formel ---- */
    MooValue gs = moo_string_new("grau");
    MooValue tg = moo_tensor_aus_frame(frame, gs);
    CHECK(tg.tag == MOO_TENSOR, "grau liefert Tensor");
    MooTensor* tgp = MV_TENSOR(tg);
    CHECK(tgp->ndim == 3 && tgp->shape[2] == 1, "grau Form [2,3,1]");
    float lum_rot = 0.299f * 255.0f / 255.0f;
    CHECK(fabsf(tgp->data[0] - lum_rot) < 1e-6f, "Luminanz Rot = 0.299");
    float lum_mix = (0.299f * 10.0f + 0.587f * 20.0f + 0.114f * 30.0f) / 255.0f;
    CHECK(fabsf(tgp->data[1 * 3 + 0] - lum_mix) < 1e-6f, "Luminanz-Mischformel exakt");

    /* ---- 3. Roundtrip byte-identisch ---- */
    MooValue frame2 = moo_frame_aus_tensor(t);
    CHECK(frame2.tag == MOO_FRAME, "frame_aus_tensor liefert Frame");
    MooFrame* f1 = MV_FRAME(frame);
    MooFrame* f2 = MV_FRAME(frame2);
    CHECK(f1->width == f2->width && f1->height == f2->height, "Roundtrip-Dimensionen");
    CHECK(memcmp(f1->pixels, f2->pixels, (size_t)f1->width * f1->height * 4) == 0,
          "Roundtrip frame->tensor->frame BYTE-IDENTISCH");

    /* ---- 4. Grau-Tensor -> Frame: Kanal-Replikation + Alpha 255 + Clamp ---- */
    MooValue fg = moo_frame_aus_tensor(tg);
    CHECK(fg.tag == MOO_FRAME, "Grau-Frame erzeugt");
    MooFrame* fgp = MV_FRAME(fg);
    CHECK(fgp->pixels[0] == fgp->pixels[1] && fgp->pixels[1] == fgp->pixels[2], "Grau repliziert RGB");
    CHECK(fgp->pixels[3] == 255, "Grau: Alpha = 255");
    /* Clamp: Werte ausserhalb 0..1 */
    tgp->data[0] = 2.5f;
    tgp->data[1] = -1.0f;
    MooValue fc = moo_frame_aus_tensor(tg);
    MooFrame* fcp = MV_FRAME(fc);
    CHECK(fcp->pixels[0] == 255, "Clamp oben -> 255");
    CHECK(fcp->pixels[4] == 0, "Clamp unten -> 0");

    /* ---- 5. Fehlerpfade werfen sauber ---- */
    fehler_reset();
    MooValue e1 = moo_tensor_aus_frame(moo_number(5), moo_none());
    CHECK(moo_error_flag == 1 && e1.tag == MOO_NONE, "kein Frame -> throw");
    fehler_reset();
    MooValue falscher_modus = moo_string_new("sepia");
    MooValue e2 = moo_tensor_aus_frame(frame, falscher_modus);
    CHECK(moo_error_flag == 1 && e2.tag == MOO_NONE, "unbekannter Modus -> throw");
    fehler_reset();
    MooValue form1 = moo_list_new(1);
    moo_list_append(form1, moo_number(7));
    MooValue t1d = moo_tensor_neu(form1, moo_number(0.5));
    MooValue e3 = moo_frame_aus_tensor(t1d);
    CHECK(moo_error_flag == 1 && e3.tag == MOO_NONE, "1D-Tensor -> throw");
    fehler_reset();

    /* ---- 6. Lifecycle: alles freigeben (ASan-Leak-Gate) ---- */
    moo_release(gs);
    moo_release(falscher_modus);
    moo_release(form1);
    moo_release(t1d);
    moo_release(t);
    moo_release(tg);
    moo_release(frame);
    moo_release(frame2);
    moo_release(fg);
    moo_release(fc);

    printf("=== FRAME<->TENSOR ASan-Harness: ALLE %d CHECKS BESTANDEN ===\n", checks);
    return 0;
}

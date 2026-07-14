/**
 * test_tensor_asan.c — Plan-014 A1: Tensor-Kern-Harness (ASan + UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: ueber run_sanitize.sh (Eintrag in EXTRA_HARNESSES).
 * Quell-Satz: moo_tensor.c moo_memory.c moo_value.c moo_print.c moo_string.c
 *             moo_dict.c moo_list.c moo_ops.c  (-lm) — OHNE moo_error.c!
 *
 * moo_throw-MODELL FUER TESTS (wie in den Voxel-Harnesses): das echte
 * moo_error.c wird nicht gelinkt; wir definieren moo_throw/try-Symbole
 * selbst. Unser moo_throw setzt das Flag und gibt den strdup-Fehlertext
 * SOFORT frei (MOO_ERROR ist kein refcounteter Heap-Typ) — damit bleibt
 * ASan detect_leaks=1 auch ueber Fehlerpfade das harte Gate.
 *
 * PRUEFT:
 *   1. Konstruktoren (neu/nullen/einsen/zufall/aus_liste 1D+2D)
 *   2. Zugriff holen/setzen inkl. Strides (2D row-major)
 *   3. Determinismus: gleicher Seed => identisch, anderer Seed => anders
 *   4. Refcount-Lifecycle inkl. grad-Buffer-Free (ASan-Leak-Gate)
 *   5. Fehlerfaelle werfen sauber und liefern harmlose Rueckgaben
 *   6. to_string (kompakt + Klein-Anzeige)
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <stdarg.h>

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
void moo_frame_free(void* p)        { (void)p; }
void moo_gif_handle_free(void* p)   { (void)p; }
void moo_video_handle_free(void* p) { (void)p; }

static int checks = 0;
#define CHECK(cond, name) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (Zeile %d)\n", name, __LINE__); return 1; } \
    checks++; \
} while (0)

/* moo-Liste aus ints bauen (Rueckgabe +1 owning). */
static MooValue liste_i(int n, ...) {
    va_list ap;
    MooValue l = moo_list_new(n);
    va_start(ap, n);
    for (int i = 0; i < n; i++)
        moo_list_append(l, moo_number((double)va_arg(ap, int)));
    va_end(ap);
    return l;
}

int main(void) {
    /* ---- 1. Konstruktoren ---- */
    MooValue s23 = liste_i(2, 2, 3);
    MooValue t = moo_tensor_neu(s23, moo_number(0.5));
    CHECK(t.tag == MOO_TENSOR, "neu liefert Tensor");
    MooTensor* tp = MV_TENSOR(t);
    CHECK(tp->refcount == 1 && tp->ndim == 2 && tp->size == 6, "neu Metadaten");
    CHECK(tp->shape[0] == 2 && tp->shape[1] == 3, "neu shape");
    CHECK(tp->strides[0] == 3 && tp->strides[1] == 1, "neu strides row-major");
    for (int i = 0; i < 6; i++) CHECK(tp->data[i] == 0.5f, "neu fill");

    MooValue tz = moo_tensor_nullen(s23);
    CHECK(MV_TENSOR(tz)->data[4] == 0.0f, "nullen");
    MooValue te = moo_tensor_einsen(s23);
    CHECK(MV_TENSOR(te)->data[4] == 1.0f, "einsen");

    /* ---- 2. holen/setzen (Strides) ---- */
    MooValue idx11 = liste_i(2, 1, 1);
    (void)moo_tensor_setzen(t, idx11, moo_number(7.25));
    CHECK(tp->data[1 * 3 + 1] == 7.25f, "setzen flat korrekt");
    MooValue got = moo_tensor_holen(t, idx11);
    CHECK(got.tag == MOO_NUMBER && MV_NUM(got) == 7.25, "holen");
    moo_release(idx11);

    /* ---- 3. Determinismus ---- */
    MooValue r1 = moo_tensor_zufall(s23, moo_number(1234));
    MooValue r2 = moo_tensor_zufall(s23, moo_number(1234));
    MooValue r3 = moo_tensor_zufall(s23, moo_number(1235));
    CHECK(memcmp(MV_TENSOR(r1)->data, MV_TENSOR(r2)->data, 6 * sizeof(float)) == 0,
          "zufall deterministisch (gleicher Seed)");
    CHECK(memcmp(MV_TENSOR(r1)->data, MV_TENSOR(r3)->data, 6 * sizeof(float)) != 0,
          "zufall divergiert (anderer Seed)");
    for (int i = 0; i < 6; i++) {
        float v = MV_TENSOR(r1)->data[i];
        CHECK(v >= -1.0f && v < 1.0f, "zufall Wertebereich [-1,1)");
    }
    moo_release(r1); moo_release(r2); moo_release(r3);

    /* ---- aus_liste 1D + 2D ---- */
    MooValue flach = liste_i(3, 10, 20, 30);
    MooValue t1d = moo_tensor_aus_liste(flach);
    CHECK(MV_TENSOR(t1d)->ndim == 1 && MV_TENSOR(t1d)->data[2] == 30.0f, "aus_liste 1D");
    moo_release(flach);

    MooValue z1 = liste_i(2, 1, 2);
    MooValue z2 = liste_i(2, 3, 4);
    MooValue nest = moo_list_new(2);
    moo_list_append(nest, z1);   /* append = Transfer (moo_list.c-Konvention) */
    moo_list_append(nest, z2);
    MooValue t2d = moo_tensor_aus_liste(nest);
    CHECK(MV_TENSOR(t2d)->ndim == 2 && MV_TENSOR(t2d)->data[3] == 4.0f, "aus_liste 2D");
    moo_release(nest);

    /* ---- form / groesse / zu_liste ---- */
    MooValue form = moo_tensor_form(t2d);
    CHECK(form.tag == MOO_LIST && MV_LIST(form)->length == 2, "form Laenge");
    CHECK(MV_NUM(MV_LIST(form)->items[1]) == 2.0, "form Werte");
    moo_release(form);
    CHECK(MV_NUM(moo_tensor_groesse(t2d)) == 4.0, "groesse");
    MooValue fl = moo_tensor_zu_liste(t2d);
    CHECK(MV_LIST(fl)->length == 4 && MV_NUM(MV_LIST(fl)->items[3]) == 4.0, "zu_liste");
    moo_release(fl);

    /* ---- 6. to_string (auf LEBENDEN Objekten) ---- */
    MooValue str2d = moo_tensor_to_string(t2d);
    CHECK(str2d.tag == MOO_STRING, "to_string liefert String");
    CHECK(strstr(MV_STR(str2d)->chars, "Tensor[2x2]") != NULL, "to_string Kopf");
    CHECK(strstr(MV_STR(str2d)->chars, "[[1, 2], [3, 4]]") != NULL, "to_string Klein-Anzeige");
    moo_release(str2d);
    MooValue gross_shape = liste_i(2, 100, 100);
    MooValue tg = moo_tensor_nullen(gross_shape);
    MooValue strg = moo_tensor_to_string(tg);
    CHECK(strcmp(MV_STR(strg)->chars, "Tensor[100x100]") == 0, "to_string gross = nur Kopf");
    moo_release(strg); moo_release(tg); moo_release(gross_shape);

    /* ---- 4. Refcount-Lifecycle + grad-Buffer-Free ---- */
    moo_retain(t2d);
    CHECK(MV_TENSOR(t2d)->refcount == 2, "retain");
    moo_release(t2d);
    CHECK(MV_TENSOR(t2d)->refcount == 1, "release");
    MV_TENSOR(t2d)->grad = (float*)calloc(4, sizeof(float));
    moo_release(t2d);   /* refcount 0 -> moo_tensor_free inkl. grad (ASan-Gate) */

    /* ---- 5. Fehlerfaelle ---- */
    fehler_reset();
    MooValue bad = moo_tensor_neu(moo_number(5), moo_number(0));
    CHECK(moo_error_flag, "neu wirft bei Nicht-Liste");
    CHECK(bad.tag == MOO_NONE, "neu Fehlerrueckgabe none");

    fehler_reset();
    MooValue oob_idx = liste_i(2, 0, 99);
    (void)moo_tensor_holen(t, oob_idx);
    CHECK(moo_error_flag, "holen wirft bei out-of-bounds");
    moo_release(oob_idx);

    fehler_reset();
    MooValue neg = liste_i(1, -3);
    (void)moo_tensor_nullen(neg);
    CHECK(moo_error_flag, "shape mit negativer Dimension wirft");
    moo_release(neg);

    fehler_reset();
    MooValue falsch_dim = liste_i(1, 0);
    (void)moo_tensor_holen(t, falsch_dim);
    CHECK(moo_error_flag, "holen wirft bei falscher Index-Anzahl");
    moo_release(falsch_dim);
    fehler_reset();

    /* Aufraeumen */
    moo_release(t); moo_release(tz); moo_release(te);
    moo_release(t1d);
    moo_release(s23);

    printf("test_tensor_asan: %d Checks OK\n", checks);
    return 0;
}

/**
 * test_contrastive_asan.c — KI-MULTI-L1.
 * Forward-Vertrag plus numerischer FD-Gradcheck der gesamten InfoNCE-
 * Komposition. Es wird bewusst kein einzelner Registry-Op geprueft.
 */
#include "../moo_runtime.h"

int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error));
    moo_error_flag = 1;
}

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
static int fails = 0;
#define CHECK(c, n) do {     if (!(c)) { fprintf(stderr, "FAIL: %s (Zeile %d)\n", n, __LINE__); fails++; }     else checks++; } while (0)

static MooValue t2(int32_t r, int32_t c, const float* daten) {
    int32_t shape[2] = { r, c };
    MooTensor* t = moo_tensor_raw(2, shape);
    for (int64_t i = 0; i < (int64_t)r * c; i++) t->data[i] = daten[i];
    MooValue v;
    v.tag = MOO_TENSOR;
    moo_val_set_ptr(&v, t);
    return v;
}

static double skalarwert(MooValue v) {
    CHECK(v.tag == MOO_TENSOR && MV_TENSOR(v)->size == 1,
          "kontrastiv liefert Skalar-Tensor");
    return (v.tag == MOO_TENSOR && MV_TENSOR(v)->size == 1)
        ? (double)MV_TENSOR(v)->data[0] : NAN;
}

static double loss_ohne_tape(MooValue a, MooValue b, double temperatur) {
    MooValue l = moo_nn_kontrastiv(a, b, moo_number(temperatur));
    double x = skalarwert(l);
    moo_release(l);
    return x;
}

static void gradcheck_input(MooValue a, MooValue b, MooTensor* t,
                            const float* analytisch, const char* name) {
    const double h = 1e-3;
    const double tol = 3e-2;
    for (int64_t i = 0; i < t->size; i++) {
        float alt = t->data[i];
        t->data[i] = alt + (float)h;
        double lp = loss_ohne_tape(a, b, 0.2);
        t->data[i] = alt - (float)h;
        double lm = loss_ohne_tape(a, b, 0.2);
        t->data[i] = alt;
        double num = (lp - lm) / (2.0 * h);
        double ag = (double)analytisch[i];
        double rel = fabs(num - ag) / fmax(1.0, fmax(fabs(num), fabs(ag)));
        if (!isfinite(num) || !isfinite(ag) || rel > tol) {
            fprintf(stderr,
                    "GRADCHECK FAIL %s[%lld]: num=%.7f ag=%.7f rel=%.3e\n",
                    name, (long long)i, num, ag, rel);
            fails++;
        } else {
            checks++;
        }
    }
}

int main(void) {
    const float av[] = {
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f
    };
    const float bv[] = {
        2.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f,
        0.0f, 0.0f, 4.0f
    };
    MooValue a = t2(3, 3, av);
    MooValue b = t2(3, 3, bv);

    moo_ag_aus();
    MooValue k = moo_nn_kosinus(a, b);
    CHECK(k.tag == MOO_TENSOR, "kosinus liefert Tensor");
    CHECK(MV_TENSOR(k)->ndim == 1 && MV_TENSOR(k)->shape[0] == 3,
          "kosinus Form [batch]");
    for (int i = 0; i < 3; i++)
        CHECK(fabs((double)MV_TENSOR(k)->data[i] - 1.0) < 1e-5,
              "kosinus identische Richtungen = 1");
    moo_release(k);

    double gut = loss_ohne_tape(a, b, 0.1);
    const float vertauscht[] = {
        0.0f, 3.0f, 0.0f,
        2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f
    };
    MooValue schlecht_b = t2(3, 3, vertauscht);
    double schlecht = loss_ohne_tape(a, schlecht_b, 0.1);
    CHECK(isfinite(gut) && gut < 1e-3, "gepaarte orthogonale Embeddings: kleiner Loss");
    CHECK(schlecht > gut + 1.0, "falsche Paarung: deutlich groesserer Loss");
    moo_release(schlecht_b);

    moo_error_flag = 0;
    MooValue ungueltig = moo_nn_kontrastiv(a, b, moo_number(0.0));
    CHECK(moo_error_flag == 1 && ungueltig.tag == MOO_NONE,
          "Temperatur <= 0 wird abgelehnt");
    moo_error_flag = 0;

    /* FD gegen Autograd fuer die KOMPOSITION, beide differenzierbaren Inputs. */
    const float ga[] = {
        0.7f, -0.2f, 0.4f,
        -0.3f, 0.8f, 0.1f,
        0.2f, 0.5f, -0.6f
    };
    const float gb[] = {
        0.6f, -0.1f, 0.3f,
        -0.2f, 0.9f, 0.2f,
        0.1f, 0.4f, -0.7f
    };
    MooValue x = t2(3, 3, ga);
    MooValue y = t2(3, 3, gb);
    MV_TENSOR(x)->requires_grad = true;
    MV_TENSOR(y)->requires_grad = true;

    moo_ag_an();
    moo_ag_reset();
    MooValue loss = moo_nn_kontrastiv(x, y, moo_number(0.2));
    CHECK(loss.tag == MOO_TENSOR, "Gradcheck-Forward erfolgreich");
    moo_tensor_rueckwaerts(loss);
    CHECK(MV_TENSOR(x)->grad != NULL && MV_TENSOR(y)->grad != NULL,
          "Komposition erzeugt Gradienten fuer a und b");

    int64_t nx = MV_TENSOR(x)->size;
    int64_t ny = MV_TENSOR(y)->size;
    float* gx = (float*)malloc((size_t)nx * sizeof(float));
    float* gy = (float*)malloc((size_t)ny * sizeof(float));
    memcpy(gx, MV_TENSOR(x)->grad, (size_t)nx * sizeof(float));
    memcpy(gy, MV_TENSOR(y)->grad, (size_t)ny * sizeof(float));
    moo_release(loss);
    moo_ag_reset();

    moo_ag_aus();
    gradcheck_input(x, y, MV_TENSOR(x), gx, "a");
    gradcheck_input(x, y, MV_TENSOR(y), gy, "b");
    free(gx);
    free(gy);
    moo_ag_an();
    moo_ag_reset();

    moo_release(a);
    moo_release(b);
    moo_release(x);
    moo_release(y);

    if (fails) {
        fprintf(stderr, "test_contrastive_asan: %d Fehler, %d Checks\n", fails, checks);
        return 1;
    }
    printf("test_contrastive_asan: PASS (%d Checks, Kompositions-FD-Gradcheck)\n",
           checks);
    return 0;
}

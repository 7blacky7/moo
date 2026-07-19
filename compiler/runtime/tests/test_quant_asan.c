/**
 * test_quant_asan.c — KI-Q1-Gate (PAPER-VERIFY-04): hadamard + QJL.
 * ============================================================================
 * GATES (alle deterministisch geseedet):
 *   H1  Determinismus: gleicher Seed -> bit-identisch; anderer Seed -> anders.
 *   H2  Norm-Erhalt: ||hadamard(x)||_zeile == ||x||_zeile (orthogonal, 1e-4).
 *   H3  Inverse-Roundtrip: D*H*y/sqrt(n) == x (1e-4) — beweist die bw-Formel
 *       unabhaengig vom Autograd (der Registry-FD-Gradcheck in
 *       test_gradcheck.c prueft "hadamard" zusaetzlich automatisch, B2).
 *   H4  Vertrag: letzte Achse keine Zweierpotenz -> Fehler.
 *   H5  Vertrag: nicht-ganzzahliger Seed -> Fehler.
 *   Q1  Determinismus: sign_jl gleicher Seed -> identische Bit-Woerter+Normen;
 *       Woerter sind Ganzzahlen in [0, 65535] (16-Bit-Packung im f32-Traeger).
 *   Q2  UNBIASEDNESS: Mittel des Schaetzers ueber 200 Seeds trifft <q,k>
 *       (rel < 0.08; statistische Schranke ~||q||*||k||/sqrt(m*R) ~ 0.6%).
 *   Q3  Vertrag: autograd_an -> Fehler auf allen drei QJL-Funktionen
 *       (Inferenz-only, Konsistenz mit KV-Cache KI-M2c).
 *   Q4  Vertrag: m=0 -> Fehler; Formen [zeilen, ceil(m/16)] / [zeilen] / [q,k].
 * Test-throw-Modell wie test_gradcheck.c (kein moo_error.c gelinkt).
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <math.h>

int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    moo_release(error);
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

static int checks = 0, fails = 0;
#define CHECK(cond, msg) do { \
    if (cond) { checks++; } \
    else { fails++; fprintf(stderr, "FAIL: %s\n", (msg)); } \
} while (0)

static MooValue mk(int32_t ndim, const int32_t* shape) {
    MooTensor* t = moo_tensor_raw(ndim, shape);
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t);
    return v;
}

/* Dict-Feld holen: moo_dict_get konsumiert den Key und liefert +1 owning. */
static MooValue feld(MooValue dict, const char* name) {
    return moo_dict_get(dict, moo_string_new(name));
}

int main(void) {
    moo_ag_aus();   /* Q1 ist Inferenz-Mechanik; kein Tape noetig */

    /* ---------- Hadamard: H1-H5 ---------- */
    int32_t s28[2] = { 2, 8 };
    MooValue x = mk(2, s28);
    for (int i = 0; i < 16; i++)
        MV_TENSOR(x)->data[i] = sinf(0.7f * (float)i) + 0.2f;

    moo_error_flag = 0;
    MooValue y1 = moo_tensor_hadamard(x, moo_number(42));
    MooValue y2 = moo_tensor_hadamard(x, moo_number(42));
    MooValue y3 = moo_tensor_hadamard(x, moo_number(43));
    CHECK(moo_error_flag == 0, "H: kein Fehler bei gueltigen Eingaben");
    {
        int gleich12 = 1, gleich13 = 1;
        for (int i = 0; i < 16; i++) {
            if (MV_TENSOR(y1)->data[i] != MV_TENSOR(y2)->data[i]) gleich12 = 0;
            if (MV_TENSOR(y1)->data[i] != MV_TENSOR(y3)->data[i]) gleich13 = 0;
        }
        CHECK(gleich12 == 1, "H1: gleicher Seed -> bit-identisch");
        CHECK(gleich13 == 0, "H1: anderer Seed -> andere Rotation");
    }

    for (int r = 0; r < 2; r++) {
        double nx = 0.0, ny = 0.0;
        for (int j = 0; j < 8; j++) {
            double a = MV_TENSOR(x)->data[r * 8 + j];
            double b = MV_TENSOR(y1)->data[r * 8 + j];
            nx += a * a; ny += b * b;
        }
        nx = sqrt(nx); ny = sqrt(ny);
        CHECK(fabs(nx - ny) < 1e-4 * nx, "H2: Norm-Erhalt pro Zeile");
    }

    /* H3: manuelle Inverse (1/sqrt(n)) * D * H * y == x via geteilte Kerne */
    {
        float d[8], tmp[8];
        moo_quant_vorzeichen(42u, 8, d);
        const float skal = 1.0f / sqrtf(8.0f);
        int rt_ok = 1;
        for (int r = 0; r < 2; r++) {
            for (int j = 0; j < 8; j++) tmp[j] = MV_TENSOR(y1)->data[r * 8 + j];
            moo_quant_wht_zeile(tmp, 8);
            for (int j = 0; j < 8; j++) {
                float rec = d[j] * tmp[j] * skal;
                float ref = MV_TENSOR(x)->data[r * 8 + j];
                if (fabsf(rec - ref) > 1e-4f * (fabsf(ref) + 1.0f)) rt_ok = 0;
            }
        }
        CHECK(rt_ok == 1, "H3: Inverse-Roundtrip rekonstruiert x");
    }

    /* H4: 6 ist keine Zweierpotenz */
    {
        int32_t s26[2] = { 2, 6 };
        MooValue xb = mk(2, s26);
        moo_error_flag = 0;
        MooValue e = moo_tensor_hadamard(xb, moo_number(1));
        CHECK(moo_error_flag == 1 && e.tag == MOO_NONE,
              "H4: Nicht-Zweierpotenz wirft Fehler");
        moo_release(xb);
    }

    /* H5: Seed muss ganzzahlig sein */
    moo_error_flag = 0;
    {
        MooValue e = moo_tensor_hadamard(x, moo_number(1.5));
        CHECK(moo_error_flag == 1 && e.tag == MOO_NONE,
              "H5: nicht-ganzzahliger Seed wirft Fehler");
    }
    moo_error_flag = 0;

    moo_release(y1); moo_release(y2); moo_release(y3);
    moo_release(x);

    /* ---------- QJL: Q1-Q4 ---------- */
    enum { D = 32, M = 128, R = 200 };
    enum { WORTE = (M + 15) / 16 };
    int32_t skv[2] = { 1, D };
    MooValue k = mk(2, skv);
    MooValue q = mk(2, skv);
    double wahr = 0.0;
    for (int i = 0; i < D; i++) {
        float kvv = sinf(0.7f * (float)i) + 0.2f;
        float qvv = kvv + 0.3f * cosf(1.3f * (float)i);   /* korreliert: <q,k> gross */
        MV_TENSOR(k)->data[i] = kvv;
        MV_TENSOR(q)->data[i] = qvv;
        wahr += (double)qvv * (double)kvv;
    }

    /* Q1 + Q4: Determinismus, Packung, Formen */
    moo_error_flag = 0;
    MooValue p1 = moo_quant_sign_jl(k, moo_number(M), moo_number(7));
    MooValue p2 = moo_quant_sign_jl(k, moo_number(M), moo_number(7));
    CHECK(moo_error_flag == 0 && p1.tag == MOO_DICT, "Q: sign_jl liefert Paket-Dict");
    {
        MooValue b1 = feld(p1, "bits");
        MooValue b2 = feld(p2, "bits");
        MooValue n1 = feld(p1, "normen");
        MooValue n2 = feld(p2, "normen");
        CHECK(b1.tag == MOO_TENSOR && MV_TENSOR(b1)->ndim == 2 &&
              MV_TENSOR(b1)->shape[0] == 1 && MV_TENSOR(b1)->shape[1] == WORTE,
              "Q4: bits-Form [zeilen, ceil(m/16)]");
        CHECK(n1.tag == MOO_TENSOR && MV_TENSOR(n1)->size == 1,
              "Q4: normen-Form [zeilen]");
        int det = 1, packung = 1;
        for (int j = 0; j < WORTE; j++) {
            float w = MV_TENSOR(b1)->data[j];
            if (w != MV_TENSOR(b2)->data[j]) det = 0;
            if (w < 0.0f || w > 65535.0f || floorf(w) != w) packung = 0;
        }
        if (MV_TENSOR(n1)->data[0] != MV_TENSOR(n2)->data[0]) det = 0;
        CHECK(det == 1, "Q1: Bits+Normen deterministisch bei gleichem Seed");
        CHECK(packung == 1, "Q1: Woerter ganzzahlig in [0, 65535] (16-Bit-Packung)");
        moo_release(b1); moo_release(b2); moo_release(n1); moo_release(n2);
    }
    moo_release(p2);

    /* Q2: Unbiasedness als z-TEST gegen das Eigenrauschen des Tests.
     * ----------------------------------------------------------------
     * BEWUSST KEINE feste Fehlerschwelle: Erwartungstreue ist eine Aussage
     * ueber den ERWARTUNGSWERT, eine Einzelziehung darf beliebig danebenliegen.
     * Eine Schwelle wie "rel < 0.08" misst faktisch die VARIANZ und haengt an
     * M, R und den Testdaten - sie kann rot werden, obwohl der Code korrekt ist
     * (gemessen: Standardfehler 0.055 bei Abweichungen von RMS 0.054), und
     * umgekehrt einen echten kleinen Bias durchwinken.
     * Stattdessen: Standardfehler aus den Stichproben schaetzen und pruefen, ob
     * |mittel - wahr| < 4*SE. Unter der Hypothese "kein Bias" ist ein Ausreisser
     * darueber ~0.006% wahrscheinlich. Das skaliert automatisch mit M und R. */
    {
        static double proben[R];
        double summe = 0.0;
        for (int s = 1; s <= R; s++) {
            MooValue paket = moo_quant_sign_jl(k, moo_number(M), moo_number(s));
            MooValue qp = moo_quant_jl_projektion(q, moo_number(M), moo_number(s));
            MooValue est = moo_quant_sign_jl_skalarprodukt(qp, paket);
            CHECK(est.tag == MOO_TENSOR && MV_TENSOR(est)->size == 1,
                  "Q4: Schaetzer-Form [1, 1]");
            proben[s - 1] = (double)MV_TENSOR(est)->data[0];
            summe += proben[s - 1];
            moo_release(est); moo_release(qp); moo_release(paket);
        }
        double mittel = summe / (double)R;
        double var = 0.0;
        for (int s = 0; s < R; s++) { double e = proben[s] - mittel; var += e * e; }
        var /= (double)(R - 1);
        double se = sqrt(var / (double)R);
        double z = fabs(mittel - wahr) / se;
        printf("QJL-Unbiasedness: wahr=%.4f mittel(%d Seeds)=%.4f SE=%.4f z=%.2f\n",
               wahr, R, mittel, se, z);
        CHECK(z < 4.0, "Q2: Schaetzer-Mittel trifft <q,k> (unbiased, z-Test)");

        /* NEGATIVKONTROLLE - sonst haette das Gate keine Zaehne und wuerde
         * jeden beliebigen Vorfaktor durchwinken: derselbe Test gegen einen
         * absichtlich falsch skalierten Schaetzer (Konstante 1.0 statt
         * sqrt(pi/2), also ~20.2% Verschiebung) MUSS anschlagen.
         * Nur fordern, wenn die Verschiebung ueberhaupt ueber dem Rauschen
         * liegt: bei <q,k> nahe 0 ist ein Skalenfehler prinzipiell nicht von
         * Null unterscheidbar - das waere keine Schwaeche des Schaetzers,
         * sondern eine Grenze der Messbarkeit. */
        double falsch = mittel / 1.2533141373155003;
        double verschiebung = fabs(wahr) * (1.0 - 1.0 / 1.2533141373155003);
        if (verschiebung > 4.0 * se) {
            double z_falsch = fabs(falsch - wahr) / se;
            printf("  Negativkontrolle: falscher Vorfaktor -> z=%.2f (muss > 4)\n",
                   z_falsch);
            CHECK(z_falsch > 4.0, "Q2: Gate erkennt einen falschen Vorfaktor");
        } else {
            printf("  Negativkontrolle: bei |wahr|=%.4f und SE=%.4f nicht "
                   "aufloesbar - uebersprungen (kein stiller Skip: siehe Zeile)\n",
                   fabs(wahr), se);
        }
    }

    /* Q3: autograd_an -> Fehler auf allen drei Funktionen */
    moo_ag_an();
    moo_error_flag = 0;
    {
        MooValue e = moo_quant_sign_jl(k, moo_number(M), moo_number(7));
        CHECK(moo_error_flag == 1 && e.tag == MOO_NONE,
              "Q3: sign_jl verweigert bei autograd_an");
    }
    moo_error_flag = 0;
    {
        MooValue e = moo_quant_jl_projektion(q, moo_number(M), moo_number(7));
        CHECK(moo_error_flag == 1 && e.tag == MOO_NONE,
              "Q3: jl_projektion verweigert bei autograd_an");
    }
    moo_error_flag = 0;
    {
        int32_t sq[2] = { 1, M };
        MooValue qp = mk(2, sq);
        MooValue e = moo_quant_sign_jl_skalarprodukt(qp, p1);
        CHECK(moo_error_flag == 1 && e.tag == MOO_NONE,
              "Q3: sign_jl_skalarprodukt verweigert bei autograd_an");
        moo_release(qp);
    }
    moo_ag_aus();

    /* Q4: m=0 -> Fehler */
    moo_error_flag = 0;
    {
        MooValue e = moo_quant_sign_jl(k, moo_number(0), moo_number(7));
        CHECK(moo_error_flag == 1 && e.tag == MOO_NONE, "Q4: m=0 wirft Fehler");
    }
    moo_error_flag = 0;

    moo_release(p1);
    moo_release(k); moo_release(q);
    moo_ag_an();

    if (fails) {
        fprintf(stderr, "test_quant_asan: %d FEHLER (%d ok)\n", fails, checks);
        return 1;
    }
    printf("test_quant_asan: PASS (%d Checks: Hadamard H1-H5 + QJL Q1-Q4)\n", checks);
    return 0;
}

/* P2-T26: Differenztest und Benchmark natives Moo (mat4_nativ.moos) gegen
 * die Produktions-C-Funktionen aus compiler/runtime/moo_3d_math.c.
 * Beide Seiten: f32, Spaltenlayout, strikte IEEE-Arithmetik (C mit
 * -ffp-contract=off, Moo-IR ohne Fast-Math-Flags, vom Skript geprueft).
 * Vergleich bitweise; einzige dokumentierte Ausnahme: NaN gegen NaN mit
 * anderer Nutzlast gilt als gleich und wird getrennt gezaehlt. */
#define _POSIX_C_SOURCE 199309L /* clock_gettime unter -std=c11 */
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "moo_3d_math.h" /* MooMatrixStack + Produktions-Prototypen */

_Static_assert(sizeof(MooMatrixStack) == 32 * 16 * 4 + 4 + 16 * 4, "MooMatrixStack-Layout");

/* Produktions-C */
void mat4_identity(float *m);
void mat4_multiply(float *result, const float *a, const float *b);
void mat4_translate(float *m, float x, float y, float z);
void mat4_scale(float *m, float sx, float sy, float sz);
void mat4_ortho(float *m, float l, float r, float b, float t, float n, float f);
void mat4_perspective(float *m, float fov_deg, float aspect, float near_val, float far_val);
void mat4_lookat(float *m, float ex, float ey, float ez, float lx, float ly, float lz,
                 float upx, float upy, float upz);
void mat4_rotate(float *m, float angle_deg, float ax, float ay, float az);

/* Natives Moo, exportiert per @einstieg */
void moo_n_mat4_identity(float *m);
void moo_n_mat4_multiply(float *result, const float *a, const float *b);
void moo_n_mat4_multiply_skalar(float *result, const float *a, const float *b);
void moo_n_mat4_translate_skalar(float *m, float x, float y, float z);
void moo_n_mat4_scale_skalar(float *m, float sx, float sy, float sz);
void moo_n_mat4_rotate_skalar(float *m, float angle_deg, float ax, float ay, float az);
void moo_n_mat4_translate(float *m, float x, float y, float z);
void moo_n_mat4_scale(float *m, float sx, float sy, float sz);
void moo_n_mat4_ortho(float *m, float l, float r, float b, float t, float n, float f);
void moo_n_mat4_perspective(float *m, float fov_deg, float aspect, float near_val, float far_val);
void moo_n_mat4_lookat(float *m, float ex, float ey, float ez, float lx, float ly, float lz,
                       float upx, float upy, float upz);
void moo_n_mat4_rotate(float *m, float angle_deg, float ax, float ay, float az);
void moo_n_matrix_stack_init(MooMatrixStack *s);
void moo_n_matrix_stack_push(MooMatrixStack *s);
void moo_n_matrix_stack_pop(MooMatrixStack *s);

static uint32_t rng_state = 0x2026u;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static float zufall(int mit_sonderwerten) {
    static const float sonder[] = {
        0.0f, -0.0f, 1.0f, -1.0f, INFINITY, -INFINITY, NAN,
        FLT_MIN, 1e-40f, -1e-40f, FLT_MAX, -FLT_MAX, 1e-7f, 3.0e38f,
    };
    uint32_t r = rng();
    if (mit_sonderwerten && (r & 7u) == 0u)
        return sonder[(r >> 3) % (sizeof sonder / sizeof sonder[0])];
    return ((float)(r >> 8) / (float)(1u << 24)) * 200.0f - 100.0f;
}

static void fuelle(float *m, int mit_sonderwerten) {
    for (int i = 0; i < 16; i++) m[i] = zufall(mit_sonderwerten);
}

static long gepr = 0, fehler = 0, nan_nutzlast = 0;

static void vergleiche(const char *fall, const float *c, const float *moo) {
    for (int i = 0; i < 16; i++) {
        uint32_t bc, bm;
        memcpy(&bc, &c[i], 4);
        memcpy(&bm, &moo[i], 4);
        gepr++;
        if (bc == bm) continue;
        if (isnan(c[i]) && isnan(moo[i])) {
            if (nan_nutzlast < 6)
                printf("NAN-PAAR %s [%d]: C=%08x Moo=%08x\n", fall, i, bc, bm);
            nan_nutzlast++;
            continue;
        }
        if (fehler < 10)
            fprintf(stderr, "ABWEICHUNG %s [%d]: C=%08x Moo=%08x\n", fall, i, bc, bm);
        fehler++;
    }
}

static void differenztest(int runden) {
    float a[16], b[16], c[16], m[16], ca[16], ma[16];
    for (int k = 0; k < runden; k++) {
        int sw = (k & 1);
        fuelle(a, sw); fuelle(b, sw);

        fuelle(c, 1); memcpy(m, c, sizeof m);
        mat4_identity(c); moo_n_mat4_identity(m);
        vergleiche("identity", c, m);

        mat4_multiply(c, a, b); moo_n_mat4_multiply(m, a, b);
        vergleiche("multiply", c, m);
        memcpy(ca, a, sizeof ca); memcpy(ma, a, sizeof ma);
        mat4_multiply(ca, ca, b); moo_n_mat4_multiply(ma, ma, b);
        vergleiche("multiply r==a", ca, ma);
        memcpy(ca, b, sizeof ca); memcpy(ma, b, sizeof ma);
        mat4_multiply(ca, a, ca); moo_n_mat4_multiply(ma, a, ma);
        vergleiche("multiply r==b", ca, ma);
        memcpy(ca, a, sizeof ca); memcpy(ma, a, sizeof ma);
        mat4_multiply(ca, ca, ca); moo_n_mat4_multiply(ma, ma, ma);
        vergleiche("multiply r==a==b", ca, ma);

        mat4_multiply(c, a, b); moo_n_mat4_multiply_skalar(m, a, b);
        vergleiche("skalar", c, m);
        memcpy(ca, a, sizeof ca); memcpy(ma, a, sizeof ma);
        mat4_multiply(ca, ca, b); moo_n_mat4_multiply_skalar(ma, ma, b);
        vergleiche("skalar r==a", ca, ma);
        memcpy(ca, b, sizeof ca); memcpy(ma, b, sizeof ma);
        mat4_multiply(ca, a, ca); moo_n_mat4_multiply_skalar(ma, a, ma);
        vergleiche("skalar r==b", ca, ma);
        memcpy(ca, a, sizeof ca); memcpy(ma, a, sizeof ma);
        mat4_multiply(ca, ca, ca); moo_n_mat4_multiply_skalar(ma, ma, ma);
        vergleiche("skalar r==a==b", ca, ma);

        float x = zufall(sw), y = zufall(sw), z = zufall(sw);
        memcpy(c, a, sizeof c); memcpy(m, a, sizeof m);
        mat4_translate(c, x, y, z); moo_n_mat4_translate(m, x, y, z);
        vergleiche("translate", c, m);
        memcpy(c, a, sizeof c); memcpy(m, a, sizeof m);
        mat4_scale(c, x, y, z); moo_n_mat4_scale(m, x, y, z);
        vergleiche("scale", c, m);
        memcpy(c, a, sizeof c); memcpy(m, a, sizeof m);
        mat4_translate(c, x, y, z); moo_n_mat4_translate_skalar(m, x, y, z);
        vergleiche("translate_skalar", c, m);
        memcpy(c, a, sizeof c); memcpy(m, a, sizeof m);
        mat4_scale(c, x, y, z); moo_n_mat4_scale_skalar(m, x, y, z);
        vergleiche("scale_skalar", c, m);

        float l = zufall(sw), r = zufall(sw), bo = zufall(sw), t = zufall(sw);
        float n = zufall(sw), f = zufall(sw);
        if ((k & 15) == 3) r = l; /* Division durch 0 bewusst mitpruefen */
        fuelle(c, 1); memcpy(m, c, sizeof m);
        mat4_ortho(c, l, r, bo, t, n, f); moo_n_mat4_ortho(m, l, r, bo, t, n, f);
        vergleiche("ortho", c, m);

        /* Trig-Pfade: dieselbe libm (sinf/cosf/tanf/sqrtf) auf beiden Seiten. */
        float fov = zufall(sw) * 1.7f, asp = zufall(sw) * 0.05f;
        fuelle(c, 1); memcpy(m, c, sizeof m);
        mat4_perspective(c, fov, asp, n, f); moo_n_mat4_perspective(m, fov, asp, n, f);
        vergleiche("perspective", c, m);

        float ux = zufall(sw), uy = zufall(sw), uz = zufall(sw);
        if ((k & 15) == 5) { l = x; bo = y; t = z; }                /* Auge == Ziel: len == 0 */
        if ((k & 15) == 7) { ux = l - x; uy = bo - y; uz = t - z; } /* up parallel: Kreuz 0 */
        fuelle(c, 1); memcpy(m, c, sizeof m);
        mat4_lookat(c, x, y, z, l, bo, t, ux, uy, uz);
        moo_n_mat4_lookat(m, x, y, z, l, bo, t, ux, uy, uz);
        vergleiche("lookat", c, m);

        float wink = zufall(sw) * 7.0f;
        if ((k & 15) == 9) { ux = 0.0f; uy = 0.0f; uz = 0.0f; }     /* Nullachse */
        memcpy(c, a, sizeof c); memcpy(m, a, sizeof m);
        mat4_rotate(c, wink, ux, uy, uz); moo_n_mat4_rotate(m, wink, ux, uy, uz);
        vergleiche("rotate", c, m);
        memcpy(c, a, sizeof c); memcpy(m, a, sizeof m);
        mat4_rotate(c, wink, ux, uy, uz); moo_n_mat4_rotate_skalar(m, wink, ux, uy, uz);
        vergleiche("rotate_skalar", c, m);
    }
}

/* Matrix-Stack: zufaellige Op-Folgen auf zwei Instanzen, nach JEDER Op wird die
 * ganze Struktur verglichen (belegt auch Layout/Offsets der Moo-Struktur).
 * Push-lastig, damit die 32er-Grenze oft ueberschritten wird; Pop-Serien fuer
 * den Unterlauf. current wird mit derselben C-Funktion veraendert. */
static long stapel_ops = 0, stapel_fehler = 0, stapel_ueberlauf = 0, stapel_unterlauf = 0;
static void stapeltest(int runden) {
    static MooMatrixStack c, m;
    memset(&c, 0, sizeof c); memset(&m, 0, sizeof m);
    for (int k = 0; k < runden; k++) {
        uint32_t op = rng() % 16u;
        if (op == 0u) { mat4_identity(c.current); memset(&c, 0x5a, sizeof c); memcpy(&m, &c, sizeof m);
                        moo_matrix_stack_init(&c); moo_n_matrix_stack_init(&m); }
        else if (op < 8u) { if (c.depth >= MAX_MATRIX_DEPTH) stapel_ueberlauf++;
                            moo_matrix_stack_push(&c); moo_n_matrix_stack_push(&m); }
        else if (op < 13u) { if (c.depth <= 0) stapel_unterlauf++;
                             moo_matrix_stack_pop(&c); moo_n_matrix_stack_pop(&m); }
        else { float x = zufall(1), y = zufall(1), z = zufall(1);
               mat4_translate(c.current, x, y, z); mat4_translate(m.current, x, y, z); }
        stapel_ops++;
        if (memcmp(&c, &m, sizeof c) != 0) {
            if (stapel_fehler < 5) fprintf(stderr, "STAPEL-ABWEICHUNG op=%u schritt=%d tiefe C=%d Moo=%d\n", op, k, c.depth, m.depth);
            stapel_fehler++;
            memcpy(&m, &c, sizeof m);
        }
    }
}

/* ---------------- Benchmark ---------------- */
typedef void (*mul_fn)(float *, const float *, const float *);
typedef void (*ts_fn)(float *, float, float, float);

static double jetzt_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int cmp_double(const void *x, const void *y) {
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}

#define POOL 64
static float pool[POOL][16];

/* Leerlauf-Grundlinie: gleiche Schleife, gleicher indirekter Aufruf, keine Arbeit. */
__attribute__((noinline)) static void leer_mul(float *r, const float *a, const float *b) {
    (void)r; (void)a; (void)b;
}
__attribute__((noinline)) static void leer_ts(float *m, float x, float y, float z) {
    (void)m; (void)x; (void)y; (void)z;
}

/* Kette acc = acc * pool[i]; alle 32 Schritte Reset, damit keine
 * Denormalen/Inf das Timing verfaelschen. Pruefsumme haelt das Ergebnis
 * beobachtbar und muss fuer C und Moo bitgleich sein. */
static uint32_t lauf_mul(mul_fn mul, long n) {
    float acc[16];
    uint32_t sum = 0;
    mat4_identity(acc);
    for (long i = 0; i < n; i++) {
        mul(acc, acc, pool[i & (POOL - 1)]);
        if ((i & 31) == 31) {
            uint32_t w; memcpy(&w, &acc[(i >> 5) & 15], 4); sum = sum * 31u + w;
            mat4_identity(acc);
        }
    }
    return sum;
}

static uint32_t lauf_ts(ts_fn tr, ts_fn sc, long n) {
    float acc[16];
    uint32_t sum = 0;
    mat4_identity(acc);
    for (long i = 0; i < n; i++) {
        const float *p = pool[i & (POOL - 1)];
        tr(acc, p[0] * 0.01f, p[1] * 0.01f, p[2] * 0.01f);
        sc(acc, 1.0f + p[3] * 0.001f, 1.0f, 1.0f - p[4] * 0.001f);
        if ((i & 31) == 31) {
            uint32_t w; memcpy(&w, &acc[12 + ((i >> 5) & 3)], 4); sum = sum * 31u + w;
            mat4_identity(acc);
        }
    }
    return sum;
}

static void bench(long n, int laeufe) {
    for (int i = 0; i < POOL; i++)
        for (int j = 0; j < 16; j++) pool[i][j] = zufall(0) * 0.01f;
    double tc[16], tm[16];
    uint32_t sc = 0, sm = 0;
    if (laeufe > 16) laeufe = 16;
    /* volatile: der C-Compiler darf lauf_* nicht auf ein bekanntes Ziel spezialisieren. */
    mul_fn volatile f_mul_c = mat4_multiply, f_mul_m = moo_n_mat4_multiply, f_mul_l = leer_mul;
    ts_fn volatile f_tr_c = mat4_translate, f_tr_m = moo_n_mat4_translate, f_ts_l = leer_ts;
    ts_fn volatile f_sc_c = mat4_scale, f_sc_m = moo_n_mat4_scale;
    double tl[16];

    lauf_mul(f_mul_c, n / 4); lauf_mul(f_mul_m, n / 4); /* Aufwaermen */
    for (int r = 0; r < laeufe; r++) {
        double t0 = jetzt_ns(); sc = lauf_mul(f_mul_c, n); tc[r] = (jetzt_ns() - t0) / (double)n;
        t0 = jetzt_ns(); sm = lauf_mul(f_mul_m, n); tm[r] = (jetzt_ns() - t0) / (double)n;
        t0 = jetzt_ns(); (void)lauf_mul(f_mul_l, n); tl[r] = (jetzt_ns() - t0) / (double)n;
    }
    qsort(tl, (size_t)laeufe, sizeof tl[0], cmp_double);
    printf("BENCH leerlauf mul  median %.2f ns/Schleife (Overhead Schleife+indirekter Aufruf+Reset)\n", tl[laeufe / 2]);
    if (sc != sm) { fprintf(stderr, "BENCH multiply: Pruefsumme C=%08x Moo=%08x\n", sc, sm); fehler++; }
    qsort(tc, (size_t)laeufe, sizeof tc[0], cmp_double);
    qsort(tm, (size_t)laeufe, sizeof tm[0], cmp_double);
    printf("BENCH multiply      n=%ld laeufe=%d median C=%.2f ns Moo=%.2f ns Faktor=%.3f (min C=%.2f Moo=%.2f) summe=%08x\n",
           n, laeufe, tc[laeufe / 2], tm[laeufe / 2], tm[laeufe / 2] / tc[laeufe / 2], tc[0], tm[0], sc);

    mul_fn volatile f_mul_s = moo_n_mat4_multiply_skalar;
    lauf_mul(f_mul_s, n / 4);
    for (int r = 0; r < laeufe; r++) {
        double t0 = jetzt_ns(); sc = lauf_mul(f_mul_c, n); tc[r] = (jetzt_ns() - t0) / (double)n;
        t0 = jetzt_ns(); sm = lauf_mul(f_mul_s, n); tm[r] = (jetzt_ns() - t0) / (double)n;
    }
    if (sc != sm) { fprintf(stderr, "BENCH multiply_skalar: Pruefsumme C=%08x Moo=%08x\n", sc, sm); fehler++; }
    qsort(tc, (size_t)laeufe, sizeof tc[0], cmp_double);
    qsort(tm, (size_t)laeufe, sizeof tm[0], cmp_double);
    printf("BENCH multiply_skalar n=%ld laeufe=%d median C=%.2f ns Moo=%.2f ns Faktor=%.3f (min C=%.2f Moo=%.2f) summe=%08x\n",
           n, laeufe, tc[laeufe / 2], tm[laeufe / 2], tm[laeufe / 2] / tc[laeufe / 2], tc[0], tm[0], sc);

    lauf_ts(f_tr_c, f_sc_c, n / 4); lauf_ts(f_tr_m, f_sc_m, n / 4);
    for (int r = 0; r < laeufe; r++) {
        double t0 = jetzt_ns(); sc = lauf_ts(f_tr_c, f_sc_c, n); tc[r] = (jetzt_ns() - t0) / (double)n;
        t0 = jetzt_ns(); sm = lauf_ts(f_tr_m, f_sc_m, n); tm[r] = (jetzt_ns() - t0) / (double)n;
        t0 = jetzt_ns(); (void)lauf_ts(f_ts_l, f_ts_l, n); tl[r] = (jetzt_ns() - t0) / (double)n;
    }
    qsort(tl, (size_t)laeufe, sizeof tl[0], cmp_double);
    printf("BENCH leerlauf t+s  median %.2f ns/Schleife\n", tl[laeufe / 2]);
    {
        ts_fn volatile f_tr_s = moo_n_mat4_translate_skalar, f_sc_s = moo_n_mat4_scale_skalar;
        double ts_c[16], ts_m[16];
        uint32_t s_c = 0, s_m = 0;
        lauf_ts(f_tr_s, f_sc_s, n / 4);
        for (int r = 0; r < laeufe; r++) {
            double t0 = jetzt_ns(); s_c = lauf_ts(f_tr_c, f_sc_c, n); ts_c[r] = (jetzt_ns() - t0) / (double)n;
            t0 = jetzt_ns(); s_m = lauf_ts(f_tr_s, f_sc_s, n); ts_m[r] = (jetzt_ns() - t0) / (double)n;
        }
        if (s_c != s_m) { fprintf(stderr, "BENCH t+s_skalar: Pruefsumme C=%08x Moo=%08x\n", s_c, s_m); fehler++; }
        qsort(ts_c, (size_t)laeufe, sizeof ts_c[0], cmp_double);
        qsort(ts_m, (size_t)laeufe, sizeof ts_m[0], cmp_double);
        printf("BENCH t+s_skalar    n=%ld laeufe=%d median C=%.2f ns Moo=%.2f ns Faktor=%.3f (min C=%.2f Moo=%.2f) summe=%08x\n",
               n, laeufe, ts_c[laeufe / 2], ts_m[laeufe / 2], ts_m[laeufe / 2] / ts_c[laeufe / 2], ts_c[0], ts_m[0], s_c);
    }
    if (sc != sm) { fprintf(stderr, "BENCH translate+scale: Pruefsumme C=%08x Moo=%08x\n", sc, sm); fehler++; }
    qsort(tc, (size_t)laeufe, sizeof tc[0], cmp_double);
    qsort(tm, (size_t)laeufe, sizeof tm[0], cmp_double);
    printf("BENCH translate+scale n=%ld laeufe=%d median C=%.2f ns Moo=%.2f ns Faktor=%.3f (min C=%.2f Moo=%.2f) summe=%08x\n",
           n, laeufe, tc[laeufe / 2], tm[laeufe / 2], tm[laeufe / 2] / tc[laeufe / 2], tc[0], tm[0], sc);
}

int main(int argc, char **argv) {
    int runden = argc > 1 ? atoi(argv[1]) : 200000;
    long n = argc > 2 ? atol(argv[2]) : 2000000;
    int laeufe = argc > 3 ? atoi(argv[3]) : 7;
    differenztest(runden);
    printf("DIFF runden=%d werte=%ld abweichungen=%ld nan_nutzlast=%ld\n", runden, gepr, fehler, nan_nutzlast);
    stapeltest(runden);
    printf("STAPEL ops=%ld abweichungen=%ld ueberlauf=%ld unterlauf=%ld\n", stapel_ops, stapel_fehler, stapel_ueberlauf, stapel_unterlauf);
    if (stapel_ueberlauf == 0 || stapel_unterlauf == 0) { puts("STAPEL: Grenzfaelle nicht erreicht"); fehler++; }
    fehler += stapel_fehler;
    if (n > 0 && laeufe > 0) bench(n, laeufe);
    if (fehler) { puts("ERGEBNIS: ROT"); return 1; }
    puts("ERGEBNIS: GRUEN (natives Moo bitgleich zu moo_3d_math.c)");
    return 0;
}

/* P2-T16 (Plan 3f03704a D2/D4): eingebaute vec2/vec3/vec4/mat4 (Moo-Objekt
 * aus vecmat_nativ.moos) gegen compiler/runtime/moo_3d_math.c und mat_ref.c.
 * Erwartung x86_64: bitgleich; einzige Ausnahme NaN gegen NaN mit anderer
 * Nutzlast. Alias (erg == Eingabe) und Sonderwerte (+-0, +-Inf, NaN,
 * Denormale, FLT_MAX). Benchmark: 10^6 mat4*vec4 und mat4*mat4, Median aus
 * 7 Laeufen, Pruefsummen muessen gleich sein.
 * P2-T89: rotate/perspective gegen dieselbe Formel mit moo_native_sinf/cosf/
 * tanf (stdlib/vektor nutzt die eingebauten sin/cos/tan, nicht libm);
 * dmat4/dvec4 gegen p2_dmat4_* und dref_*; Spaltenindex m[i], m[j][k].
 * Argumente: [runden] [bench_n] [bench_laeufe] */
#define _POSIX_C_SOURCE 200809L /* clock_gettime unter -std=c11 */
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mat_ref.h"
#include "moo_native_math.h"

void mat4_identity(float *m);
void mat4_multiply(float *result, const float *a, const float *b);
void mat4_perspective(float *m, float fov_deg, float aspect, float near_val, float far_val);
void mat4_lookat(float *m, float ex, float ey, float ez, float lx, float ly, float lz,
                 float upx, float upy, float upz);
void mat4_translate(float *m, float x, float y, float z);
void mat4_rotate(float *m, float angle_deg, float ax, float ay, float az);
void mat4_scale(float *m, float sx, float sy, float sz);
void mat4_ortho(float *m, float l, float r, float b, float t, float n, float f);

void moo_n_vm_mat4_vec4(float *erg, const float *m, const float *v);
void moo_n_vm_mat4_mul(float *erg, const float *a, const float *b);
void moo_n_vm_mat4_vec4_viele(float *erg, const float *m, const float *v, uint64_t n);
void moo_n_vm_identity(float *m);
void moo_n_vm_translate(float *m, float x, float y, float z);
void moo_n_vm_scale(float *m, float x, float y, float z);
void moo_n_vm_rotate(float *m, float grad, float x, float y, float z);
void moo_n_vm_perspective(float *m, float fov, float seiten, float nah, float fern);
void moo_n_vm_lookat(float *m, float ex, float ey, float ez, float lx, float ly, float lz,
                     float ux, float uy, float uz);
void moo_n_vm_ortho(float *m, float l, float r, float b, float t, float n, float f);
void moo_n_vm_ops(float *erg, float *neg, const float *a, const float *b, float s);
float moo_n_vm_punkt(const float *a, const float *b);
void moo_n_vm_kreuz(float *erg, const float *a, const float *b);
float moo_n_vm_betrag(const float *a);
void moo_n_vm_normalisiere(float *erg, const float *a);
/* Moo bool = i1 ohne zeroext: nur Bit 0 auswerten. */
uint8_t moo_n_vm_gleich(const float *a, const float *b);
void moo_n_vm_dmat4_dvec4(double *erg, const double *m, const double *v);
void moo_n_vm_dmat4_mul(double *erg, const double *a, const double *b);
void moo_n_vm_drotate(double *m, double grad, double x, double y, double z);
void moo_n_vm_dperspective(double *m, double fov, double seiten, double nah, double fern);
void moo_n_vm_spalte(float *erg, float *m, uint64_t i, uint64_t j, uint64_t k, float x);
double moo_n_vm_dspalte(const double *m, uint64_t i, uint64_t j);

static uint64_t zustand = 0x9E3779B97F4A7C15ull;
static uint64_t zufall(void) {
    zustand ^= zustand << 13; zustand ^= zustand >> 7; zustand ^= zustand << 17;
    return zustand;
}

static const float sonder[] = {
    0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 3.0f, -2.5f, 1e-45f, -1e-40f, FLT_MAX, -FLT_MAX,
    INFINITY, -INFINITY, NAN, 1e20f, 1e-20f,
};

/* Haelfte gewoehnliche Werte [-8, 8), Rest Sonderwerte bzw. beliebige Bits. */
static float wert(int modus) {
    uint64_t r = zufall();
    if (modus == 0 || (r & 7) < 5) return ((float)(r >> 40) / (float)(1u << 24)) * 16.0f - 8.0f;
    if ((r & 7) == 5) return sonder[(r >> 8) % (sizeof sonder / sizeof *sonder)];
    uint32_t bits = (uint32_t)(r >> 16);
    float f; memcpy(&f, &bits, sizeof f); return f;
}

static void fuellen(float *p, int n, int modus) { for (int i = 0; i < n; ++i) p[i] = wert(modus); }

static long fehler = 0;
static int gleich_bits(float a, float b) {
    if (isnan(a) && isnan(b)) return 1;
    return memcmp(&a, &b, sizeof a) == 0;
}
static void vergleiche(const char *was, const float *moo, const float *ref, int n) {
    for (int i = 0; i < n; ++i) {
        if (!gleich_bits(moo[i], ref[i])) {
            if (fehler < 10)
                fprintf(stderr, "ABWEICHUNG %s[%d]: moo=%a ref=%a\n", was, i, moo[i], ref[i]);
            ++fehler;
            return;
        }
    }
}

static void ref_ops(float *erg, float *neg, const float *a, const float *b, float s) {
    for (int i = 0; i < 4; ++i) { erg[i] = ((a[i] + b[i]) * s - a[i] / s) * b[i]; neg[i] = -a[i]; }
}
static float ref_punkt(const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3]; }
static void ref_kreuz(float *e, const float *a, const float *b) {
    float t[3] = { a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0] };
    memcpy(e, t, sizeof t);
}
static float ref_betrag(const float *a) { return sqrtf(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]); }
static void ref_normalisiere(float *e, const float *a) {
    float x = a[0], y = a[1], z = a[2], len = sqrtf(x*x + y*y + z*z);
    if (len > 0) { x /= len; y /= len; z /= len; }
    e[0] = x; e[1] = y; e[2] = z;
}

/* moo_3d_math.c mat4_rotate/mat4_perspective, Trigonometrie ueber moo_native_*. */
static void ref_rotate(float *m, float angle_deg, float ax, float ay, float az) {
    float rad = angle_deg * (float)3.14159265358979323846 / 180.0f;
    float c = moo_native_cosf(rad), s = moo_native_sinf(rad);
    float len = sqrtf(ax*ax + ay*ay + az*az);
    if (len > 0) { ax /= len; ay /= len; az /= len; }
    float r[16];
    mat4_identity(r);
    r[0]  = c + ax*ax*(1-c);
    r[1]  = ay*ax*(1-c) + az*s;
    r[2]  = az*ax*(1-c) - ay*s;
    r[4]  = ax*ay*(1-c) - az*s;
    r[5]  = c + ay*ay*(1-c);
    r[6]  = az*ay*(1-c) + ax*s;
    r[8]  = ax*az*(1-c) + ay*s;
    r[9]  = ay*az*(1-c) - ax*s;
    r[10] = c + az*az*(1-c);
    float tmp[16];
    mat4_multiply(tmp, m, r);
    memcpy(m, tmp, 16 * sizeof(float));
}
static void ref_perspective(float *m, float fov_deg, float aspect, float near_val, float far_val) {
    memset(m, 0, 16 * sizeof(float));
    float rad = fov_deg * (float)3.14159265358979323846 / 180.0f;
    float tanHalf = moo_native_tanf(rad / 2.0f);
    m[0]  = 1.0f / (aspect * tanHalf);
    m[5]  = 1.0f / tanHalf;
    m[10] = -(far_val + near_val) / (far_val - near_val);
    m[11] = -1.0f;
    m[14] = -(2.0f * far_val * near_val) / (far_val - near_val);
}

/* f64: Werte, Bitvergleich, Referenzen (gleiche Operationsfolge wie vektor.moos). */
static double dwert(int modus) {
    uint64_t r = zufall();
    if (modus == 0 || (r & 7) < 5) return ((double)(r >> 11) / 9007199254740992.0) * 16.0 - 8.0;
    if ((r & 7) == 5) return (double)sonder[(r >> 8) % (sizeof sonder / sizeof *sonder)];
    uint64_t bits = zufall();
    double d; memcpy(&d, &bits, sizeof d); return d;
}
static void dfuellen(double *p, int n, int modus) { for (int i = 0; i < n; ++i) p[i] = dwert(modus); }
static int dgleich_bits(double a, double b) {
    if (isnan(a) && isnan(b)) return 1;
    return memcmp(&a, &b, sizeof a) == 0;
}
static void dvergleiche(const char *was, const double *moo, const double *ref, int n) {
    for (int i = 0; i < n; ++i) {
        if (!dgleich_bits(moo[i], ref[i])) {
            if (fehler < 10)
                fprintf(stderr, "ABWEICHUNG %s[%d]: moo=%a ref=%a\n", was, i, moo[i], ref[i]);
            ++fehler;
            return;
        }
    }
}
static void dref_rotate(double *m, double grad, double ax, double ay, double az) {
    double rad = grad * 3.14159265358979323846 / 180.0;
    double c = moo_native_cos(rad), s = moo_native_sin(rad);
    double len = sqrt(ax*ax + ay*ay + az*az);
    if (len > 0) { ax /= len; ay /= len; az /= len; }
    double k = 1.0 - c, r[16];
    memset(r, 0, sizeof r);
    r[15] = 1.0;
    r[0]  = c + ax*ax*k;
    r[1]  = ay*ax*k + az*s;
    r[2]  = az*ax*k - ay*s;
    r[4]  = ax*ay*k - az*s;
    r[5]  = c + ay*ay*k;
    r[6]  = az*ay*k + ax*s;
    r[8]  = ax*az*k + ay*s;
    r[9]  = ay*az*k - ax*s;
    r[10] = c + az*az*k;
    p2_dmat4_mul(m, m, r);
}
static void dref_perspective(double *m, double fov, double aspect, double nah, double fern) {
    memset(m, 0, 16 * sizeof(double));
    double rad = fov * 3.14159265358979323846 / 180.0;
    double t = moo_native_tan(rad / 2.0);
    double tiefe = fern - nah;
    m[0]  = 1.0 / (aspect * t);
    m[5]  = 1.0 / t;
    m[10] = (-(fern + nah)) / tiefe;
    m[11] = -1.0;
    m[14] = (-(2.0 * fern * nah)) / tiefe;
}

static double jetzt(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}
static int kleiner(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y);
}
static double median(double *z, int n) { qsort(z, (size_t)n, sizeof *z, kleiner); return z[n / 2]; }

/* C-Basis fuer den Benchmark: dieselbe Formel inline (-O2, -ffp-contract=off). */
static void c_viele(float *erg, const float *m, const float *v, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i)
        for (int row = 0; row < 4; ++row)
            erg[i*4 + row] = m[row]*v[i*4] + m[4+row]*v[i*4+1] + m[8+row]*v[i*4+2] + m[12+row]*v[i*4+3];
}

int main(int argc, char **argv) {
    long runden = argc > 1 ? atol(argv[1]) : 20000;
    uint64_t bench_n = argc > 2 ? (uint64_t)atoll(argv[2]) : 1000000;
    int laeufe = argc > 3 ? atoi(argv[3]) : 7;
    if (laeufe < 1 || laeufe > 64) laeufe = 7;

    for (long k = 0; k < runden; ++k) {
        int modus = k % 4 == 3;
        float m[16], b[16], v[4], w[4], e1[16], e2[16], n1[4], n2[4];
        fuellen(m, 16, modus); fuellen(b, 16, modus); fuellen(v, 4, modus); fuellen(w, 4, modus);
        float s = wert(modus);

        moo_n_vm_mat4_vec4(e1, m, v); p2_mat4_vec4(e2, m, v); vergleiche("mat4*vec4", e1, e2, 4);
        memcpy(e1, v, sizeof v); memcpy(e2, v, sizeof v);
        moo_n_vm_mat4_vec4(e1, m, e1); p2_mat4_vec4(e2, m, e2); vergleiche("mat4*vec4 alias", e1, e2, 4);
        moo_n_vm_mat4_mul(e1, m, b); mat4_multiply(e2, m, b); vergleiche("mat4*mat4", e1, e2, 16);
        memcpy(e1, m, sizeof m); memcpy(e2, m, sizeof m);
        moo_n_vm_mat4_mul(e1, e1, e1); mat4_multiply(e2, e2, e2); vergleiche("mat4*mat4 alias", e1, e2, 16);

        moo_n_vm_identity(e1); mat4_identity(e2); vergleiche("identity", e1, e2, 16);
        memcpy(e1, m, sizeof m); memcpy(e2, m, sizeof m);
        moo_n_vm_translate(e1, v[0], v[1], v[2]); mat4_translate(e2, v[0], v[1], v[2]); vergleiche("translate", e1, e2, 16);
        moo_n_vm_scale(e1, w[0], w[1], w[2]); mat4_scale(e2, w[0], w[1], w[2]); vergleiche("scale", e1, e2, 16);
        float grad = modus ? s : s * 45.0f;
        moo_n_vm_rotate(e1, grad, w[0], w[1], w[3]); ref_rotate(e2, grad, w[0], w[1], w[3]); vergleiche("rotate", e1, e2, 16);
        moo_n_vm_perspective(e1, v[0] * 20.0f, v[1], v[2], v[3]); ref_perspective(e2, v[0] * 20.0f, v[1], v[2], v[3]);
        vergleiche("perspective", e1, e2, 16);
        moo_n_vm_lookat(e1, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
        mat4_lookat(e2, m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]); vergleiche("lookat", e1, e2, 16);
        moo_n_vm_ortho(e1, b[0], b[1], b[2], b[3], b[4], b[5]); mat4_ortho(e2, b[0], b[1], b[2], b[3], b[4], b[5]);
        vergleiche("ortho", e1, e2, 16);

        double dm[16], db[16], dv[4], d1[16], d2[16];
        dfuellen(dm, 16, modus); dfuellen(db, 16, modus); dfuellen(dv, 4, modus);
        double ds = dwert(modus);
        moo_n_vm_dmat4_dvec4(d1, dm, dv); p2_dmat4_vec4(d2, dm, dv); dvergleiche("dmat4*dvec4", d1, d2, 4);
        moo_n_vm_dmat4_mul(d1, dm, db); p2_dmat4_mul(d2, dm, db); dvergleiche("dmat4*dmat4", d1, d2, 16);
        memcpy(d1, dm, sizeof dm); memcpy(d2, dm, sizeof dm);
        moo_n_vm_dmat4_mul(d1, d1, d1); p2_dmat4_mul(d2, d2, d2); dvergleiche("dmat4*dmat4 alias", d1, d2, 16);
        memcpy(d1, dm, sizeof dm); memcpy(d2, dm, sizeof dm);
        double dgrad = modus ? ds : ds * 45.0;
        moo_n_vm_drotate(d1, dgrad, db[0], db[1], db[3]); dref_rotate(d2, dgrad, db[0], db[1], db[3]);
        dvergleiche("drotate", d1, d2, 16);
        moo_n_vm_dperspective(d1, dv[0] * 20.0, dv[1], dv[2], dv[3]); dref_perspective(d2, dv[0] * 20.0, dv[1], dv[2], dv[3]);
        dvergleiche("dperspective", d1, d2, 16);
        {
            uint64_t r = zufall(), si = r & 3, sj = (r >> 2) & 3, sk = (r >> 4) & 3;
            float sm[16], se[4], sr[16];
            memcpy(sm, m, sizeof sm); memcpy(sr, m, sizeof sr);
            sr[4 * sj + sk] = s;
            moo_n_vm_spalte(se, sm, si, sj, sk, s);
            vergleiche("spalte m[i]", se, m + 4 * si, 4);
            vergleiche("spalte m[j][k] = x", sm, sr, 16);
            double dx = moo_n_vm_dspalte(dm, si, sj);
            dvergleiche("dspalte m[i][j]", &dx, &dm[4 * si + sj], 1);
        }

        moo_n_vm_ops(n1, e1, v, w, s); ref_ops(n2, e2, v, w, s);
        vergleiche("ops", n1, n2, 4); vergleiche("neg", e1, e2, 4);
        memcpy(n1, v, sizeof v); memcpy(n2, v, sizeof v);
        moo_n_vm_ops(n1, e1, n1, w, s); ref_ops(n2, e2, n2, w, s); vergleiche("ops alias", n1, n2, 4);
        float p1 = moo_n_vm_punkt(v, w), p2 = ref_punkt(v, w); vergleiche("punkt", &p1, &p2, 1);
        moo_n_vm_kreuz(n1, v, w); ref_kreuz(n2, v, w); vergleiche("kreuz", n1, n2, 3);
        memcpy(n1, v, sizeof v); memcpy(n2, v, sizeof v);
        moo_n_vm_kreuz(n1, n1, w); ref_kreuz(n2, n2, w); vergleiche("kreuz alias", n1, n2, 3);
        p1 = moo_n_vm_betrag(v); p2 = ref_betrag(v); vergleiche("betrag", &p1, &p2, 1);
        moo_n_vm_normalisiere(n1, v); ref_normalisiere(n2, v); vergleiche("normalisiere", n1, n2, 3);
        int g_moo = moo_n_vm_gleich(m, b) & 1, g_ref = 1;
        for (int i = 0; i < 16; ++i) g_ref &= m[i] == b[i];
        if (g_moo != g_ref) { ++fehler; fprintf(stderr, "ABWEICHUNG gleich\n"); }
        if ((moo_n_vm_gleich(m, m) & 1) != 1) {
            int nan = 0; for (int i = 0; i < 16; ++i) nan |= isnan(m[i]);
            if (!nan) { ++fehler; fprintf(stderr, "ABWEICHUNG gleich(m, m)\n"); }
        }
    }
    printf("DIFFERENZ: %ld Runden, %ld Abweichungen\n", runden, fehler);

    float m[16]; fuellen(m, 16, 0);
    float *v = malloc(bench_n * 4 * sizeof *v), *a = malloc(bench_n * 4 * sizeof *a), *c = malloc(bench_n * 4 * sizeof *c);
    if (!v || !a || !c) { fprintf(stderr, "kein Speicher\n"); return 2; }
    fuellen(v, (int)(bench_n * 4), 0);
    double zm[64], zc[64], mm[64], mc[64];
    for (int l = 0; l < laeufe; ++l) {
        double t0 = jetzt(); moo_n_vm_mat4_vec4_viele(a, m, v, bench_n); double t1 = jetzt();
        c_viele(c, m, v, bench_n); double t2 = jetzt();
        zm[l] = t1 - t0; zc[l] = t2 - t1;
        float x[16], y[16]; memcpy(x, m, sizeof x); memcpy(y, m, sizeof y);
        double t3 = jetzt();
        for (uint64_t i = 0; i < bench_n; ++i) moo_n_vm_mat4_mul(x, x, m);
        double t4 = jetzt();
        for (uint64_t i = 0; i < bench_n; ++i) mat4_multiply(y, y, m);
        double t5 = jetzt();
        mm[l] = t4 - t3; mc[l] = t5 - t4;
        vergleiche("bench mat4*mat4", x, y, 16);
    }
    vergleiche("bench mat4*vec4", a, c, (int)(bench_n * 4));
    double summe_moo = 0, summe_c = 0;
    for (uint64_t i = 0; i < bench_n * 4; ++i) { summe_moo += a[i]; summe_c += c[i]; }
    double vm = median(zm, laeufe), vc = median(zc, laeufe), xm = median(mm, laeufe), xc = median(mc, laeufe);
    printf("BENCH mat4*vec4 n=%llu: moo %.3f ms, C %.3f ms, Faktor %.2f, Pruefsumme moo %.6e C %.6e\n",
           (unsigned long long)bench_n, vm * 1e3, vc * 1e3, vm / vc, summe_moo, summe_c);
    printf("BENCH mat4*mat4 n=%llu (je Aufruf): moo %.3f ms, C %.3f ms, Faktor %.2f\n",
           (unsigned long long)bench_n, xm * 1e3, xc * 1e3, xm / xc);
    free(v); free(a); free(c);
    if (fehler) { printf("FEHLER: %ld Abweichungen\n", fehler); return 1; }
    printf("PASS vecmat bitgleich\n");
    return 0;
}

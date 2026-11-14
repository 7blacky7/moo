/* P2-T58 G1 (Plan f40b5980 §4, Review 06d10060 #5): moo_native_math gegen libm
 * (libm NUR im Test).
 * fmod/fmodf: Sonderfall-Kreuzmatrix je Funktion und je >= 10^6 deterministische
 * Zufallspaare (LCG: alle Bitmuster inkl. Subnormale/Inf/NaN, nahe Exponenten,
 * grosse Exponentdifferenz, gleiche Betraege) bitgleich zu C fmod/fmodf.
 * pow/powf: Sonderfalltabelle C99 F.9.4.4 bitgenau (signbit, Inf, NaN-
 * Eigenschaft), exakte Faelle exakt, Zufallslauf mit ULP-Abstand <= 1.
 * Nichtendliche und Null-Referenzen muessen bitgleich sein (auch powf). */
#include "../moo_native_math.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t b64(double x) { uint64_t u; memcpy(&u, &x, 8); return u; }
static uint32_t b32(float x) { uint32_t u; memcpy(&u, &x, 4); return u; }
static int gleich(double a, double c) { return (isnan(a) && isnan(c)) || b64(a) == b64(c); }
static int gleichf(float a, float c) { return (isnan(a) && isnan(c)) || b32(a) == b32(c); }
static long long ord(double x) {
    uint64_t u = b64(x);
    return (u >> 63) ? -(long long)(u & 0x7FFFFFFFFFFFFFFFULL) : (long long)u;
}
static long long ordf(float x) {
    uint32_t u = b32(x);
    return (u >> 31) ? -(long long)(u & 0x7FFFFFFFu) : (long long)u;
}
static uint64_t zustand = 88172645463325252ULL;
static uint64_t lcg(void) {
    zustand = zustand * 6364136223846793005ULL + 1442695040888963407ULL;
    return zustand;
}
static double aus_bits(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }
static float aus_bits_f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static double gleichverteilt(double lo, double hi) {
    return lo + (hi - lo) * ((double)(lcg() >> 11) / 9007199254740992.0);
}
/* pow-Vertrag: bitgleich; sonst nur endliche, von Null verschiedene Referenz
 * mit endlichem Ergebnis innerhalb 1 ULP (Inf/NaN/+-0 immer bitgenau). */
static int pow_ok(double a, double r) {
    if (gleich(a, r)) return 1;
    return isfinite(r) && r != 0.0 && isfinite(a) && llabs(ord(a) - ord(r)) <= 1;
}
static int powf_ok(float a, float r) {
    if (gleichf(a, r)) return 1;
    return isfinite(r) && r != 0.0f && isfinite(a) && llabs(ordf(a) - ordf(r)) <= 1;
}

/* Sonderfall nach Plan f40b5980 §2.3 (Review 42ca1d2e): Ergebnis muss
 * bitgenau sein (Vorzeichen der Null, Inf, NaN-Eigenschaft); alle uebrigen
 * Kombinationen duerfen 1 ULP abweichen. */
static int ist_sonderfall(double x, double y) {
    if (y == 0.0 || x == 1.0 || isnan(x) || isnan(y) || x == 0.0 || isinf(x) || isinf(y)) return 1;
    return x < 0.0 && y != floor(y);
}

static int fehler = 0;
static long faelle = 0, paare_fmod = 0, paare_fmodf = 0;

static void fmod_fall(double x, double y) {
    faelle++; paare_fmod++;
    if (!gleich(moo_native_fmod(x, y), fmod(x, y))) {
        if (++fehler < 8) printf("FAIL fmod %a %a\n", x, y);
    }
}
static void fmodf_fall(float x, float y) {
    faelle++; paare_fmodf++;
    if (!gleichf(moo_native_fmodf(x, y), fmodf(x, y))) {
        if (++fehler < 8) printf("FAIL fmodf %a %a\n", (double)x, (double)y);
    }
}

int main(void) {
    const double sp[] = {
        0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0, -3.0, 0.25, 1.5, -1.5, -7.25,
        INFINITY, -INFINITY, NAN, 1e-310, -1e-310, -4.9e-324, 1e300, -1e300,
        1.7976931348623157e308, 2.2250738585072014e-308, 123456789.0,
        0x1p64, -0x1p64, 0x1p65 + 2, 4503599627370497.0, -4503599627370497.0, 9007199254740994.0,
    };
    const float spf[] = {
        0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f, 3.0f, -3.0f, 0.25f, 1.5f, -1.5f, -7.25f,
        INFINITY, -INFINITY, NAN, 1e-40f, -1e-40f, -1.4e-45f, 1e38f, -1e38f,
        FLT_MAX, FLT_MIN, 16777216.0f, 8388609.0f, 0x1p64f, -0x1p64f, 0x1p-126f, 123456.789f,
    };
    const int n = (int)(sizeof sp / sizeof sp[0]);
    const int nf = (int)(sizeof spf / sizeof spf[0]);

    /* fmod / fmodf: Sonderfall-Kreuzmatrix bitgleich */
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) fmod_fall(sp[i], sp[j]);
    for (int i = 0; i < nf; i++)
        for (int j = 0; j < nf; j++) fmodf_fall(spf[i], spf[j]);

    /* fmod / fmodf: je >= 10^6 Zufallspaare in vier Familien */
    for (long k = 0; k < 250000; k++) {
        /* (1) beliebige Bitmuster: Subnormale, Inf, NaN, grosse Exponentdifferenz */
        fmod_fall(aus_bits(lcg()), aus_bits(lcg()));
        /* (2) nahe Exponenten */
        uint64_t ux = lcg(), uy = (ux & 0xFFF0000000000000ULL) ^ (lcg() & 0x803FFFFFFFFFFFFFULL);
        fmod_fall(aus_bits(ux), aus_bits(uy));
        /* (3) grosser Dividend, kleiner Divisor (inkl. subnormaler Divisoren) */
        uint64_t gx = (lcg() & 0x800FFFFFFFFFFFFFULL) | ((uint64_t)(1800 + lcg() % 246) << 52);
        uint64_t gy = (lcg() & 0x800FFFFFFFFFFFFFULL) | ((uint64_t)(lcg() % 64) << 52);
        fmod_fall(aus_bits(gx), aus_bits(gy));
        /* (4) gleiche Betraege und ganzzahlige Vielfache */
        double v = aus_bits(lcg() & 0x7FEFFFFFFFFFFFFFULL);
        fmod_fall((k & 1) ? -v : v, (k & 2) ? -v : v);

        fmodf_fall(aus_bits_f((uint32_t)(lcg() >> 32)), aus_bits_f((uint32_t)(lcg() >> 32)));
        uint32_t fx = (uint32_t)(lcg() >> 32), fy = (fx & 0xFF800000u) ^ ((uint32_t)(lcg() >> 32) & 0x803FFFFFu);
        fmodf_fall(aus_bits_f(fx), aus_bits_f(fy));
        fmodf_fall((float)(int32_t)lcg() / (float)(1 + (lcg() & 0xffff)),
                   (float)(int32_t)lcg() / (float)(1 + (lcg() & 0xff)));
        uint32_t gfx = ((uint32_t)(lcg() >> 32) & 0x807FFFFFu) | ((uint32_t)(200 + lcg() % 54) << 23);
        uint32_t gfy = ((uint32_t)(lcg() >> 32) & 0x807FFFFFu) | ((uint32_t)(lcg() % 8) << 23);
        fmodf_fall(aus_bits_f(gfx), aus_bits_f(gfy));
    }

    /* pow / powf: Sonderfalltabelle (glibc ist dort exakt) */
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double a = moo_native_pow(sp[i], sp[j]), r = pow(sp[i], sp[j]);
            int ok = ist_sonderfall(sp[i], sp[j]) ? gleich(a, r) : pow_ok(a, r);
            faelle++;
            if (!ok) { fehler++; printf("FAIL pow %a ** %a: %a statt %a\n", sp[i], sp[j], a, r); }
        }
    for (int i = 0; i < nf; i++)
        for (int j = 0; j < nf; j++) {
            float a = moo_native_powf(spf[i], spf[j]), r = powf(spf[i], spf[j]);
            int ok = ist_sonderfall(spf[i], spf[j]) ? gleichf(a, r) : powf_ok(a, r);
            faelle++;
            if (!ok) {
                fehler++;
                printf("FAIL powf %a ** %a: %a statt %a\n", (double)spf[i], (double)spf[j], (double)a, (double)r);
            }
        }

    /* pow / powf: exakt darstellbare Ergebnisse exakt */
    const double exakt[][3] = {
        {2, 10, 1024}, {9, 0.5, 3}, {4, 0.5, 2}, {16, 0.25, 2}, {1e10, 0.5, 1e5}, {10, 15, 1e15},
        {-2, 3, -8}, {2, -1074, 0x1p-1074}, {2, 1023, 0x1p1023}, {0.5, 3, 0.125},
    };
    for (unsigned i = 0; i < sizeof exakt / sizeof exakt[0]; i++) {
        faelle++;
        if (!gleich(moo_native_pow(exakt[i][0], exakt[i][1]), exakt[i][2])) {
            fehler++;
            printf("FAIL exakt %g ** %g\n", exakt[i][0], exakt[i][1]);
        }
    }
    const float exaktf[][3] = {
        {2, 10, 1024}, {9, 0.5f, 3}, {4, 0.5f, 2}, {16, 0.25f, 2}, {1e6f, 0.5f, 1e3f}, {10, 7, 1e7f},
        {-2, 3, -8}, {2, -149, 0x1p-149f}, {2, 127, 0x1p127f}, {0.5f, 3, 0.125f}, {-0.5f, -3, -8},
    };
    for (unsigned i = 0; i < sizeof exaktf / sizeof exaktf[0]; i++) {
        faelle++;
        if (!gleichf(moo_native_powf(exaktf[i][0], exaktf[i][1]), exaktf[i][2])) {
            fehler++;
            printf("FAIL exakt powf %g ** %g\n", (double)exaktf[i][0], (double)exaktf[i][1]);
        }
    }

    /* Referenzfall 10 ** -1: gerundet wie die Referenz (bitgleich, Plan §2.3). */
    faelle += 2;
    if (!gleich(moo_native_pow(10.0, -1.0), pow(10.0, -1.0))) { fehler++; printf("FAIL pow 10 ** -1\n"); }
    if (!gleichf(moo_native_powf(10.0f, -1.0f), powf(10.0f, -1.0f))) { fehler++; printf("FAIL powf 10 ** -1\n"); }

    /* pow / powf: Zufall mit ULP-Abstand; Inf/NaN/+-0 der Referenz bitgenau */
    long long max_ulp = 0, max_ulp_f = 0;
    for (long k = 0; k < 400000; k++) {
        double x, y;
        switch (k % 5) {
            case 0: x = gleichverteilt(0, 4); y = gleichverteilt(-50, 50); break;
            case 1: x = exp(gleichverteilt(-700, 700)); y = gleichverteilt(-1, 1); break;
            case 2: x = gleichverteilt(0.9, 1.1); y = gleichverteilt(-50000, 50000); break;
            case 3: x = gleichverteilt(-10, 10); y = (double)(int64_t)gleichverteilt(-400, 400); break;
            default: x = aus_bits(lcg()); y = aus_bits(lcg()); break;
        }
        double a = moo_native_pow(x, y), r = pow(x, y);
        faelle++;
        if (!isfinite(r) || r == 0.0) {
            if (!gleich(a, r)) { if (++fehler < 8) printf("FAIL pow %a ** %a: %a statt %a\n", x, y, a, r); }
        } else {
            long long d = isfinite(a) ? llabs(ord(a) - ord(r)) : 1LL << 62;
            if (d > max_ulp) max_ulp = d;
        }
        float xf, yf;
        switch (k % 5) {
            case 0: xf = (float)gleichverteilt(0, 100); yf = (float)gleichverteilt(-20, 20); break;
            case 1: xf = (float)gleichverteilt(0.9, 1.1); yf = (float)gleichverteilt(-5000, 5000); break;
            case 2: xf = (float)gleichverteilt(-10, 10); yf = (float)(int64_t)gleichverteilt(-60, 60); break;
            case 3: xf = (float)exp(gleichverteilt(-80, 80)); yf = (float)gleichverteilt(-1.5, 1.5); break;
            default: xf = aus_bits_f((uint32_t)(lcg() >> 32)); yf = aus_bits_f((uint32_t)(lcg() >> 32)); break;
        }
        float af = moo_native_powf(xf, yf), rf = powf(xf, yf);
        faelle++;
        if (!isfinite(rf) || rf == 0.0f) {
            if (!gleichf(af, rf)) {
                if (++fehler < 8) printf("FAIL powf %a ** %a: %a statt %a\n", (double)xf, (double)yf, (double)af, (double)rf);
            }
        } else {
            long long df = isfinite(af) ? llabs(ordf(af) - ordf(rf)) : 1LL << 62;
            if (df > max_ulp_f) max_ulp_f = df;
        }
    }
    printf("moo_native_math: %ld Faelle, %d Fehler, fmod %ld Paare, fmodf %ld Paare, pow max ULP = %lld, powf max ULP = %lld\n",
           faelle, fehler, paare_fmod, paare_fmodf, max_ulp, max_ulp_f);
    return (fehler != 0 || max_ulp > 1 || max_ulp_f > 1 || paare_fmod < 1000000 || paare_fmodf < 1000000) ? 1 : 0;
}

#include "native_suite.h"
#include "moo_noise.h"
#include <math.h>

uint64_t p2_c_summe(const uint64_t *werte, uint64_t n) {
    uint64_t summe = 0;
    for (uint64_t i = 0; i < n; ++i)
        summe += werte[i];
    return summe;
}

void p2_c_vektor(const float *a, const float *b, float *ausgabe, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i)
        ausgabe[i] = a[i] + b[i];
}

void p2_c_matrix(const float *m, const float *v, float *ausgabe, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i) {
        const uint64_t anfang = i * 4;
        for (uint64_t zeile = 0; zeile < 4; ++zeile)
            ausgabe[anfang + zeile] =
                m[zeile] * v[anfang] + m[4 + zeile] * v[anfang + 1] +
                m[8 + zeile] * v[anfang + 2] + m[12 + zeile] * v[anfang + 3];
    }
}

void p2_c_fuellen(float *ausgabe, uint64_t n) {
    for (uint64_t i = 0; i < n; ++i)
        ausgabe[i] = (float)(i % 100);
}

double p2_c_objekte(const Punkt *punkte, uint64_t n) {
    double summe = 0.0;
    for (uint64_t i = 0; i < n; ++i)
        summe += (punkte[i].x + punkte[i].y);
    return summe;
}

double p2_c_betrag(double wert) {
    return fabs(wert);
}

void p2_c_rauschen(const float *koordinaten, float *ausgabe, int32_t seed, uint64_t breite) {
    for (uint64_t y = 0; y < breite; ++y)
        for (uint64_t x = 0; x < breite; ++x)
            ausgabe[y * breite + x] =
                moo_noise_perlin2(seed, koordinaten[x], koordinaten[y]);
}

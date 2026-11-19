#ifndef MOO_P2_NATIVE_SUITE_H
#define MOO_P2_NATIVE_SUITE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    double x;
    double y;
} Punkt;

_Static_assert(sizeof(Punkt) == 16 && offsetof(Punkt, y) == 8, "Punkt-ABI");

uint64_t p2_c_summe(const uint64_t *werte, uint64_t n);
void p2_c_vektor(const float *a, const float *b, float *ausgabe, uint64_t n);
void p2_c_matrix(const float *m, const float *v, float *ausgabe, uint64_t n);
void p2_c_fuellen(float *ausgabe, uint64_t n);
double p2_c_objekte(const Punkt *punkte, uint64_t n);
double p2_c_betrag(double wert);
void p2_c_rauschen(const float *koordinaten, float *ausgabe, int32_t seed, uint64_t breite);

uint64_t p2_moo_summe(const uint64_t *werte, uint64_t n);
void p2_moo_vektor(const float *a, const float *b, float *ausgabe, uint64_t n);
void p2_moo_matrix(const float *m, const float *v, float *ausgabe, uint64_t n);
void p2_moo_fuellen(float *ausgabe, uint64_t n);
double p2_moo_objekte(const Punkt *punkte, uint64_t n);
double p2_moo_betrag(double wert);
void p2_moo_rauschen(const float *koordinaten, float *ausgabe, int32_t seed, uint64_t breite);

#endif

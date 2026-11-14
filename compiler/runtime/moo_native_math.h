/* P2-T58 Batch D (Plan f40b5980 §3.2): freistehende Rechenfunktionen der nativen
 * Wertschicht (Float-Rest, Float-Potenz) ohne libm; hosted, bare und UEFI.
 * Der Compiler senkt Float-% und Float-** auf diese Symbole ab. */
#ifndef MOO_NATIVE_MATH_H
#define MOO_NATIVE_MATH_H
#include <stdint.h>
/* Exakter Rest wie C fmod (C99 F.9.7.1): Vorzeichen von x, |r| < |y|. */
double moo_native_fmod(double x, double y);
float moo_native_fmodf(float x, float y);
/* pow nach C99 F.9.4.4: Sonderfaelle exakt, sonst <= 1 ULP. */
double moo_native_pow(double x, double y);
float moo_native_powf(float x, float y);
#endif

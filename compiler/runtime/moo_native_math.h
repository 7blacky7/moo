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
/* P2-T89: Trigonometrie ohne libm (Bogenmass). Sonderfaelle exakt (+-0 behaelt
 * das Vorzeichen bei sin/tan, cos(+-0) = 1, +-Inf und NaN -> NaN), sonst
 * <= 1 ULP. Exakte Argumentreduktion fuer jedes endliche Argument. */
double moo_native_sin(double x);
double moo_native_cos(double x);
double moo_native_tan(double x);
float moo_native_sinf(float x);
float moo_native_cosf(float x);
float moo_native_tanf(float x);
#endif

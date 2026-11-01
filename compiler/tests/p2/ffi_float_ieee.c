#include <math.h>
#include <float.h>
double p2_ieee_nan(void) { return NAN; }
double p2_ieee_posinf(void) { return INFINITY; }
double p2_ieee_neginf(void) { return -INFINITY; }
double p2_ieee_negzero(void) { return -0.0; }
double p2_ieee_poszero(void) { return 0.0; }
double p2_ieee_tiny(void) { return 1e-50; }
double p2_ieee_nearmax(void) { return 0x1.fffffe0000001p127; }
float p2_ieee_class(float value) {
    if (isnan(value)) return 1.0f;
    if (isinf(value)) return signbit(value) ? 3.0f : 2.0f;
    if (value == 0.0f) return signbit(value) ? 4.0f : 5.0f;
    if (value == FLT_MAX) return 7.0f;
    return 6.0f;
}

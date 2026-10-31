#ifndef MOO_3D_FOG_MATH_H
#define MOO_3D_FOG_MATH_H

/* Shared C/GLSL token body: the CPU gate exercises the shader's float math.
 * sigma(y) = density * exp(-falloff * (y - base_height)).
 * Validated finite inputs: density >= 0, distance >= 0, falloff >= 0.
 * Symmetric low-endpoint formulation avoids exp(large positive a) cancellation.
 * Optical depth is saturated at 80 (transmittance < 2e-35). */
#define MOO_FOG_TAU_FUNCTION \
float moo_fog_tau(float density, float distance, float y0, float y1, \
                  float base_height, float falloff) { \
    if (density <= 0.0 || distance <= 0.0) return 0.0; \
    float a = falloff * abs(y1 - y0); \
    float phi; \
    if (a < 0.05) { \
        phi = 1.0 - a * (0.5 - a * (1.0 / 6.0 - a * (1.0 / 24.0 - a / 120.0))); \
    } else { \
        phi = (1.0 - exp(-a)) / a; \
    } \
    float log_tau = log(density) + log(distance) \
        - falloff * (min(y0, y1) - base_height) + log(phi); \
    if (log_tau <= -80.0) return 0.0; \
    if (log_tau >= 4.38202663467) return 80.0; \
    return exp(log_tau); \
}

#define MOO_FOG_STRINGIFY_RAW(...) #__VA_ARGS__
#define MOO_FOG_STRINGIFY(...) MOO_FOG_STRINGIFY_RAW(__VA_ARGS__)
#define MOO_FOG_TAU_GLSL MOO_FOG_STRINGIFY(MOO_FOG_TAU_FUNCTION)

#endif

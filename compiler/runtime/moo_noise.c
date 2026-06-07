/**
 * moo_noise.c — Seed-parametrisierte Noise-Primitiven (Perlin + fBm).
 *
 * Extrahiert aus moo_world.c (Plan-005 P0.3). Algorithmus 1:1 uebernommen,
 * lediglich der vormals globale `world_noise_seed` ist jetzt ein expliziter
 * Funktionsparameter. Bei identischem Seed sind die Ergebnisse bit-identisch
 * zur frueheren moo_world-Implementierung.
 */
#include "moo_noise.h"
#include <math.h>
#include <stdlib.h>

int moo_noise_scramble_seed(int seed) {
    /* xorshift — entspricht der vormaligen Streuung in moo_world_seed() */
    int x = seed ^ (seed << 13);
    x = x ^ (x >> 7);
    x = x ^ (x << 17);
    return abs(x) & 0x7FFFFFFF;
}

int moo_noise_hash2(int seed, int ix, int iy) {
    int n = ((ix + 1) * 374761 + (iy + 1) * 668265 + seed) & 0x7FFFFFFF;
    n = (n ^ (n >> 11)) & 0x7FFFFFFF;
    n = (n * 45673) & 0x7FFFFFFF;
    n = (n ^ (n >> 15)) & 0x7FFFFFFF;
    n = (n * 31337) & 0x7FFFFFFF;
    n = (n ^ (n >> 13)) & 0x7FFFFFFF;
    return n;
}

static float moo_noise_fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static float moo_noise_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float moo_noise_perlin2(int seed, float x, float y) {
    int ix = (int)floorf(x);
    int iy = (int)floorf(y);
    float fx = x - ix;
    float fy = y - iy;
    float u = moo_noise_fade(fx);
    float v = moo_noise_fade(fy);
    float a = (moo_noise_hash2(seed, ix,     iy    ) % 1000) / 500.0f - 1.0f;
    float b = (moo_noise_hash2(seed, ix + 1, iy    ) % 1000) / 500.0f - 1.0f;
    float c = (moo_noise_hash2(seed, ix,     iy + 1) % 1000) / 500.0f - 1.0f;
    float d = (moo_noise_hash2(seed, ix + 1, iy + 1) % 1000) / 500.0f - 1.0f;
    return moo_noise_lerp(moo_noise_lerp(a, b, u), moo_noise_lerp(c, d, u), v);
}

float moo_noise_fbm(int seed, float x, float y, int octaves, float freq, float amp) {
    float value = 0.0f, max_val = 0.0f;
    float a = 1.0f, f = freq;
    for (int i = 0; i < octaves; i++) {
        value += moo_noise_perlin2(seed, x * f, y * f) * a;
        max_val += a;
        a *= 0.5f;
        f *= 2.0f;
    }
    if (max_val == 0.0f) return 0.0f;
    return (value / max_val) * amp;
}

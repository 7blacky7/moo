/**
 * moo_noise.c — Seed-parametrisierte Noise-Primitiven (Perlin + fBm).
 *
 * Extrahiert aus moo_world.c (Plan-005 P0.3). Algorithmus 1:1 uebernommen,
 * lediglich der vormals globale `world_noise_seed` ist jetzt ein expliziter
 * Funktionsparameter. Bei identischem Seed sind die Ergebnisse bit-identisch
 * zur frueheren moo_world-Implementierung.
 *
 * UBSan-Hinweis (Plan-006 P006-REST): Das Hash-Mixing rechnet intern bewusst
 * mit `uint32_t`. Die urspruengliche Fassung nutzte signed `int` fuer
 * Links-Shifts (seed << 13 / x << 17) und Multiplikationen (n * 45673 etc.),
 * was bei grossen Zwischenwerten signed-integer-overflow ausloest — auf
 * Zweierkomplement-Maschinen wohldefiniertes Wrap-around, aber laut C-Standard
 * undefiniertes Verhalten und damit von -fsanitize=undefined gemeldet. Durch
 * Rechnen in `uint32_t` (definiertes modulo-2^32-Wrap) und Casts nur an den
 * Raendern bleibt das exakte Bitmuster erhalten: alle Ergebnisse sind
 * byte-identisch zur vorherigen signed-Implementierung (verifiziert per
 * Vorher/Nachher-Sample und Worldgen-Aequivalenz-Harness).
 *
 * Signed Rechts-Shifts (>> 7, >> 11, ...) sind implementation-defined, NICHT
 * undefiniert; sie wirken hier ausschliesslich auf nicht-negative Werte
 * (Top-Bit per & 0x7FFFFFFF geloescht) und werden daher beibehalten.
 */
#include "moo_noise.h"
#include <math.h>
#include <stdint.h>

int moo_noise_scramble_seed(int seed) {
    /* xorshift — entspricht der vormaligen Streuung in moo_world_seed().
     * Links-Shifts in uint32_t (definiertes Wrap statt signed-overflow-UB);
     * Bitmuster identisch zur vorherigen (int)-Fassung auf Zweierkomplement. */
    /* x bleibt signed: der Rechts-Shift (x >> 7) ist ein arithmetischer
     * (vorzeichenerhaltender) Shift — implementation-defined, NICHT UB, und
     * muss erhalten bleiben, damit das Bitmuster der frueheren Fassung exakt
     * reproduziert wird. NUR die Links-Shifts loesten signed-overflow-UB aus;
     * sie werden in uint32_t gerechnet (definiertes Wrap) und zurueckgecastet. */
    int32_t x = seed;
    x ^= (int32_t)((uint32_t)x << 13);
    x ^= x >> 7;
    x ^= (int32_t)((uint32_t)x << 17);
    /* Vormals `abs(x) & 0x7FFFFFFF`: abs(INT_MIN) waere UB. Hier explizit
     * der definierte Zweierkomplement-Betrag (|INT_MIN| wrappt auf 0x80000000,
     * dessen Maskierung 0 ergibt) — fuer alle anderen Werte bit-identisch zu
     * abs(x) & 0x7FFFFFFF. */
    uint32_t mag = (x < 0) ? (uint32_t)(-(int64_t)x) : (uint32_t)x;
    return (int)(mag & 0x7FFFFFFFu);
}

int moo_noise_hash2(int seed, int ix, int iy) {
    /* Komposition + Multiplikationen in uint32_t (definiertes Wrap). Nach jedem
     * & 0x7FFFFFFF ist das Top-Bit geloescht, sodass die folgenden >>-Shifts auf
     * nicht-negativen Werten arbeiten — identisch zur signed-Fassung. */
    uint32_t n = ((uint32_t)(ix + 1) * 374761u
                + (uint32_t)(iy + 1) * 668265u
                + (uint32_t)seed) & 0x7FFFFFFFu;
    n = (n ^ (n >> 11)) & 0x7FFFFFFFu;
    n = (n * 45673u) & 0x7FFFFFFFu;
    n = (n ^ (n >> 15)) & 0x7FFFFFFFu;
    n = (n * 31337u) & 0x7FFFFFFFu;
    n = (n ^ (n >> 13)) & 0x7FFFFFFFu;
    return (int)n;
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

/* Gitterwert in [-1, 1] fuer (ix,iy,iz). z wird per uint32_t-Mischung
 * (definiertes Wrap, UB-Policy) in den 2D-Hash eingerechnet. */
static float moo_noise_corner3(int seed, int ix, int iy, int iz) {
    uint32_t h = (uint32_t)moo_noise_hash2(seed, ix, iy);
    h ^= (uint32_t)iz * 0x27d4eb2du;
    h = (h ^ (h >> 15)) * 0x2c1b3c6du;
    h = (h ^ (h >> 12)) & 0x7FFFFFFFu;
    return (float)(h % 1000u) / 500.0f - 1.0f;
}

float moo_noise_value3(int seed, float x, float y, float z) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float u = moo_noise_fade(x - ix), v = moo_noise_fade(y - iy), w = moo_noise_fade(z - iz);
    float z0 = moo_noise_lerp(
        moo_noise_lerp(moo_noise_corner3(seed, ix, iy, iz),     moo_noise_corner3(seed, ix + 1, iy, iz),     u),
        moo_noise_lerp(moo_noise_corner3(seed, ix, iy + 1, iz), moo_noise_corner3(seed, ix + 1, iy + 1, iz), u), v);
    float z1 = moo_noise_lerp(
        moo_noise_lerp(moo_noise_corner3(seed, ix, iy, iz + 1),     moo_noise_corner3(seed, ix + 1, iy, iz + 1),     u),
        moo_noise_lerp(moo_noise_corner3(seed, ix, iy + 1, iz + 1), moo_noise_corner3(seed, ix + 1, iy + 1, iz + 1), u), v);
    return moo_noise_lerp(z0, z1, w);
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

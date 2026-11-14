/* P2-T69 Folgepaket: Differenztest natives Moo (effekt_zustand_nativ.moos)
 * gegen compiler/runtime/moo_compositor_effects_state.c. Die C-Datei wird
 * eingebunden, damit die statischen limits_valid/state_valid/over_mask/estimate
 * als Orakel dienen. Verglichen werden Resultcode bzw. Rueckgabewert UND alle
 * geschriebenen Bytes (Waechterwerte vorbelegt, Alias-Faelle bei config_init). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "moo_compositor_effects_state.c"

_Static_assert(sizeof(MooCompEffectState) == 96 &&
               offsetof(MooCompEffectState, fallback_policy) == 16 &&
               offsetof(MooCompEffectState, reserved) == 20 &&
               offsetof(MooCompEffectState, corners) == 24 &&
               offsetof(MooCompEffectState, shadow) == 32 &&
               offsetof(MooCompEffectState, backdrop) == 48 &&
               offsetof(MooCompEffectState, affine) == 64 &&
               offsetof(MooCompShadow, blur_radius) == 8 && offsetof(MooCompShadow, color) == 12 &&
               offsetof(MooCompBackdrop, tint) == 4 && offsetof(MooCompBackdrop, tint_mix) == 8 &&
               offsetof(MooCompBackdrop, noise) == 9 && offsetof(MooCompBackdrop, reserved) == 10 &&
               offsetof(MooCompBackdrop, noise_seed) == 12,
               "MooCompEffectState-Layout = KompEffektZustand");
_Static_assert(sizeof(MooCompEffectLimits) == 80 &&
               offsetof(MooCompEffectLimits, max_abs_shadow_offset) == 12 &&
               offsetof(MooCompEffectLimits, max_animations_per_surface) == 28 &&
               offsetof(MooCompEffectLimits, min_animation_duration_ns) == 40 &&
               offsetof(MooCompEffectLimits, max_effect_scratch_bytes) == 72, "Limits-Layout");
_Static_assert(sizeof(MooCompEffectStateConfig) == 88 &&
               offsetof(MooCompEffectStateConfig, limits) == 8, "Config-Layout");
_Static_assert(MOO_COMP_EFFECTS_V2 == 0xFFu && MOO_COMP_EFFECT_FALLBACK_ALLOW_APPROXIMATE == 2 &&
               MOO_COMP_EFFECT_WORK_CORNER_PER_PIXEL == 1u && MOO_COMP_EFFECT_WORK_SHADOW_PER_PIXEL == 4u &&
               MOO_COMP_EFFECT_WORK_BACKDROP_PER_PIXEL == 4u && MOO_COMP_EFFECT_WORK_COLOR_PER_PIXEL == 1u &&
               MOO_COMP_EFFECT_WORK_AFFINE_PER_PIXEL == 2u &&
               MOO_COMP_EFFECT_SCRATCH_SHADOW_PER_PIXEL == 8u &&
               MOO_COMP_EFFECT_SCRATCH_BACKDROP_PER_PIXEL == 32u,
               "Konstanten = Literale in effekt_zustand_nativ.moos");

/* Natives Moo (@einstieg). Zeiger als u64-Adresse = gleiche C-ABI. */
uint32_t moo_n_effect_limits_valid(const MooCompEffectLimits *limits);
uint32_t moo_n_effect_state_config_init(MooCompEffectStateConfig *out, uint64_t capabilities,
                                        const MooCompEffectLimits *limits);
uint32_t moo_n_effect_state_valid(const MooCompEffectState *s);
uint64_t moo_n_effect_over_mask(const MooCompEffectState *s, const MooCompEffectLimits *l);
uint32_t moo_n_effect_estimate(const MooCompEffectState *s, uint64_t pixels, uint64_t *work, uint64_t *scratch);

static uint64_t rng_state = 0x5851f42d4c957f2dull;
static uint64_t rng64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static uint32_t rng(void) { return (uint32_t)(rng64() >> 32); }
static uint64_t bereich(uint64_t lo, uint64_t hi) { return lo + rng64() % (hi - lo + 1u); }

static long faelle, fehler;
static long lim_zahl[2], zust_zahl[2], cfg_rc[2], est_rc[3], over_nicht_null, alias_faelle;

static void melde(const char *was, long fall, uint64_t c, uint64_t m) {
    if (fehler < 20) fprintf(stderr, "ABWEICHUNG %s Fall %ld: C=%llu Moo=%llu\n", was, fall,
                             (unsigned long long)c, (unsigned long long)m);
    fehler++;
}

/* Gueltige, zufaellig verschaerfte Limits; mit p=1/3 ein Feld verletzt/Grenze.
 * Fall 13 (per_surface > per_client) ist hier GUELTIG (Unterschied zu animation.c). */
static void limits_wuerfeln(MooCompEffectLimits *l) {
    *l = moo_comp_effect_limits_default();
    if (rng() % 2u) {
        l->max_corner_radius = (uint16_t)bereich(0, 4096);
        l->max_shadow_blur_radius = (uint16_t)bereich(0, 64);
        l->max_shadow_spread_radius = (uint16_t)bereich(0, 64);
        l->max_backdrop_blur_radius = (uint16_t)bereich(0, 32);
        l->max_saturation_q8_8 = (uint16_t)bereich(256, 1024);
        l->max_abs_shadow_offset = (int32_t)bereich(0, 4096);
        l->max_abs_affine_coefficient = (int32_t)bereich(65536, 1048576);
        l->max_abs_affine_translation = (int32_t)bereich(0, 2147418112);
        l->max_abs_affine_origin = (int32_t)bereich(0, 2147418112);
        l->max_animations_per_surface = (uint32_t)bereich(1, 8);
        l->max_animations_per_client = (uint32_t)bereich(1, 64);
        l->min_animation_duration_ns = bereich(1000000u, 5000000000u);
        l->max_animation_duration_ns = bereich(l->min_animation_duration_ns, 600000000000u);
        l->max_animation_timeline_ns = bereich(l->max_animation_duration_ns, 3600000000000u);
        l->max_effect_work_units_per_frame = bereich(1, 268435456u);
        l->max_effect_scratch_bytes = bereich(1, 67108864u);
    }
    if (rng() % 3u != 0u) return;
    switch (rng() % 22u) {
        case 0: l->reserved = (uint16_t)(rng() | 1u); break;
        case 1: l->reserved_alignment = rng() | 1u; break;
        case 2: l->max_corner_radius = (uint16_t)(4097u + rng() % 60000u); break;
        case 3: l->max_shadow_blur_radius = (uint16_t)(rng() % 2u ? 65u : 64u); break;
        case 4: l->max_shadow_spread_radius = (uint16_t)(rng() % 2u ? 65u : 0u); break;
        case 5: l->max_backdrop_blur_radius = (uint16_t)(rng() % 2u ? 33u : 32u); break;
        case 6: l->max_saturation_q8_8 = (uint16_t)(rng() % 2u ? 255u : 1025u); break;
        case 7: l->max_abs_shadow_offset = rng() % 2u ? -1 : 4097; break;
        case 8: l->max_abs_affine_coefficient = rng() % 2u ? 65535 : 1048577; break;
        case 9: l->max_abs_affine_translation = rng() % 2u ? -1 : INT32_MAX; break;
        case 10: l->max_abs_affine_origin = rng() % 2u ? INT32_MIN : 2147418113; break;
        case 11: l->max_animations_per_surface = rng() % 2u ? 0u : 9u; break;
        case 12: l->max_animations_per_client = rng() % 2u ? 0u : 65u; break;
        case 13: l->max_animations_per_surface = 8u; l->max_animations_per_client = 7u; break;
        case 14: l->min_animation_duration_ns = 999999u; break;
        case 15: l->min_animation_duration_ns = l->max_animation_duration_ns + 1u; break;
        case 16: l->max_animation_duration_ns = 600000000001u; break;
        case 17: l->max_animation_timeline_ns = l->max_animation_duration_ns - 1u; break;
        case 18: l->max_animation_timeline_ns = 3600000000001u; break;
        case 19: l->max_effect_work_units_per_frame = rng() % 2u ? 0u : 268435457u; break;
        case 20: l->max_effect_scratch_bytes = rng() % 2u ? 0u : 67108865u; break;
        default: l->min_animation_duration_ns = l->max_animation_duration_ns; break;
    }
}

static int32_t i32_um(int32_t g) {
    uint32_t a = rng() % 8u;
    if (a == 0u) return (int32_t)rng();
    if (a == 1u) return g;
    if (a == 2u) return -g;
    if (a == 3u && g < INT32_MAX) return g + 1;
    if (a == 4u && g > INT32_MIN + 1) return -g - 1;
    return (int32_t)((int64_t)(rng64() % ((uint64_t)(g < 0 ? 0 : g) + 1u)) * ((rng() & 1u) ? 1 : -1));
}

/* Zustand: aktive Effekte mit Werten um die Limits, inaktive meist neutral. */
static void zustand_wuerfeln(MooCompEffectState *s, const MooCompEffectLimits *l) {
    *s = moo_comp_effect_state_neutral();
    uint64_t m = rng() % 4u == 0u ? 0xFFu : (rng() & 0xFFu);
    s->enabled_mask = m;
    s->required_mask = rng() % 2u ? (m & rng()) : 0u;
    s->fallback_policy = rng() % 3u;
    if (m & MOO_COMP_EFFECT_CORNER_CLIP) {
        s->corners.top_left = (uint16_t)bereich(0, (uint64_t)l->max_corner_radius + 1u);
        s->corners.top_right = (uint16_t)bereich(0, (uint64_t)l->max_corner_radius + 1u);
        s->corners.bottom_right = (uint16_t)bereich(0, (uint64_t)l->max_corner_radius + 1u);
        s->corners.bottom_left = (uint16_t)(rng() % 16u == 0u ? 65535u : bereich(0, l->max_corner_radius));
    }
    if (m & MOO_COMP_EFFECT_SHADOW) {
        s->shadow.offset_x = i32_um(l->max_abs_shadow_offset);
        s->shadow.offset_y = i32_um(l->max_abs_shadow_offset);
        s->shadow.blur_radius = (uint16_t)bereich(0, (uint64_t)l->max_shadow_blur_radius + 1u);
        s->shadow.spread_radius = (uint16_t)bereich(0, (uint64_t)l->max_shadow_spread_radius + 1u);
        s->shadow.color = (MooCompRgba8){ (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng() };
    }
    if (m & MOO_COMP_EFFECT_BACKDROP_BLUR) s->backdrop.blur_radius = (uint16_t)bereich(0, (uint64_t)l->max_backdrop_blur_radius + 1u);
    if (m & MOO_COMP_EFFECT_SATURATION) s->backdrop.saturation_q8_8 = (uint16_t)bereich(0, (uint64_t)l->max_saturation_q8_8 + 1u);
    if (m & MOO_COMP_EFFECT_TINT) {
        s->backdrop.tint = (MooCompRgba8){ (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng() };
        s->backdrop.tint_mix = (uint8_t)rng();
    }
    if (m & MOO_COMP_EFFECT_NOISE) { s->backdrop.noise = (uint8_t)rng(); s->backdrop.noise_seed = rng(); }
    if (m & MOO_COMP_EFFECT_AFFINE_2D) {
        s->affine.m11 = i32_um(l->max_abs_affine_coefficient);
        s->affine.m12 = i32_um(l->max_abs_affine_coefficient);
        s->affine.m21 = i32_um(l->max_abs_affine_coefficient);
        s->affine.m22 = i32_um(l->max_abs_affine_coefficient);
        s->affine.tx = i32_um(l->max_abs_affine_translation);
        s->affine.ty = i32_um(l->max_abs_affine_translation);
        s->affine.origin_x = i32_um(l->max_abs_affine_origin);
        s->affine.origin_y = i32_um(l->max_abs_affine_origin);
    }
    if (rng() % 3u != 0u) return;
    switch (rng() % 30u) {   /* gezielte Verletzungen / Grenzfaelle */
        case 0: s->enabled_mask |= (uint64_t)1u << (8u + rng() % 56u); break;
        case 1: s->required_mask |= (uint64_t)1u << (8u + rng() % 56u); break;
        case 2: s->required_mask = s->enabled_mask ^ 0xFFu; break;
        case 3: s->reserved = rng() | 1u; break;
        case 4: s->backdrop.reserved = (uint16_t)(rng() | 1u); break;
        case 5: s->fallback_policy = 3u + rng() % 5u; break;
        case 6: s->corners.top_right = (uint16_t)(rng() | 1u); break;
        case 7: s->shadow.offset_x = (int32_t)(rng() | 1u); break;
        case 8: s->shadow.offset_y = -1; break;
        case 9: s->shadow.blur_radius = 1u; break;
        case 10: s->shadow.spread_radius = 7u; break;
        case 11: s->shadow.color.a = 1u; break;
        case 12: s->shadow.color.r = 255u; break;
        case 13: s->backdrop.blur_radius = 2u; break;
        case 14: s->backdrop.saturation_q8_8 = rng() % 2u ? 255u : 257u; break;
        case 15: s->backdrop.tint_mix = 1u; break;
        case 16: s->backdrop.tint.b = 9u; break;
        case 17: s->backdrop.noise = 3u; break;
        case 18: s->backdrop.noise_seed = 1u; break;
        case 19: s->affine.m11 = 65535; break;
        case 20: s->affine.m12 = 1; break;
        case 21: s->affine.ty = -5; break;
        case 22: s->affine.origin_y = 1; break;
        case 23: s->affine.m22 = 0; break;
        case 24: s->enabled_mask = 0u; s->required_mask = 0u; break;
        default: break;
    }
}

static void limits_faelle(long runden) {
    MooCompEffectLimits l;
    faelle++;
    if ((uint32_t)moo_comp_effect_limits_valid(NULL) != moo_n_effect_limits_valid(NULL)) melde("limits NULL", -1, 0, 0);
    for (long k = 0; k < runden; k++) {
        limits_wuerfeln(&l);
        uint32_t c = (uint32_t)moo_comp_effect_limits_valid(&l), m = moo_n_effect_limits_valid(&l);
        faelle++;
        if (c != m) { melde("limits_valid", k, c, m); continue; }
        lim_zahl[c != 0u]++;
    }
}

static void zustand_faelle(long runden) {
    MooCompEffectLimits l;
    MooCompEffectState s;
    faelle++;
    if ((uint32_t)moo_comp_effect_state_valid(NULL) != moo_n_effect_state_valid(NULL)) melde("state NULL", -1, 0, 0);
    for (long k = 0; k < runden; k++) {
        limits_wuerfeln(&l);
        if (!moo_comp_effect_limits_valid(&l)) l = moo_comp_effect_limits_default();
        zustand_wuerfeln(&s, &l);
        uint32_t c = (uint32_t)moo_comp_effect_state_valid(&s), m = moo_n_effect_state_valid(&s);
        faelle++;
        if (c != m) { melde("state_valid", k, c, m); continue; }
        zust_zahl[c != 0u]++;

        uint64_t oc = moo_comp_effect_over_mask(&s, &l), om = moo_n_effect_over_mask(&s, &l);
        faelle++;
        if (oc != om) { melde("over_mask", k, oc, om); continue; }
        if (oc) over_nicht_null++;

        uint64_t pixel;
        switch (rng() % 6u) {
            case 0: pixel = 0u; break;
            case 1: pixel = UINT64_MAX / (bereich(1, 40)) + (rng() % 3u); break;   /* Ueberlauf-Grenze */
            case 2: pixel = rng64(); break;
            default: pixel = bereich(1, 8294400u); break;                           /* bis 4K */
        }
        uint64_t wc = 0xC0FFEEull, sc = 0xBEEFull, wm = wc, sm = sc;
        uint32_t ec = (uint32_t)moo_comp_effect_estimate(&s, pixel, &wc, &sc);
        uint32_t em = moo_n_effect_estimate(&s, pixel, &wm, &sm);
        faelle++;
        if (ec != em || wc != wm || sc != sm) { melde("estimate", k, ec * 1000u + (wc != wm) * 10u + (sc != sm), em); continue; }
        if (ec < 3u) est_rc[ec]++;
    }
}

/* config_init: frei liegende und ueberlappende Ziel-/Quellbereiche. */
static void config_faelle(long runden) {
    static uint64_t puffer_c[64], puffer_m[64];
    for (long k = 0; k < runden; k++) {
        MooCompEffectLimits l;
        limits_wuerfeln(&l);
        uint64_t caps = rng() % 8u == 0u ? (rng64() | 0x100u) : (rng() & 0xFFu);
        for (int i = 0; i < 64; i++) puffer_c[i] = puffer_m[i] = 0xA5A5A5A5A5A5A5A5ull ^ (uint64_t)i;
        int alias = rng() % 3u == 0u;
        size_t ziel = 16, quelle = 40;                      /* Wortindizes, disjunkt */
        if (alias) { quelle = (size_t)(16 + (int)(rng() % 23u) - 11); alias_faelle++; }   /* Ueberlappung -11..+11 Worte */
        memcpy(&puffer_c[quelle], &l, sizeof l);
        memcpy(&puffer_m[quelle], &l, sizeof l);
        uint32_t c = (uint32_t)moo_comp_effect_state_config_init((MooCompEffectStateConfig *)&puffer_c[ziel], caps,
                                                                (const MooCompEffectLimits *)&puffer_c[quelle]);
        uint32_t m = moo_n_effect_state_config_init((MooCompEffectStateConfig *)&puffer_m[ziel], caps,
                                                    (const MooCompEffectLimits *)&puffer_m[quelle]);
        faelle++;
        if (c != m) { melde("config_init rc", k, c, m); continue; }
        if (c < 2u) cfg_rc[c]++;
        if (memcmp(puffer_c, puffer_m, sizeof puffer_c) != 0) melde("config_init Speicher", k, alias, quelle);
    }
    MooCompEffectLimits l = moo_comp_effect_limits_default();
    MooCompEffectStateConfig o;
    faelle += 2;
    if ((uint32_t)moo_comp_effect_state_config_init(NULL, 0u, &l) != moo_n_effect_state_config_init(NULL, 0u, &l)) melde("config NULL out", -1, 0, 0);
    if ((uint32_t)moo_comp_effect_state_config_init(&o, 0u, NULL) != moo_n_effect_state_config_init(&o, 0u, NULL)) melde("config NULL limits", -2, 0, 0);
}

int main(int argc, char **argv) {
    long runden = argc > 1 ? atol(argv[1]) : 300000;
    limits_faelle(runden);
    zustand_faelle(runden);
    config_faelle(runden);
    printf("DIFF faelle=%ld abweichungen=%ld\n", faelle, fehler);
    printf("DIFF limits_valid   gueltig=%ld ungueltig=%ld\n", lim_zahl[1], lim_zahl[0]);
    printf("DIFF state_valid    gueltig=%ld ungueltig=%ld over_mask!=0: %ld\n", zust_zahl[1], zust_zahl[0], over_nicht_null);
    printf("DIFF estimate       OK=%ld LIMIT=%ld\n", est_rc[0], est_rc[2]);
    printf("DIFF config_init    OK=%ld INVALID=%ld alias=%ld\n", cfg_rc[0], cfg_rc[1], alias_faelle);
    if (runden > 0 && (lim_zahl[0] == 0 || lim_zahl[1] == 0 || zust_zahl[0] == 0 || zust_zahl[1] == 0 ||
                       over_nicht_null == 0 || est_rc[0] == 0 || est_rc[2] == 0 ||
                       cfg_rc[0] == 0 || cfg_rc[1] == 0 || alias_faelle == 0)) {
        puts("DIFF: nicht alle Pfade erreicht"); fehler++;
    }
    if (fehler) { puts("ERGEBNIS: ROT"); return 1; }
    puts("ERGEBNIS: GRUEN (natives Moo identisch zu moo_compositor_effects_state.c)");
    return 0;
}

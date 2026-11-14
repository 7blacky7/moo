/* P2-T69: Differenztest natives Moo (animation_pruefung_nativ.moos, Dekodierung
 * aus animation_nativ.moos) gegen die Validierung in
 * compiler/runtime/moo_compositor_animation.c. Die C-Datei wird eingebunden,
 * damit die statischen limits_valid/validate_value als Orakel dienen.
 * Verglichen wird je Fall der Resultcode (die Funktionen schreiben nichts). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "moo_compositor_animation.c"

_Static_assert(sizeof(MooCompEffectLimits) == 80 &&
               offsetof(MooCompEffectLimits, reserved) == 10 &&
               offsetof(MooCompEffectLimits, max_abs_shadow_offset) == 12 &&
               offsetof(MooCompEffectLimits, max_abs_affine_coefficient) == 16 &&
               offsetof(MooCompEffectLimits, max_abs_affine_translation) == 20 &&
               offsetof(MooCompEffectLimits, max_abs_affine_origin) == 24 &&
               offsetof(MooCompEffectLimits, max_animations_per_surface) == 28 &&
               offsetof(MooCompEffectLimits, max_animations_per_client) == 32 &&
               offsetof(MooCompEffectLimits, reserved_alignment) == 36 &&
               offsetof(MooCompEffectLimits, min_animation_duration_ns) == 40 &&
               offsetof(MooCompEffectLimits, max_animation_duration_ns) == 48 &&
               offsetof(MooCompEffectLimits, max_animation_timeline_ns) == 56 &&
               offsetof(MooCompEffectLimits, max_effect_work_units_per_frame) == 64 &&
               offsetof(MooCompEffectLimits, max_effect_scratch_bytes) == 72,
               "MooCompEffectLimits-Layout = KompAnimLimits");
_Static_assert(offsetof(MooCompAnimationDesc, delay_ns) == 8 &&
               offsetof(MooCompAnimationDesc, duration_ns) == 16 &&
               offsetof(MooCompAnimationDesc, repeat_count) == 24 &&
               offsetof(MooCompAnimationDesc, flags) == 40 &&
               offsetof(MooCompAnimationDesc, reserved) == 44 &&
               offsetof(MooCompAnimationDesc, from) == 48 &&
               offsetof(MooCompAnimationDesc, to) == 80, "Desc-Kopf = KompAnimKopf");
_Static_assert(MOO_COMP_EFFECT_CORNER_CLIP == 1u && MOO_COMP_EFFECT_SHADOW == 2u &&
               MOO_COMP_EFFECT_BACKDROP_BLUR == 4u && MOO_COMP_EFFECT_SATURATION == 8u &&
               MOO_COMP_EFFECT_TINT == 16u && MOO_COMP_EFFECT_NOISE == 32u &&
               MOO_COMP_EFFECT_AFFINE_2D == 64u, "Effekt-Bits");
_Static_assert(MOO_COMP_EFFECT_DEFAULT_MAX_CORNER_RADIUS == 4096 &&
               MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_BLUR_RADIUS == 64 &&
               MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_SPREAD_RADIUS == 64 &&
               MOO_COMP_EFFECT_DEFAULT_MAX_BACKDROP_BLUR_RADIUS == 32 &&
               MOO_COMP_EFFECT_DEFAULT_MAX_SATURATION_Q8_8 == 1024 &&
               MOO_COMP_SATURATION_ONE == 256 &&
               MOO_COMP_EFFECT_DEFAULT_MAX_ABS_SHADOW_OFFSET == 4096 &&
               MOO_COMP_Q16_ONE == 65536 &&
               MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_COEFFICIENT == 1048576 &&
               MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_TRANSLATION == 2147418112 &&
               MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_ORIGIN == 2147418112 &&
               MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_SURFACE == 8u &&
               MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_CLIENT == 64u &&
               MOO_COMP_EFFECT_DEFAULT_MIN_ANIMATION_DURATION_NS == 1000000u &&
               MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_DURATION_NS == 600000000000u &&
               MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_TIMELINE_NS == 3600000000000u &&
               MOO_COMP_EFFECT_DEFAULT_MAX_WORK_UNITS == 268435456u &&
               MOO_COMP_EFFECT_DEFAULT_MAX_SCRATCH_BYTES == 67108864u,
               "Defaults = Literale in animation_pruefung_nativ.moos");

/* Natives Moo (@einstieg). Zeiger als u64-Adresse = gleiche C-ABI. */
uint32_t moo_n_anim_limits_valid(const MooCompEffectLimits *limits);
uint32_t moo_n_anim_validate_value(uint32_t property, const MooCompAnimationValue *value,
                                   const MooCompEffectLimits *limits, uint64_t enabled_effect_mask);
uint32_t moo_n_anim_validate_desc(const MooCompAnimationDesc *desc, const MooCompEffectLimits *limits,
                                  uint64_t enabled_effect_mask);

static uint64_t rng_state = 0x2545f4914f6cdd1dull;
static uint64_t rng64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static uint32_t rng(void) { return (uint32_t)(rng64() >> 32); }
static uint64_t bereich(uint64_t lo, uint64_t hi) { return lo + rng64() % (hi - lo + 1u); }

static long faelle, fehler;
static long lim_zahl[2], wert_rc[3], desc_rc[3];

static void melde(const char *was, long fall, uint32_t c, uint32_t m) {
    if (fehler < 20) fprintf(stderr, "ABWEICHUNG %s Fall %ld: C=%u Moo=%u\n", was, fall, c, m);
    fehler++;
}

static void limits_standard(MooCompEffectLimits *l) {
    memset(l, 0, sizeof *l);
    l->max_corner_radius = MOO_COMP_EFFECT_DEFAULT_MAX_CORNER_RADIUS;
    l->max_shadow_blur_radius = MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_BLUR_RADIUS;
    l->max_shadow_spread_radius = MOO_COMP_EFFECT_DEFAULT_MAX_SHADOW_SPREAD_RADIUS;
    l->max_backdrop_blur_radius = MOO_COMP_EFFECT_DEFAULT_MAX_BACKDROP_BLUR_RADIUS;
    l->max_saturation_q8_8 = MOO_COMP_EFFECT_DEFAULT_MAX_SATURATION_Q8_8;
    l->max_abs_shadow_offset = MOO_COMP_EFFECT_DEFAULT_MAX_ABS_SHADOW_OFFSET;
    l->max_abs_affine_coefficient = MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_COEFFICIENT;
    l->max_abs_affine_translation = MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_TRANSLATION;
    l->max_abs_affine_origin = MOO_COMP_EFFECT_DEFAULT_MAX_ABS_AFFINE_ORIGIN;
    l->max_animations_per_surface = MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_SURFACE;
    l->max_animations_per_client = MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATIONS_PER_CLIENT;
    l->min_animation_duration_ns = MOO_COMP_EFFECT_DEFAULT_MIN_ANIMATION_DURATION_NS;
    l->max_animation_duration_ns = MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_DURATION_NS;
    l->max_animation_timeline_ns = MOO_COMP_EFFECT_DEFAULT_MAX_ANIMATION_TIMELINE_NS;
    l->max_effect_work_units_per_frame = MOO_COMP_EFFECT_DEFAULT_MAX_WORK_UNITS;
    l->max_effect_scratch_bytes = MOO_COMP_EFFECT_DEFAULT_MAX_SCRATCH_BYTES;
}

/* Gueltige, zufaellig verschaerfte Limits; mit p=1/3 ein Feld gezielt verletzt
 * oder an eine Grenze gesetzt. */
static void limits_wuerfeln(MooCompEffectLimits *l) {
    limits_standard(l);
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
        l->max_animations_per_client = (uint32_t)bereich(l->max_animations_per_surface, 64);
        l->min_animation_duration_ns = bereich(1000000u, 5000000000u);
        l->max_animation_duration_ns = bereich(l->min_animation_duration_ns, 600000000000u);
        l->max_animation_timeline_ns = bereich(l->max_animation_duration_ns, 3600000000000u);
        l->max_effect_work_units_per_frame = bereich(1, 268435456u);
        l->max_effect_scratch_bytes = bereich(1, 67108864u);
    }
    if (rng() % 3u != 0u) return;
    switch (rng() % 24u) {
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
        case 21: l->min_animation_duration_ns = l->max_animation_duration_ns; break;   /* Grenze: gueltig */
        case 22: l->max_animation_timeline_ns = l->max_animation_duration_ns; break;   /* Grenze: gueltig */
        default: l->max_animations_per_client = l->max_animations_per_surface; break;  /* Grenze: gueltig */
    }
}

static uint32_t vorz(int64_t v) { return (uint32_t)(v < 0 ? v + INT64_C(4294967296) : v); }
static uint32_t signiert_um(uint32_t grenze) {
    /* um die Grenze herum: -g-1 .. g+1, gelegentlich voller Bereich */
    if (rng() % 8u == 0u) return rng();
    int64_t g = (int64_t)grenze;
    int64_t v = (int64_t)(rng64() % (uint64_t)(2 * g + 3)) - g - 1;
    if (rng() % 4u == 0u) v = rng() % 2u ? g + (int64_t)(rng() % 2u) : -g - (int64_t)(rng() % 2u);
    return vorz(v);
}

static void wert_wuerfeln(uint32_t prop, const MooCompEffectLimits *l, MooCompAnimationValue *v) {
    memset(v, 0, sizeof *v);
    switch (prop) {
        case 1: v->word[0] = rng() % 4u ? (uint32_t)bereich(0, 65536) : 65537u + rng() % 3u; break;
        case 2: for (int i = 0; i < 4; i++)
                    v->word[i] = rng() % 8u ? (uint32_t)bereich(0, (uint64_t)l->max_corner_radius + 1u)
                                           : (rng() % 2u ? 65535u : 65536u + rng() % 10u);
                break;
        case 3: v->word[0] = signiert_um((uint32_t)(l->max_abs_shadow_offset < 0 ? 0 : l->max_abs_shadow_offset));
                v->word[1] = signiert_um((uint32_t)(l->max_abs_shadow_offset < 0 ? 0 : l->max_abs_shadow_offset));
                v->word[2] = rng() % 8u ? (uint32_t)bereich(0, (uint64_t)l->max_shadow_blur_radius + 1u) : 65536u;
                v->word[3] = rng() % 8u ? (uint32_t)bereich(0, (uint64_t)l->max_shadow_spread_radius + 1u) : 65536u;
                v->word[4] = rng();
                break;
        case 4: v->word[0] = rng() % 3u ? (uint32_t)bereich(0, (uint64_t)l->max_backdrop_blur_radius + 1u) : 0u;
                v->word[1] = rng() % 3u ? (uint32_t)bereich(0, (uint64_t)l->max_saturation_q8_8 + 1u) : 256u;
                v->word[2] = rng() % 3u ? rng() : 0u;
                v->word[3] = rng() % 3u ? (uint32_t)bereich(0, 256) : 0u;
                v->word[4] = rng() % 3u ? (uint32_t)bereich(0, 256) : 0u;
                v->word[5] = rng() % 3u ? rng() : 0u;
                if (rng() % 16u == 0u) v->word[0] = 65536u;
                if (rng() % 16u == 0u) v->word[1] = 65536u;
                break;
        case 5: for (int i = 0; i < 4; i++) v->word[i] = signiert_um((uint32_t)(l->max_abs_affine_coefficient < 0 ? 0 : l->max_abs_affine_coefficient));
                for (int i = 4; i < 8; i++) v->word[i] = rng() % 2u ? rng() : signiert_um(1000000u);
                break;
        default: for (int i = 0; i < 8; i++) v->word[i] = rng() % 4u ? 0u : rng(); break;
    }
    if (rng() % 16u == 0u) v->word[rng() % 8u] = rng() | 1u;   /* unbenutztes Wort != 0 */
}

static uint64_t maske_wuerfeln(void) {
    uint64_t m = rng() % 4u == 0u ? 0x7Fu : (rng() & 0x7Fu);
    if (rng() % 32u == 0u) m |= (uint64_t)1u << (7u + rng() % 57u);
    return m;
}

static void limits_faelle(long runden) {
    MooCompEffectLimits l;
    faelle++;
    if ((uint32_t)moo_comp_animation_limits_valid(NULL) != moo_n_anim_limits_valid(NULL)) melde("limits NULL", -1, 0, 0);
    for (long k = 0; k < runden; k++) {
        limits_wuerfeln(&l);
        uint32_t c = (uint32_t)moo_comp_animation_limits_valid(&l), m = moo_n_anim_limits_valid(&l);
        faelle++;
        if (c != m) { melde("limits_valid", k, c, m); continue; }
        lim_zahl[c != 0u]++;
    }
}

static void wert_faelle(long runden) {
    MooCompEffectLimits l;
    MooCompAnimationValue v;
    for (long k = 0; k < runden; k++) {
        limits_wuerfeln(&l);
        uint32_t prop = rng() % 16u == 0u ? (rng() % 2u ? 0u : 6u + rng() % 50u) : rng() % 5u + 1u;
        wert_wuerfeln(prop, &l, &v);
        uint64_t maske = maske_wuerfeln();
        uint32_t c = (uint32_t)moo_comp_animation_validate_value(prop, &v, &l, maske);
        uint32_t m = moo_n_anim_validate_value(prop, &v, &l, maske);
        faelle++;
        if (c != m) { melde("validate_value", k, c, m); continue; }
        if (c < 3u) wert_rc[c]++;
    }
    limits_standard(&l);
    memset(&v, 0, sizeof v);
    faelle += 2;
    if ((uint32_t)moo_comp_animation_validate_value(1u, NULL, &l, 0x7Fu) != moo_n_anim_validate_value(1u, NULL, &l, 0x7Fu)) melde("value NULL", -1, 0, 0);
    if ((uint32_t)moo_comp_animation_validate_value(1u, &v, NULL, 0x7Fu) != moo_n_anim_validate_value(1u, &v, NULL, 0x7Fu)) melde("value NULL limits", -2, 0, 0);
}

static void desc_wuerfeln(MooCompAnimationDesc *d, const MooCompEffectLimits *l) {
    uint32_t art = rng() % 48u;
    memset(d, 0, sizeof *d);
    d->token = rng64() | 1u;
    d->repeat_count = (uint32_t)bereich(1, 4);
    d->property = rng() % 5u + 1u;
    d->easing = rng() % 4u;
    d->direction = rng() % 4u;
    d->duration_ns = bereich(l->min_animation_duration_ns, l->max_animation_duration_ns < l->min_animation_duration_ns
                                                            ? l->min_animation_duration_ns : l->max_animation_duration_ns);
    d->delay_ns = rng() % 2u ? bereich(0, 1000000000u) : 0u;
    wert_wuerfeln(d->property, l, &d->from);
    wert_wuerfeln(d->property, l, &d->to);
    if (rng() % 2u) { d->to = d->from; }
    switch (art) {
        case 0: d->token = 0u; break;
        case 1: d->repeat_count = 0u; break;
        case 2: d->property = 0u; break;
        case 3: d->property = 6u + rng() % 9u; break;
        case 4: d->easing = 4u + rng() % 9u; break;
        case 5: d->direction = 4u + rng() % 9u; break;
        case 6: d->flags = rng() | 1u; break;
        case 7: d->reserved = rng() | 1u; break;
        case 8: d->duration_ns = l->min_animation_duration_ns - 1u; break;
        case 9: d->duration_ns = l->max_animation_duration_ns + 1u; break;
        case 10: d->duration_ns = l->min_animation_duration_ns; break;
        case 11: d->duration_ns = l->max_animation_duration_ns; break;
        case 12: d->repeat_count = UINT32_MAX; break;                          /* Zeitleiste/Ueberlauf */
        case 13: d->delay_ns = UINT64_MAX - rng() % 1000u; break;               /* delay > UINT64_MAX - gesamt */
        case 14: d->delay_ns = l->max_animation_timeline_ns - d->duration_ns * d->repeat_count; break; /* exakt Grenze */
        case 15: d->delay_ns = l->max_animation_timeline_ns - d->duration_ns * d->repeat_count + 1u; break;
        case 16: d->repeat_count = (uint32_t)(l->max_animation_timeline_ns / d->duration_ns) + 1u; break;
        default: break;
    }
}

static void desc_faelle(long runden) {
    MooCompEffectLimits l;
    MooCompAnimationDesc d;
    for (long k = 0; k < runden; k++) {
        limits_wuerfeln(&l);
        desc_wuerfeln(&d, &l);
        uint64_t maske = maske_wuerfeln();
        uint32_t c = (uint32_t)moo_comp_animation_validate_desc(&d, &l, maske);
        uint32_t m = moo_n_anim_validate_desc(&d, &l, maske);
        faelle++;
        if (c != m) { melde("validate_desc", k, c, m); continue; }
        if (c < 3u) desc_rc[c]++;
    }
    limits_standard(&l);
    desc_wuerfeln(&d, &l);
    faelle += 2;
    if ((uint32_t)moo_comp_animation_validate_desc(NULL, &l, 0x7Fu) != moo_n_anim_validate_desc(NULL, &l, 0x7Fu)) melde("desc NULL", -1, 0, 0);
    if ((uint32_t)moo_comp_animation_validate_desc(&d, NULL, 0x7Fu) != moo_n_anim_validate_desc(&d, NULL, 0x7Fu)) melde("desc NULL limits", -2, 0, 0);
}

int main(int argc, char **argv) {
    long runden = argc > 1 ? atol(argv[1]) : 400000;
    limits_faelle(runden);
    wert_faelle(runden);
    desc_faelle(runden);
    printf("DIFF faelle=%ld abweichungen=%ld\n", faelle, fehler);
    printf("DIFF limits_valid   gueltig=%ld ungueltig=%ld\n", lim_zahl[1], lim_zahl[0]);
    printf("DIFF validate_value OK=%ld INVALID=%ld LIMIT=%ld\n", wert_rc[0], wert_rc[1], wert_rc[2]);
    printf("DIFF validate_desc  OK=%ld INVALID=%ld LIMIT=%ld\n", desc_rc[0], desc_rc[1], desc_rc[2]);
    if (runden > 0 && (lim_zahl[0] == 0 || lim_zahl[1] == 0 || wert_rc[0] == 0 || wert_rc[1] == 0 ||
                       wert_rc[2] == 0 || desc_rc[0] == 0 || desc_rc[1] == 0 || desc_rc[2] == 0)) {
        puts("DIFF: nicht alle Resultcodes erreicht"); fehler++;
    }
    if (fehler) { puts("ERGEBNIS: ROT"); return 1; }
    puts("ERGEBNIS: GRUEN (native Validierung identisch zu moo_compositor_animation.c)");
    return 0;
}

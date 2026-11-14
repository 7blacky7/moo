/* P2-T65: Differenztest + Benchmark natives Moo (effekt_mathe_nativ.moos)
 * gegen die Produktions-C-Funktionen aus
 * compiler/runtime/moo_compositor_effects_math.c. Verglichen werden je Aufruf
 * Resultcode UND Ausgabe; bei Fehlern muss die Ausgabe unberuehrt bleiben
 * (Waechterwert). Ganzzahl-only: keine Toleranz, keine Ausnahme. */
#define _POSIX_C_SOURCE 199309L
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "moo_compositor_effects_math.h"

_Static_assert(sizeof(MooCompRect) == 16, "MooCompRect-Groesse");
_Static_assert(offsetof(MooCompRect, x) == 0 && offsetof(MooCompRect, y) == 4 &&
               offsetof(MooCompRect, width) == 8 && offsetof(MooCompRect, height) == 12,
               "MooCompRect-Layout = KompRechteck");
_Static_assert(MOO_COMP_OK == 0 && MOO_COMP_INVALID == 1 && MOO_COMP_LIMIT == 2, "Resultcodes");
_Static_assert(sizeof(MooCompAffine2D) == 32 && offsetof(MooCompAffine2D, m12) == 4 &&
               offsetof(MooCompAffine2D, m21) == 8 && offsetof(MooCompAffine2D, m22) == 12 &&
               offsetof(MooCompAffine2D, tx) == 16 && offsetof(MooCompAffine2D, ty) == 20 &&
               offsetof(MooCompAffine2D, origin_x) == 24 && offsetof(MooCompAffine2D, origin_y) == 28,
               "MooCompAffine2D-Layout = KompAffin");
_Static_assert(sizeof(MooCompEffectPointQ16) == 8 && offsetof(MooCompEffectPointQ16, y) == 4,
               "MooCompEffectPointQ16-Layout = KompPunkt");
_Static_assert(sizeof(MooCompCorners) == 8 && offsetof(MooCompCorners, top_right) == 2 &&
               offsetof(MooCompCorners, bottom_right) == 4 && offsetof(MooCompCorners, bottom_left) == 6,
               "MooCompCorners-Layout = KompEcken");
_Static_assert(sizeof(MooCompShadow) == 16 && offsetof(MooCompShadow, blur_radius) == 8 &&
               offsetof(MooCompShadow, spread_radius) == 10 && offsetof(MooCompShadow, color) == 12,
               "MooCompShadow-Layout = KompSchatten");
_Static_assert(sizeof(MooCompBackdrop) == 16 && offsetof(MooCompBackdrop, tint) == 4 &&
               offsetof(MooCompBackdrop, tint_mix) == 8 && offsetof(MooCompBackdrop, noise) == 9 &&
               offsetof(MooCompBackdrop, reserved) == 10 && offsetof(MooCompBackdrop, noise_seed) == 12,
               "MooCompBackdrop-Layout = KompHintergrund");
_Static_assert(sizeof(MooCompEffectBounds) == 48 && offsetof(MooCompEffectBounds, visual_bounds) == 16 &&
               offsetof(MooCompEffectBounds, backdrop_sample_bounds) == 32,
               "MooCompEffectBounds-Layout = KompGrenzen");
_Static_assert(MOO_COMP_EFFECTS_V2 == 0xFFu && MOO_COMP_EFFECT_SHADOW == 2u && MOO_COMP_EFFECT_BACKDROP_BLUR == 4u,
               "Maskenwerte wie in effekt_mathe_nativ.moos");

/* Natives Moo (@einstieg). Zeiger nimmt Moo als u64-Adresse: gleiche C-ABI. */
uint32_t moo_n_q16_mul(int32_t a, int32_t b, MooCompQ16 *out);
uint32_t moo_n_q16_div(int32_t numerator, int32_t denominator, MooCompQ16 *out);
uint32_t moo_n_rect_union(const MooCompRect *a, const MooCompRect *b, MooCompRect *out);
uint32_t moo_n_rect_intersect(const MooCompRect *a, const MooCompRect *b, MooCompRect *out);
/* API-Differenz (dokumentiert): C nimmt Punkt/Rechteck by value, Moo per Zeiger. */
uint32_t moo_n_affine_transform_point(const MooCompAffine2D *a, const MooCompEffectPointQ16 *p,
                                      MooCompEffectPointQ16 *out);
uint32_t moo_n_affine_inverse(const MooCompAffine2D *a, MooCompAffine2D *out);
uint32_t moo_n_transform_rect_aabb(const MooCompAffine2D *a, const MooCompRect *rect, MooCompRect *out);
uint32_t moo_n_rect_expand(const MooCompRect *rect, int32_t radius, MooCompRect *out);
uint32_t moo_n_corners_normalize(int32_t width, int32_t height, const MooCompCorners *r, MooCompCorners *out);
uint32_t moo_n_rounded_coverage_a8(int32_t width, int32_t height, const MooCompCorners *r,
                                   int32_t x, int32_t y, uint8_t *out);
uint32_t moo_n_compute_bounds(const MooCompRect *local, const MooCompAffine2D *a, uint64_t mask,
                              const MooCompShadow *shadow, const MooCompBackdrop *backdrop,
                              MooCompEffectBounds *out);

static uint32_t rng_state = 0x6512u;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static int32_t wert(void) {
    static const int32_t sonder[] = {
        0, 1, -1, 2, -2, INT32_MAX, INT32_MIN, INT32_MAX - 1, INT32_MIN + 1,
        65536, -65536, 32768, -32768, 32767, -32767, 46340, -46341, 65535,
        1 << 30, -(1 << 30), 0x7fff0000, -0x7fff0000, 3, -3,
    };
    uint32_t r = rng();
    switch (r & 3u) {
    case 0: return sonder[(r >> 2) % (sizeof sonder / sizeof sonder[0])];
    case 1: return (int32_t)(rng() % 2001u) - 1000;            /* klein */
    case 2: return (int32_t)((rng() % 0x1000000u)) - 0x800000; /* Q16 +-128 */
    default: return (int32_t)rng();                           /* beliebig */
    }
}

static MooCompRect rechteck(void) {
    MooCompRect r;
    r.x = wert(); r.y = wert();
    uint32_t s = rng() & 15u;
    r.width = s == 0u ? -(int32_t)(rng() % 5u) - 1 : s == 1u ? 0 : (s < 4u ? wert() : (int32_t)(rng() % 5000u));
    s = rng() & 15u;
    r.height = s == 0u ? -(int32_t)(rng() % 5u) - 1 : s == 1u ? 0 : (s < 4u ? wert() : (int32_t)(rng() % 5000u));
    return r;
}

static long faelle = 0, fehler = 0, rc_zahl[3] = {0, 0, 0};
static void melde(const char *fall, long nr, uint32_t rc_c, uint32_t rc_m) {
    if (fehler < 10) fprintf(stderr, "ABWEICHUNG %s #%ld: rc C=%u Moo=%u\n", fall, nr, rc_c, rc_m);
    fehler++;
}

#define WAECHTER 0x5eadbeef
static const MooCompRect WAECHTER_R = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };

static void q16_fall(const char *fall, int div, int32_t a, int32_t b) {
    MooCompQ16 oc = WAECHTER, om = WAECHTER;
    uint32_t rc = div ? (uint32_t)moo_comp_effect_q16_div(a, b, &oc) : (uint32_t)moo_comp_effect_q16_mul(a, b, &oc);
    uint32_t rm = div ? moo_n_q16_div(a, b, &om) : moo_n_q16_mul(a, b, &om);
    faelle++;
    if (rc < 3u) rc_zahl[rc]++;
    if (rc != rm || oc != om) melde(fall, faelle, rc, rm);
}

static void rect_fall(const char *fall, int inter, MooCompRect a, MooCompRect b) {
    MooCompRect oc = WAECHTER_R, om = WAECHTER_R;
    uint32_t rc = inter ? (uint32_t)moo_comp_effect_rect_intersect(a, b, &oc) : (uint32_t)moo_comp_effect_rect_union(a, b, &oc);
    uint32_t rm = inter ? moo_n_rect_intersect(&a, &b, &om) : moo_n_rect_union(&a, &b, &om);
    faelle++;
    if (rc < 3u) rc_zahl[rc]++;
    if (rc != rm || memcmp(&oc, &om, sizeof oc) != 0) { melde(fall, faelle, rc, rm); return; }
    /* Alias: Ausgabe == erste Eingabe (C bekommt a by value, Moo per Zeiger). */
    MooCompRect ac = a, am = a;
    rc = inter ? (uint32_t)moo_comp_effect_rect_intersect(ac, b, &ac) : (uint32_t)moo_comp_effect_rect_union(ac, b, &ac);
    rm = inter ? moo_n_rect_intersect(&am, &b, &am) : moo_n_rect_union(&am, &b, &am);
    if (rc != rm || memcmp(&ac, &am, sizeof ac) != 0) melde("rect alias out==a", faelle, rc, rm);
}

/* Affine: Koeffizienten meist nahe 1.0 (65536) bzw. Rotation, teils extrem. */
static MooCompQ16 koeff(void) {
    uint32_t s = rng() & 7u;
    if (s < 3u) return 65536 + (int32_t)(rng() % 20001u) - 10000;
    if (s < 5u) return (int32_t)(rng() % 131073u) - 65536;
    if (s == 5u) return 0;
    return wert();
}
static MooCompAffine2D affin(void) {
    MooCompAffine2D a;
    a.m11 = koeff(); a.m12 = koeff(); a.m21 = koeff(); a.m22 = koeff();
    if ((rng() & 15u) == 0u) { a.m12 = 0; a.m21 = 0; a.m22 = 0; } /* det == 0 */
    a.tx = wert(); a.ty = wert(); a.origin_x = wert(); a.origin_y = wert();
    if ((rng() & 1u) == 0u) { a.tx >>= 8; a.ty >>= 8; a.origin_x >>= 8; a.origin_y >>= 8; }
    return a;
}
static const MooCompAffine2D WAECHTER_A = { 0x01010101, 0x02020202, 0x03030303, 0x04040404,
                                            0x05050505, 0x06060606, 0x07070707, 0x08080808 };
static const MooCompEffectPointQ16 WAECHTER_P = { 0x0a0a0a0a, 0x0b0b0b0b };
static long affin_rc[3][3];

static void affin_faelle(void) {
    MooCompAffine2D a = affin();
    MooCompEffectPointQ16 p = { wert(), wert() };
    if ((rng() & 1u) == 0u) { p.x >>= 4; p.y >>= 4; }
    /* transform_point */
    MooCompEffectPointQ16 pc = WAECHTER_P, pm = WAECHTER_P;
    uint32_t rc = (uint32_t)moo_comp_effect_affine_transform_point(&a, p, &pc);
    uint32_t rm = moo_n_affine_transform_point(&a, &p, &pm);
    faelle++; if (rc < 3u) { rc_zahl[rc]++; affin_rc[0][rc]++; }
    if (rc != rm || memcmp(&pc, &pm, sizeof pc) != 0) melde("transform_point", faelle, rc, rm);
    MooCompEffectPointQ16 qa = p;                               /* Alias out == punkt */
    rm = moo_n_affine_transform_point(&a, &qa, &qa);
    MooCompEffectPointQ16 qc = p;
    rc = (uint32_t)moo_comp_effect_affine_transform_point(&a, qc, &qc);
    if (rc != rm || memcmp(&qc, &qa, sizeof qa) != 0) melde("transform_point alias", faelle, rc, rm);
    /* inverse */
    MooCompAffine2D ic = WAECHTER_A, im = WAECHTER_A;
    rc = (uint32_t)moo_comp_effect_affine_inverse(&a, &ic);
    rm = moo_n_affine_inverse(&a, &im);
    faelle++; if (rc < 3u) { rc_zahl[rc]++; affin_rc[1][rc]++; }
    if (rc != rm || memcmp(&ic, &im, sizeof ic) != 0) melde("affine_inverse", faelle, rc, rm);
    MooCompAffine2D ac = a, am = a;                              /* Alias out == affin */
    rc = (uint32_t)moo_comp_effect_affine_inverse(&ac, &ac);
    rm = moo_n_affine_inverse(&am, &am);
    if (rc != rm || memcmp(&ac, &am, sizeof ac) != 0) melde("affine_inverse alias", faelle, rc, rm);
    /* transform_rect_aabb */
    MooCompRect r = rechteck();
    if ((rng() & 1u) == 0u) { r.x >>= 18; r.y >>= 18; }
    MooCompRect rcc = WAECHTER_R, rcm = WAECHTER_R;
    rc = (uint32_t)moo_comp_effect_transform_rect_aabb(&a, r, &rcc);
    rm = moo_n_transform_rect_aabb(&a, &r, &rcm);
    faelle++; if (rc < 3u) { rc_zahl[rc]++; affin_rc[2][rc]++; }
    if (rc != rm || memcmp(&rcc, &rcm, sizeof rcc) != 0) melde("transform_rect_aabb", faelle, rc, rm);
    MooCompRect ra2 = r, rc2 = r;                                /* Alias out == rechteck */
    rm = moo_n_transform_rect_aabb(&a, &ra2, &ra2);
    rc = (uint32_t)moo_comp_effect_transform_rect_aabb(&a, rc2, &rc2);
    if (rc != rm || memcmp(&rc2, &ra2, sizeof ra2) != 0) melde("transform_rect_aabb alias", faelle, rc, rm);
}

/* Stufe 3: rect_expand + corners_normalize. */
static long stufe3_rc[2][3];
static const MooCompCorners WAECHTER_E = { 0x1111, 0x2222, 0x3333, 0x4444 };
static uint16_t eckwert(void) {
    uint32_t s = rng() & 7u;
    if (s == 0u) return 0;
    if (s == 1u) return 65535;
    if (s < 5u) return (uint16_t)(rng() % 200u);
    return (uint16_t)rng();
}
static void stufe3_faelle(void) {
    /* rect_expand */
    MooCompRect r = rechteck();
    int32_t rad;
    uint32_t s = rng() & 7u;
    rad = s == 0u ? -(int32_t)(rng() % 5u) - 1 : s == 1u ? INT32_MAX : s == 2u ? wert() : (int32_t)(rng() % 300u);
    MooCompRect oc = WAECHTER_R, om = WAECHTER_R;
    uint32_t rc = (uint32_t)moo_comp_effect_rect_expand(r, rad, &oc);
    uint32_t rm = moo_n_rect_expand(&r, rad, &om);
    faelle++; if (rc < 3u) { rc_zahl[rc]++; stufe3_rc[0][rc]++; }
    if (rc != rm || memcmp(&oc, &om, sizeof oc) != 0) melde("rect_expand", faelle, rc, rm);
    MooCompRect ac = r, am = r;                                   /* Alias out == rechteck */
    rc = (uint32_t)moo_comp_effect_rect_expand(ac, rad, &ac);
    rm = moo_n_rect_expand(&am, rad, &am);
    if (rc != rm || memcmp(&ac, &am, sizeof ac) != 0) melde("rect_expand alias", faelle, rc, rm);
    /* corners_normalize */
    MooCompCorners e = { eckwert(), eckwert(), eckwert(), eckwert() };
    int32_t w = (rng() & 15u) == 0u ? -(int32_t)(rng() % 5u) - 1 : (rng() & 3u) == 0u ? wert() : (int32_t)(rng() % 1000u);
    int32_t h = (rng() & 15u) == 0u ? -(int32_t)(rng() % 5u) - 1 : (rng() & 3u) == 0u ? wert() : (int32_t)(rng() % 1000u);
    MooCompCorners ec = WAECHTER_E, em = WAECHTER_E;
    rc = (uint32_t)moo_comp_effect_corners_normalize(w, h, &e, &ec);
    rm = moo_n_corners_normalize(w, h, &e, &em);
    faelle++; if (rc < 3u) { rc_zahl[rc]++; stufe3_rc[1][rc]++; }
    if (rc != rm || memcmp(&ec, &em, sizeof ec) != 0) melde("corners_normalize", faelle, rc, rm);
    MooCompCorners xc = e, xm = e;                                /* Alias out == ecken */
    rc = (uint32_t)moo_comp_effect_corners_normalize(w, h, &xc, &xc);
    rm = moo_n_corners_normalize(w, h, &xm, &xm);
    if (rc != rm || memcmp(&xc, &xm, sizeof xc) != 0) melde("corners_normalize alias", faelle, rc, rm);
}

/* Stufe 4: rounded_coverage_a8 + compute_bounds. */
static long stufe4_rc[2][3];
static void stufe4_faelle(void) {
    /* rounded_coverage_a8: Ecken teils normalisiert (realistisch), teils roh. */
    int32_t w = (rng() & 15u) == 0u ? -(int32_t)(rng() % 3u) - 1 : (rng() & 7u) == 0u ? wert() : (int32_t)(rng() % 400u);
    int32_t h = (rng() & 15u) == 0u ? -(int32_t)(rng() % 3u) - 1 : (rng() & 7u) == 0u ? wert() : (int32_t)(rng() % 400u);
    MooCompCorners e = { eckwert(), eckwert(), eckwert(), eckwert() };
    if ((rng() & 1u) == 0u && w >= 0 && h >= 0) { MooCompCorners n; if (moo_comp_effect_corners_normalize(w, h, &e, &n) == MOO_COMP_OK) e = n; }
    int32_t x, y;
    if ((rng() & 7u) == 0u) { x = wert(); y = wert(); }
    else { x = (int32_t)(rng() % 420u) - 10; y = (int32_t)(rng() % 420u) - 10; }
    uint8_t oc = 0x5a, om = 0x5a;
    uint32_t rc = (uint32_t)moo_comp_effect_rounded_coverage_a8(w, h, &e, x, y, &oc);
    uint32_t rm = moo_n_rounded_coverage_a8(w, h, &e, x, y, &om);
    faelle++; if (rc < 3u) { rc_zahl[rc]++; stufe4_rc[0][rc]++; }
    if (rc != rm || oc != om) melde("rounded_coverage_a8", faelle, rc, rm);
    /* compute_bounds */
    MooCompRect lokal = rechteck();
    if ((rng() & 1u) == 0u) { lokal.x >>= 18; lokal.y >>= 18; }
    MooCompAffine2D a = affin();
    uint64_t maske = (uint64_t)(rng() & 0xFFu);
    if ((rng() & 15u) == 0u) maske |= (uint64_t)1 << (8u + (rng() % 56u));     /* unbekanntes Bit */
    MooCompShadow sh;
    memset(&sh, 0, sizeof sh);
    sh.offset_x = (rng() & 7u) == 0u ? wert() : (int32_t)(rng() % 9000u) - 4500;
    sh.offset_y = (rng() & 7u) == 0u ? wert() : (int32_t)(rng() % 9000u) - 4500;
    sh.blur_radius = (rng() & 7u) == 0u ? (uint16_t)rng() : (uint16_t)(rng() % 65u);
    sh.spread_radius = (rng() & 7u) == 0u ? (uint16_t)rng() : (uint16_t)(rng() % 65u);
    sh.color = (MooCompRgba8){ 1, 2, 3, 4 };
    MooCompBackdrop bd;
    memset(&bd, 0, sizeof bd);
    bd.blur_radius = (rng() & 7u) == 0u ? (uint16_t)rng() : (uint16_t)(rng() % 33u);
    bd.noise_seed = rng();
    MooCompEffectBounds bc, bm;
    memset(&bc, 0x6b, sizeof bc); memset(&bm, 0x6b, sizeof bm);
    rc = (uint32_t)moo_comp_effect_compute_bounds(lokal, &a, maske, &sh, &bd, &bc);
    rm = moo_n_compute_bounds(&lokal, &a, maske, &sh, &bd, &bm);
    faelle++; if (rc < 3u) { rc_zahl[rc]++; stufe4_rc[1][rc]++; }
    if (rc != rm || memcmp(&bc, &bm, sizeof bc) != 0) melde("compute_bounds", faelle, rc, rm);
}

static void differenztest(long runden) {
    /* NULL-Ausgabe -> INVALID auf beiden Seiten. */
    MooCompRect r0 = {0, 0, 1, 1};
    if ((uint32_t)moo_comp_effect_q16_mul(1, 1, NULL) != moo_n_q16_mul(1, 1, NULL)) melde("mul NULL", 0, 9, 9);
    if ((uint32_t)moo_comp_effect_q16_div(1, 1, NULL) != moo_n_q16_div(1, 1, NULL)) melde("div NULL", 0, 9, 9);
    if ((uint32_t)moo_comp_effect_rect_union(r0, r0, NULL) != moo_n_rect_union(&r0, &r0, NULL)) melde("union NULL", 0, 9, 9);
    if ((uint32_t)moo_comp_effect_rect_intersect(r0, r0, NULL) != moo_n_rect_intersect(&r0, &r0, NULL)) melde("intersect NULL", 0, 9, 9);
    MooCompAffine2D id = { 65536, 0, 0, 65536, 0, 0, 0, 0 }, ido = WAECHTER_A;
    MooCompEffectPointQ16 p0 = { 0, 0 }, po = WAECHTER_P;
    if ((uint32_t)moo_comp_effect_affine_transform_point(NULL, p0, &po) != moo_n_affine_transform_point(NULL, &p0, &po)) melde("point NULL a", 0, 9, 9);
    if ((uint32_t)moo_comp_effect_affine_transform_point(&id, p0, NULL) != moo_n_affine_transform_point(&id, &p0, NULL)) melde("point NULL out", 0, 9, 9);
    if ((uint32_t)moo_comp_effect_affine_inverse(NULL, &ido) != moo_n_affine_inverse(NULL, &ido)) melde("inverse NULL a", 0, 9, 9);
    if ((uint32_t)moo_comp_effect_affine_inverse(&id, NULL) != moo_n_affine_inverse(&id, NULL)) melde("inverse NULL out", 0, 9, 9);
    if ((uint32_t)moo_comp_effect_transform_rect_aabb(NULL, r0, &r0) != moo_n_transform_rect_aabb(NULL, &r0, &r0)) melde("aabb NULL a", 0, 9, 9);
    if ((uint32_t)moo_comp_effect_transform_rect_aabb(&id, r0, NULL) != moo_n_transform_rect_aabb(&id, &r0, NULL)) melde("aabb NULL out", 0, 9, 9);
    if (memcmp(&po, &WAECHTER_P, sizeof po) != 0 || memcmp(&ido, &WAECHTER_A, sizeof ido) != 0) melde("NULL schrieb Ausgabe", 0, 9, 9);
    MooCompCorners e0 = { 1, 2, 3, 4 }, eo = WAECHTER_E;
    if ((uint32_t)moo_comp_effect_rect_expand(r0, 1, NULL) != moo_n_rect_expand(&r0, 1, NULL)) melde("expand NULL out", 0, 9, 9);
    if ((uint32_t)moo_comp_effect_corners_normalize(10, 10, NULL, &eo) != moo_n_corners_normalize(10, 10, NULL, &eo)) melde("corners NULL r", 0, 9, 9);
    if ((uint32_t)moo_comp_effect_corners_normalize(10, 10, &e0, NULL) != moo_n_corners_normalize(10, 10, &e0, NULL)) melde("corners NULL out", 0, 9, 9);
    if (memcmp(&eo, &WAECHTER_E, sizeof eo) != 0) melde("corners NULL schrieb Ausgabe", 0, 9, 9);
    {
        uint8_t cov = 0x5a;
        MooCompShadow sh0; MooCompBackdrop bd0; MooCompEffectBounds b0;
        memset(&sh0, 0, sizeof sh0); memset(&bd0, 0, sizeof bd0); memset(&b0, 0x6b, sizeof b0);
        if ((uint32_t)moo_comp_effect_rounded_coverage_a8(10, 10, NULL, 1, 1, &cov) != moo_n_rounded_coverage_a8(10, 10, NULL, 1, 1, &cov)) melde("coverage NULL r", 0, 9, 9);
        if ((uint32_t)moo_comp_effect_rounded_coverage_a8(10, 10, &e0, 1, 1, NULL) != moo_n_rounded_coverage_a8(10, 10, &e0, 1, 1, NULL)) melde("coverage NULL out", 0, 9, 9);
        if ((uint32_t)moo_comp_effect_compute_bounds(r0, NULL, 0, &sh0, &bd0, &b0) != moo_n_compute_bounds(&r0, NULL, 0, &sh0, &bd0, &b0)) melde("bounds NULL a", 0, 9, 9);
        if ((uint32_t)moo_comp_effect_compute_bounds(r0, &id, 0, NULL, &bd0, &b0) != moo_n_compute_bounds(&r0, &id, 0, NULL, &bd0, &b0)) melde("bounds NULL shadow", 0, 9, 9);
        if ((uint32_t)moo_comp_effect_compute_bounds(r0, &id, 0, &sh0, NULL, &b0) != moo_n_compute_bounds(&r0, &id, 0, &sh0, NULL, &b0)) melde("bounds NULL backdrop", 0, 9, 9);
        if ((uint32_t)moo_comp_effect_compute_bounds(r0, &id, 0, &sh0, &bd0, NULL) != moo_n_compute_bounds(&r0, &id, 0, &sh0, &bd0, NULL)) melde("bounds NULL out", 0, 9, 9);
        if ((uint32_t)moo_comp_effect_compute_bounds(r0, &id, 0x100u, &sh0, &bd0, &b0) != moo_n_compute_bounds(&r0, &id, 0x100u, &sh0, &bd0, &b0)) melde("bounds maske 0x100", 0, 9, 9);
        if ((uint32_t)moo_comp_effect_compute_bounds(r0, &id, UINT64_C(1) << 63, &sh0, &bd0, &b0) != moo_n_compute_bounds(&r0, &id, UINT64_C(1) << 63, &sh0, &bd0, &b0)) melde("bounds maske bit63", 0, 9, 9);
        MooCompEffectBounds bw; memset(&bw, 0x6b, sizeof bw);
        if (cov != 0x5a || memcmp(&b0, &bw, sizeof b0) != 0) melde("stufe4 NULL schrieb Ausgabe", 0, 9, 9);
    }
    /* Feste Grenzfaelle. */
    static const int32_t g[] = { 0, 1, -1, INT32_MAX, INT32_MIN, 65536, -65536, 32768, -32768, 46341, -46341 };
    for (size_t i = 0; i < sizeof g / sizeof g[0]; i++)
        for (size_t j = 0; j < sizeof g / sizeof g[0]; j++) {
            q16_fall("mul grenze", 0, g[i], g[j]);
            q16_fall("div grenze", 1, g[i], g[j]);
        }
    for (long k = 0; k < runden; k++) {
        q16_fall("mul", 0, wert(), wert());
        q16_fall("div", 1, wert(), (rng() & 31u) == 0u ? 0 : wert());
        rect_fall("union", 0, rechteck(), rechteck());
        rect_fall("intersect", 1, rechteck(), rechteck());
        affin_faelle();
        stufe3_faelle();
        stufe4_faelle();
    }
}

/* ---------------- Benchmark ---------------- */
static double jetzt_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}
static int cmp_double(const void *x, const void *y) {
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}
#define POOL 1024
static int32_t qa[POOL], qb[POOL];
static MooCompRect ra[POOL], rb[POOL];

__attribute__((noinline)) static uint32_t lauf_mul_c(long n) {
    uint32_t s = 0; MooCompQ16 o = 0;
    for (long i = 0; i < n; i++) { s += (uint32_t)moo_comp_effect_q16_mul(qa[i & (POOL - 1)], qb[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)o; }
    return s;
}
__attribute__((noinline)) static uint32_t lauf_mul_m(long n) {
    uint32_t s = 0; MooCompQ16 o = 0;
    for (long i = 0; i < n; i++) { s += moo_n_q16_mul(qa[i & (POOL - 1)], qb[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)o; }
    return s;
}
__attribute__((noinline)) static uint32_t lauf_div_c(long n) {
    uint32_t s = 0; MooCompQ16 o = 0;
    for (long i = 0; i < n; i++) { s += (uint32_t)moo_comp_effect_q16_div(qa[i & (POOL - 1)], qb[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)o; }
    return s;
}
__attribute__((noinline)) static uint32_t lauf_div_m(long n) {
    uint32_t s = 0; MooCompQ16 o = 0;
    for (long i = 0; i < n; i++) { s += moo_n_q16_div(qa[i & (POOL - 1)], qb[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)o; }
    return s;
}
__attribute__((noinline)) static uint32_t lauf_sect_c(long n) {
    uint32_t s = 0; MooCompRect o = {0, 0, 0, 0};
    for (long i = 0; i < n; i++) { s += (uint32_t)moo_comp_effect_rect_intersect(ra[i & (POOL - 1)], rb[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)(o.x ^ o.width); }
    return s;
}
__attribute__((noinline)) static uint32_t lauf_sect_m(long n) {
    uint32_t s = 0; MooCompRect o = {0, 0, 0, 0};
    for (long i = 0; i < n; i++) { s += moo_n_rect_intersect(&ra[i & (POOL - 1)], &rb[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)(o.x ^ o.width); }
    return s;
}

static MooCompAffine2D aff[POOL];
static MooCompEffectPointQ16 pkt[POOL];
__attribute__((noinline)) static uint32_t lauf_pt_c(long n) {
    uint32_t s = 0; MooCompEffectPointQ16 o = {0, 0};
    for (long i = 0; i < n; i++) { s += (uint32_t)moo_comp_effect_affine_transform_point(&aff[i & (POOL - 1)], pkt[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)(o.x ^ o.y); }
    return s;
}
__attribute__((noinline)) static uint32_t lauf_pt_m(long n) {
    uint32_t s = 0; MooCompEffectPointQ16 o = {0, 0};
    for (long i = 0; i < n; i++) { s += moo_n_affine_transform_point(&aff[i & (POOL - 1)], &pkt[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)(o.x ^ o.y); }
    return s;
}
__attribute__((noinline)) static uint32_t lauf_inv_c(long n) {
    uint32_t s = 0; MooCompAffine2D o;
    memset(&o, 0, sizeof o);
    for (long i = 0; i < n; i++) { s += (uint32_t)moo_comp_effect_affine_inverse(&aff[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)(o.m11 ^ o.tx); }
    return s;
}
__attribute__((noinline)) static uint32_t lauf_inv_m(long n) {
    uint32_t s = 0; MooCompAffine2D o;
    memset(&o, 0, sizeof o);
    for (long i = 0; i < n; i++) { s += moo_n_affine_inverse(&aff[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)(o.m11 ^ o.tx); }
    return s;
}
__attribute__((noinline)) static uint32_t lauf_aabb_c(long n) {
    uint32_t s = 0; MooCompRect o = {0, 0, 0, 0};
    for (long i = 0; i < n; i++) { s += (uint32_t)moo_comp_effect_transform_rect_aabb(&aff[i & (POOL - 1)], ra[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)(o.x ^ o.width); }
    return s;
}
__attribute__((noinline)) static uint32_t lauf_aabb_m(long n) {
    uint32_t s = 0; MooCompRect o = {0, 0, 0, 0};
    for (long i = 0; i < n; i++) { s += moo_n_transform_rect_aabb(&aff[i & (POOL - 1)], &ra[i & (POOL - 1)], &o); s = s * 31u + (uint32_t)(o.x ^ o.width); }
    return s;
}

typedef uint32_t (*lauf_fn)(long);
static void bench_paar(const char *name, lauf_fn fc, lauf_fn fm, long n, int laeufe) {
    double tc[16], tm[16];
    uint32_t sc = 0, sm = 0;
    if (laeufe > 16) laeufe = 16;
    fc(n / 4); fm(n / 4);
    for (int r = 0; r < laeufe; r++) {
        double t0 = jetzt_ns(); sc = fc(n); tc[r] = (jetzt_ns() - t0) / (double)n;
        t0 = jetzt_ns(); sm = fm(n); tm[r] = (jetzt_ns() - t0) / (double)n;
    }
    if (sc != sm) { fprintf(stderr, "BENCH %s: Pruefsumme C=%08x Moo=%08x\n", name, sc, sm); fehler++; }
    qsort(tc, (size_t)laeufe, sizeof tc[0], cmp_double);
    qsort(tm, (size_t)laeufe, sizeof tm[0], cmp_double);
    printf("BENCH %-14s n=%ld laeufe=%d median C=%.2f ns Moo=%.2f ns Faktor=%.3f summe=%08x\n",
           name, n, laeufe, tc[laeufe / 2], tm[laeufe / 2], tm[laeufe / 2] / tc[laeufe / 2], sc);
}

int main(int argc, char **argv) {
    long runden = argc > 1 ? atol(argv[1]) : 500000;
    long n = argc > 2 ? atol(argv[2]) : 4000000;
    int laeufe = argc > 3 ? atoi(argv[3]) : 7;
    differenztest(runden);
    printf("DIFF faelle=%ld abweichungen=%ld rc OK=%ld INVALID=%ld LIMIT=%ld\n",
           faelle, fehler, rc_zahl[0], rc_zahl[1], rc_zahl[2]);
    static const char *const affin_name[3] = { "transform_point", "affine_inverse", "rect_aabb" };
    for (int f = 0; f < 3; f++) {
        printf("DIFF %-15s OK=%ld INVALID=%ld LIMIT=%ld\n", affin_name[f], affin_rc[f][0], affin_rc[f][1], affin_rc[f][2]);
        if (affin_rc[f][0] == 0 || affin_rc[f][2] == 0 || (f > 0 && affin_rc[f][1] == 0)) {
            printf("DIFF %s: Resultcode nicht erreicht\n", affin_name[f]); fehler++;
        }
    }
    printf("DIFF rect_expand       OK=%ld INVALID=%ld LIMIT=%ld\n", stufe3_rc[0][0], stufe3_rc[0][1], stufe3_rc[0][2]);
    printf("DIFF corners_normalize OK=%ld INVALID=%ld LIMIT=%ld\n", stufe3_rc[1][0], stufe3_rc[1][1], stufe3_rc[1][2]);
    if (stufe3_rc[0][0] == 0 || stufe3_rc[0][1] == 0 || stufe3_rc[0][2] == 0 ||
        stufe3_rc[1][0] == 0 || stufe3_rc[1][1] == 0) { puts("DIFF Stufe 3: Resultcode nicht erreicht"); fehler++; }
    printf("DIFF rounded_coverage  OK=%ld INVALID=%ld LIMIT=%ld\n", stufe4_rc[0][0], stufe4_rc[0][1], stufe4_rc[0][2]);
    printf("DIFF compute_bounds    OK=%ld INVALID=%ld LIMIT=%ld\n", stufe4_rc[1][0], stufe4_rc[1][1], stufe4_rc[1][2]);
    if (stufe4_rc[0][0] == 0 || stufe4_rc[0][1] == 0 ||
        stufe4_rc[1][0] == 0 || stufe4_rc[1][1] == 0 || stufe4_rc[1][2] == 0) { puts("DIFF Stufe 4: Resultcode nicht erreicht"); fehler++; }
    if (rc_zahl[0] == 0 || rc_zahl[1] == 0 || rc_zahl[2] == 0) { puts("DIFF: nicht alle Resultcodes erreicht"); fehler++; }
    if (n > 0 && laeufe > 0) {
        for (int i = 0; i < POOL; i++) {
            qa[i] = (int32_t)(rng() % 0x1000000u) - 0x800000;
            qb[i] = (int32_t)(rng() % 0x1000000u) - 0x800000;
            if (qb[i] == 0) qb[i] = 65536;
            ra[i] = (MooCompRect){ (int32_t)(rng() % 4000u), (int32_t)(rng() % 4000u), (int32_t)(rng() % 900u), (int32_t)(rng() % 900u) };
            rb[i] = (MooCompRect){ (int32_t)(rng() % 4000u), (int32_t)(rng() % 4000u), (int32_t)(rng() % 900u), (int32_t)(rng() % 900u) };
        }
        bench_paar("q16_mul", lauf_mul_c, lauf_mul_m, n, laeufe);
        bench_paar("q16_div", lauf_div_c, lauf_div_m, n, laeufe);
        bench_paar("rect_intersect", lauf_sect_c, lauf_sect_m, n, laeufe);
        for (int i = 0; i < POOL; i++) {
            int32_t w = (int32_t)(rng() % 65536u) - 32768;              /* kleine Rotation/Scherung */
            aff[i] = (MooCompAffine2D){ 65536 - (w >> 4), w, -w, 65536 - (w >> 4),
                                         (int32_t)(rng() % 0x4000000u), (int32_t)(rng() % 0x4000000u),
                                         (int32_t)(rng() % 0x1000000u), (int32_t)(rng() % 0x1000000u) };
            pkt[i] = (MooCompEffectPointQ16){ (int32_t)(rng() % 0x8000000u), (int32_t)(rng() % 0x8000000u) };
        }
        bench_paar("transform_point", lauf_pt_c, lauf_pt_m, n, laeufe);
        bench_paar("affine_inverse", lauf_inv_c, lauf_inv_m, n, laeufe);
        bench_paar("rect_aabb", lauf_aabb_c, lauf_aabb_m, n, laeufe);
    }
    if (fehler) { puts("ERGEBNIS: ROT"); return 1; }
    puts("ERGEBNIS: GRUEN (natives Moo identisch zu moo_compositor_effects_math.c)");
    return 0;
}

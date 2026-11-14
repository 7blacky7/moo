/* P2-T72: Differenztest natives Moo (effekt_cpu_{pixel,geometrie,blur,render}_nativ.moos
 * + effekt_mathe_nativ.moos) gegen compiler/runtime/moo_compositor_effects_cpu.c
 * (eingebunden; statische Helfer dienen als Orakel). Teil A vergleicht die
 * Einzelfunktionen, Teil B komplette render-Laeufe auf kleinen Zufallsszenen:
 * C und Moo erhalten getrennte Ziel- und Scratch-Puffer (gleicher Startinhalt),
 * verglichen werden Resultcode, Stats und alle Bytes von Ziel und Scratch. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "moo_compositor_effects_cpu.c"

_Static_assert(sizeof(MooCompEffectCpuSource) == 32 && offsetof(MooCompEffectCpuSource, stride) == 16 &&
               offsetof(MooCompEffectCpuSource, width) == 24, "Source-Layout");
_Static_assert(sizeof(MooCompEffectCpuJob) == 232 && offsetof(MooCompEffectCpuJob, lower_z) == 32 &&
               offsetof(MooCompEffectCpuJob, target) == 64 && offsetof(MooCompEffectCpuJob, content_rect) == 96 &&
               offsetof(MooCompEffectCpuJob, effect) == 112 && offsetof(MooCompEffectCpuJob, content_opacity) == 208 &&
               offsetof(MooCompEffectCpuJob, reserved) == 209 && offsetof(MooCompEffectCpuJob, content_scale) == 216 &&
               offsetof(MooCompEffectCpuJob, reserved_scale) == 220 && offsetof(MooCompEffectCpuJob, max_work_units) == 224,
               "Job-Layout = Offsets in effekt_cpu_*_nativ.moos");
_Static_assert(offsetof(MooCompEffectState, shadow) == 32 && offsetof(MooCompEffectState, backdrop) == 48 &&
               offsetof(MooCompShadow, color) == 12 && offsetof(MooCompBackdrop, tint_mix) == 8 &&
               offsetof(MooCompBackdrop, noise_seed) == 12 && offsetof(MooCompEffectState, affine) == 64, "State-Layout");
_Static_assert(sizeof(MooCompEffectCpuStats) == 40 && sizeof(MooCompEffectCpuScratch) == 48 &&
               offsetof(MooCompEffectCpuScratch, rgba_words_per_buffer) == 16, "Stats/Scratch");

uint32_t moo_n_fx_clamp_u8(int64_t v);
uint64_t moo_n_fx_round_u64(uint64_t n, uint64_t d);
int64_t moo_n_fx_round_signed(int64_t n, int64_t d);
uint32_t moo_n_fx_view_valid(const uint8_t *p, size_t bytes, size_t stride, int32_t w, int32_t h);
uint32_t moo_n_fx_ranges_overlap(const void *a, uint64_t ab, const void *b, uint64_t bb);
uint32_t moo_n_fx_rect_valid(const MooCompRect *r);
uint32_t moo_n_fx_effect_valid(const MooCompEffectState *e);
void moo_n_fx_over(uint8_t *dst, const uint8_t *src);
uint32_t moo_n_comp_noise_hash(uint32_t x, uint32_t y, uint32_t seed);
uint32_t moo_n_fx_hash_rgba(const MooCompEffectCpuSource *img, uint64_t *out);
int32_t moo_n_fx_q16_floor(int32_t q);
uint32_t moo_n_fx_local_sample_checked(const MooCompAffine2D *inv, const MooCompRect *r, int32_t x, int32_t y,
                                       int32_t ox, int32_t oy, int32_t *sx, int32_t *sy);
uint32_t moo_n_fx_geometry_preflight(const MooCompEffectCpuJob *job);
uint32_t moo_n_fx_requirements(const MooCompEffectCpuJob *job, MooCompEffectCpuStats *out);
void moo_n_fx_box_mask_h(const uint32_t *s, uint32_t *d, int32_t w, int32_t h, uint32_t r);
void moo_n_fx_box_mask_v(const uint32_t *s, uint32_t *d, int32_t w, int32_t h, uint32_t r);
void moo_n_fx_box_rgba_h(const uint32_t *s, uint32_t *d, int32_t w, int32_t h, uint32_t r);
void moo_n_fx_box_rgba_v(const uint32_t *s, uint32_t *d, int32_t w, int32_t h, uint32_t r);
void moo_n_fx_colorize(const MooCompEffectCpuJob *job, int32_t x, int32_t y, uint8_t *p);
uint32_t moo_n_fx_render(const MooCompEffectCpuJob *job, const MooCompEffectCpuScratch *scratch,
                         MooCompEffectCpuStats *out);

static uint64_t rng_state = 0x6a09e667f3bcc909ull;
static uint64_t rng64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static uint32_t rng(void) { return (uint32_t)(rng64() >> 32); }
static int64_t bereich(int64_t lo, int64_t hi) { return lo + (int64_t)(rng64() % (uint64_t)(hi - lo + 1)); }

static long faelle, fehler, rc_zahl[3], gerendert, mit_schatten, mit_backdrop, mit_affin;

static void melde(const char *was, long fall, uint64_t c, uint64_t m) {
    if (fehler < 20) fprintf(stderr, "ABWEICHUNG %s Fall %ld: C=%llu Moo=%llu\n", was, fall,
                             (unsigned long long)c, (unsigned long long)m);
    fehler++;
}

/* ---------- Teil A: Einzelfunktionen ---------- */
static void einzel_faelle(long runden) {
    static uint32_t s[4096], dc[4096], dm[4096];
    for (long k = 0; k < runden; k++) {
        int64_t v = bereich(-600, 600);
        faelle += 6;
        if ((uint32_t)moo_fx_clamp_u8(v) != moo_n_fx_clamp_u8(v)) melde("clamp_u8", k, 0, 0);
        uint64_t n = rng64() >> (rng() % 64u), d = (rng64() >> (rng() % 63u)) | 1u;
        if (moo_fx_round_u64(n, d) != moo_n_fx_round_u64(n, d)) melde("round_u64", k, n, d);
        int64_t sn = (int64_t)(rng64() >> 2) * ((rng() & 1u) ? 1 : -1);
        int64_t sd = (int64_t)bereich(1, 1000000);
        if (moo_fx_round_signed(sn, sd) != moo_n_fx_round_signed(sn, sd)) melde("round_signed", k, 0, 0);
        uint32_t x = rng(), y = rng(), seed = rng();
        if (moo_comp_effect_cpu_noise_hash(x, y, seed) != moo_n_comp_noise_hash(x, y, seed)) melde("noise_hash", k, x, y);
        int32_t q = (int32_t)rng();
        if (moo_fx_q16_floor(q) != moo_n_fx_q16_floor(q)) melde("q16_floor", k, (uint32_t)q, 0);
        MooCompRect r = { (int32_t)rng(), (int32_t)rng(), (int32_t)bereich(-2, 100), (int32_t)bereich(-2, 100) };
        if (rng() % 2u) { r.x = (int32_t)bereich(-50, 50); r.y = INT32_MAX - (int32_t)bereich(0, 120); }
        if ((uint32_t)moo_fx_rect_valid(r) != moo_n_fx_rect_valid(&r)) melde("rect_valid", k, 0, 0);
        /* over (auch src==dst) */
        uint8_t pc[8], pm[8];
        for (int i = 0; i < 8; i++) pc[i] = pm[i] = (uint8_t)rng();
        if (rng() % 8u == 0u) { pc[3] = pm[3] = 0u; pc[7] = pm[7] = 0u; }
        int alias = rng() % 8u == 0u;
        moo_fx_over(pc, alias ? pc : pc + 4);
        moo_n_fx_over(pm, alias ? pm : pm + 4);
        faelle++;
        if (memcmp(pc, pm, 8) != 0) melde("over", k, pc[0], pm[0]);
        /* ranges_overlap / view_valid */
        uint64_t a = rng() % 4u == 0u ? 0u : (UINT64_MAX - rng64() % 4096u), ab = rng64() % 8192u;
        uint64_t b = rng() % 2u ? a + bereich(-100, 100) : rng64(), bb = rng64() % 8192u;
        faelle += 2;
        if ((uint32_t)moo_fx_ranges_overlap((const void *)(uintptr_t)a, ab, (const void *)(uintptr_t)b, bb) !=
            moo_n_fx_ranges_overlap((const void *)(uintptr_t)a, ab, (const void *)(uintptr_t)b, bb)) melde("ranges_overlap", k, a, b);
        int32_t vw = (int32_t)bereich(-1, 40), vh = (int32_t)bereich(-1, 40);
        size_t stride = (size_t)bereich(0, 200), bytes = (size_t)bereich(0, 9000);
        const uint8_t *vp = rng() % 8u == 0u ? NULL : (const uint8_t *)s;
        if ((uint32_t)moo_fx_view_valid(vp, bytes, stride, vw, vh) != moo_n_fx_view_valid(vp, bytes, stride, vw, vh))
            melde("view_valid", k, (uint64_t)stride, bytes);
        /* Box-Blur Maske/RGBA */
        int32_t w = (int32_t)bereich(1, 16), h = (int32_t)bereich(1, 16);
        uint32_t rad = (uint32_t)bereich(0, 6);
        for (int i = 0; i < w * h * 4; i++) { s[i] = rng() % 66000u; dc[i] = dm[i] = 0xEEEEEEEEu; }
        faelle += 4;
        moo_fx_box_mask_h(s, dc, w, h, rad); moo_n_fx_box_mask_h(s, dm, w, h, rad);
        if (memcmp(dc, dm, sizeof(uint32_t) * (size_t)(w * h * 4)) != 0) melde("box_mask_h", k, (uint64_t)w, rad);
        moo_fx_box_mask_v(s, dc, w, h, rad); moo_n_fx_box_mask_v(s, dm, w, h, rad);
        if (memcmp(dc, dm, sizeof(uint32_t) * (size_t)(w * h * 4)) != 0) melde("box_mask_v", k, (uint64_t)w, rad);
        moo_fx_box_rgba_h(s, dc, w, h, rad); moo_n_fx_box_rgba_h(s, dm, w, h, rad);
        if (memcmp(dc, dm, sizeof(uint32_t) * (size_t)(w * h * 4)) != 0) melde("box_rgba_h", k, (uint64_t)w, rad);
        moo_fx_box_rgba_v(s, dc, w, h, rad); moo_n_fx_box_rgba_v(s, dm, w, h, rad);
        if (memcmp(dc, dm, sizeof(uint32_t) * (size_t)(w * h * 4)) != 0) melde("box_rgba_v", k, (uint64_t)w, rad);
        /* hash_rgba */
        MooCompEffectCpuSource img = { (const uint8_t *)s, (size_t)bereich(0, 16384), (size_t)(w * 4 + bereich(0, 9)), w, h };
        uint64_t hc = 7u, hm = 7u;
        faelle++;
        uint32_t rc = (uint32_t)moo_comp_effect_cpu_hash_rgba(&img, &hc), rm = moo_n_fx_hash_rgba(&img, &hm);
        if (rc != rm || hc != hm) melde("hash_rgba", k, hc, hm);
    }
}

/* ---------- Teil B: komplette Szenen ---------- */
#define TMAX 20
#define PUF 4096
static uint8_t inhalt[PUF], unten[PUF], ziel_c[PUF], ziel_m[PUF];
static uint32_t ping_c[2048], pong_c[2048], ping_m[2048], pong_m[2048];

static void effekt_wuerfeln(MooCompEffectState *e) {
    memset(e, 0, sizeof *e);
    e->backdrop.saturation_q8_8 = MOO_COMP_SATURATION_ONE;
    e->affine.m11 = MOO_COMP_Q16_ONE; e->affine.m22 = MOO_COMP_Q16_ONE;
    uint64_t m = rng() & 0x7Fu;
    if (rng() % 16u == 0u) m |= 0x80u;
    e->enabled_mask = m;
    e->fallback_policy = (uint32_t)bereich(0, 2);
    if (m & 1u) { e->corners.top_left = (uint16_t)bereich(0, 8); e->corners.top_right = (uint16_t)bereich(0, 8);
                  e->corners.bottom_right = (uint16_t)bereich(0, 8); e->corners.bottom_left = (uint16_t)bereich(0, 8); }
    if (m & 2u) { e->shadow.offset_x = (int32_t)bereich(-6, 6); e->shadow.offset_y = (int32_t)bereich(-6, 6);
                  e->shadow.blur_radius = (uint16_t)bereich(0, 4); e->shadow.spread_radius = (uint16_t)bereich(0, 3);
                  e->shadow.color = (MooCompRgba8){ (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng() }; }
    if (m & 4u) e->backdrop.blur_radius = (uint16_t)bereich(0, 3);
    if (m & 8u) e->backdrop.saturation_q8_8 = (uint16_t)bereich(0, 1024);
    if (m & 16u) { e->backdrop.tint = (MooCompRgba8){ (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng() };
                   e->backdrop.tint_mix = (uint8_t)rng(); }
    if (m & 32u) { e->backdrop.noise = (uint8_t)rng(); e->backdrop.noise_seed = rng(); }
    if (m & 64u) {
        e->affine.m11 = (int32_t)bereich(32768, 131072); e->affine.m22 = (int32_t)bereich(32768, 131072);
        e->affine.m12 = (int32_t)bereich(-20000, 20000); e->affine.m21 = (int32_t)bereich(-20000, 20000);
        e->affine.tx = (int32_t)bereich(-655360, 655360); e->affine.ty = (int32_t)bereich(-655360, 655360);
        e->affine.origin_x = (int32_t)bereich(0, 655360); e->affine.origin_y = (int32_t)bereich(0, 655360);
        if (rng() % 16u == 0u) { e->affine.m11 = 0; e->affine.m12 = 0; }   /* singulaer */
    }
    switch (rng() % 24u) {   /* strukturelle Verletzungen */
        case 0: e->reserved = 1u; break;
        case 1: e->fallback_policy = 3u; break;
        case 2: e->corners.top_left = 5000u; break;
        case 3: e->shadow.offset_x = 5000; break;
        default: break;
    }
}

static void szene(MooCompEffectCpuJob *j, MooCompEffectCpuScratch *scc, MooCompEffectCpuScratch *scm) {
    memset(j, 0, sizeof *j);
    int32_t tw = (int32_t)bereich(1, TMAX), th = (int32_t)bereich(1, TMAX);
    size_t tstride = (size_t)(tw * 4) + (size_t)bereich(0, 3) * 4u;
    int32_t skala = (int32_t)bereich(1, 2);
    int32_t rw = (int32_t)bereich(1, 12), rh = (int32_t)bereich(1, 12);
    size_t cstride = (size_t)(rw * skala * 4) + (size_t)bereich(0, 2) * 4u;
    for (int i = 0; i < PUF; i++) { inhalt[i] = (uint8_t)rng(); unten[i] = (uint8_t)rng(); ziel_c[i] = (uint8_t)rng(); }
    memcpy(ziel_m, ziel_c, PUF);
    j->content = (MooCompEffectCpuSource){ inhalt, cstride * (size_t)(rh * skala), cstride, rw * skala, rh * skala };
    j->target = (MooCompEffectCpuTarget){ ziel_c, tstride * (size_t)th, tstride, tw, th };
    if (rng() % 2u) j->lower_z = (MooCompEffectCpuSource){ unten, tstride * (size_t)th, tstride, tw, th };
    j->content_rect = (MooCompRect){ (int32_t)bereich(-4, 14), (int32_t)bereich(-4, 14), rw, rh };
    effekt_wuerfeln(&j->effect);
    j->content_opacity = (uint8_t)rng();
    j->content_scale = (uint32_t)skala;
    j->max_work_units = rng() % 6u == 0u ? (uint64_t)bereich(1, 400) : 0u;
    switch (rng() % 32u) {
        case 0: j->reserved[rng() % 7u] = 1u; break;
        case 1: j->content_scale = 0u; break;
        case 2: j->reserved_scale = 1u; break;
        case 3: j->content.width += 1; break;
        case 4: j->lower_z.width = tw + 1; j->lower_z.pixels = unten; j->lower_z.stride = tstride;
                j->lower_z.height = th; j->lower_z.buffer_bytes = PUF; break;
        default: break;
    }
    for (int i = 0; i < 2048; i++) { ping_c[i] = ping_m[i] = 0x13579BDFu ^ (uint32_t)i; pong_c[i] = pong_m[i] = 0x2468ACE0u ^ (uint32_t)i; }
    uint64_t worte = (uint64_t)bereich(0, 2048);
    if (rng() % 4u != 0u) worte = 2048u;
    *scc = (MooCompEffectCpuScratch){ ping_c, pong_c, worte, NULL, NULL, 0u };
    *scm = (MooCompEffectCpuScratch){ ping_m, pong_m, worte, NULL, NULL, 0u };
}

static void szenen_faelle(long runden) {
    for (long k = 0; k < runden; k++) {
        MooCompEffectCpuJob jc, jm;
        MooCompEffectCpuScratch scc, scm;
        szene(&jc, &scc, &scm);
        jm = jc;
        jm.target.pixels = ziel_m;
        int ohne_scratch = rng() % 16u == 0u;
        /* Einzelpruefungen auf derselben Szene */
        MooCompEffectCpuStats stc, stm;
        memset(&stc, 0x3C, sizeof stc); memset(&stm, 0x3C, sizeof stm);
        uint32_t rc = (uint32_t)moo_comp_effect_cpu_requirements(&jc, &stc);
        uint32_t rm = moo_n_fx_requirements(&jm, &stm);
        faelle++;
        if (rc != rm || memcmp(&stc, &stm, sizeof stc) != 0) { melde("requirements", k, rc, rm); continue; }
        if (moo_fx_effect_valid(&jc.effect) && moo_fx_rect_valid(jc.content_rect)) {
            faelle++;
            if ((uint32_t)moo_fx_geometry_preflight(&jc) != moo_n_fx_geometry_preflight(&jm)) melde("geometry_preflight", k, 0, 0);
            MooCompAffine2D inv = moo_fx_identity();
            int32_t sxc = 0, syc = 0, sxm = 0, sym = 0;
            int32_t px = (int32_t)bereich(-2, 22), py = (int32_t)bereich(-2, 22);
            faelle++;
            uint32_t lc = (uint32_t)moo_fx_local_sample_checked(&inv, jc.content_rect, px, py, jc.effect.shadow.offset_x,
                                                                jc.effect.shadow.offset_y, &sxc, &syc);
            uint32_t lm = moo_n_fx_local_sample_checked(&inv, &jc.content_rect, px, py, jc.effect.shadow.offset_x,
                                                        jc.effect.shadow.offset_y, &sxm, &sym);
            if (lc != lm || sxc != sxm || syc != sym) melde("local_sample_checked", k, lc, lm);
            uint8_t fc[4], fm[4];
            for (int i = 0; i < 4; i++) fc[i] = fm[i] = (uint8_t)rng();
            moo_fx_colorize(&jc, px, py, fc);
            moo_n_fx_colorize(&jm, px, py, fm);
            faelle++;
            if (memcmp(fc, fm, 4) != 0) melde("colorize", k, fc[0], fm[0]);
        }
        /* kompletter render */
        memset(&stc, 0x5D, sizeof stc); memset(&stm, 0x5D, sizeof stm);
        rc = (uint32_t)moo_comp_effect_cpu_render(&jc, ohne_scratch ? NULL : &scc, &stc);
        rm = moo_n_fx_render(&jm, ohne_scratch ? NULL : &scm, &stm);
        faelle++;
        if (rc != rm) { melde("render rc", k, rc, rm); continue; }
        if (rc < 3u) rc_zahl[rc]++;
        if (memcmp(&stc, &stm, sizeof stc) != 0) { melde("render stats", k, stc.work_units, stm.work_units); continue; }
        if (memcmp(ziel_c, ziel_m, PUF) != 0) {
            for (int i = 0; i < PUF; i++)
                if (ziel_c[i] != ziel_m[i]) { melde("render Ziel-Byte", k * 10000 + i, ziel_c[i], ziel_m[i]); break; }
            continue;
        }
        if (memcmp(ping_c, ping_m, sizeof ping_c) != 0 || memcmp(pong_c, pong_m, sizeof pong_c) != 0) {
            melde("render Scratch", k, 0, 0); continue;
        }
        if (rc == 0u) {
            gerendert++;
            if (jc.effect.enabled_mask & 2u) mit_schatten++;
            if (jc.effect.enabled_mask & 0x3Cu) mit_backdrop++;
            if (jc.effect.enabled_mask & 64u) mit_affin++;
        }
    }
}

int main(int argc, char **argv) {
    long einzel = argc > 1 ? atol(argv[1]) : 200000;
    long szenen = argc > 2 ? atol(argv[2]) : 60000;
    einzel_faelle(einzel);
    szenen_faelle(szenen);
    printf("DIFF faelle=%ld abweichungen=%ld\n", faelle, fehler);
    printf("DIFF render OK=%ld INVALID=%ld LIMIT=%ld gerendert=%ld schatten=%ld backdrop=%ld affin=%ld\n",
           rc_zahl[0], rc_zahl[1], rc_zahl[2], gerendert, mit_schatten, mit_backdrop, mit_affin);
    if (szenen > 0 && (rc_zahl[0] == 0 || rc_zahl[1] == 0 || rc_zahl[2] == 0 || mit_schatten == 0 ||
                       mit_backdrop == 0 || mit_affin == 0)) {
        puts("DIFF: nicht alle Pfade erreicht"); fehler++;
    }
    if (fehler) { puts("ERGEBNIS: ROT"); return 1; }
    puts("ERGEBNIS: GRUEN (nativer CPU-Effekt-Unterbau identisch zu moo_compositor_effects_cpu.c)");
    return 0;
}

/* P2-T70: Differenztest natives Moo (effekt_aufloesung_nativ.moos +
 * effekt_zustand_nativ.moos) gegen compiler/runtime/moo_compositor_effects_state.c
 * (eingebunden, damit disable/resolve_one/resolve/access als Orakel dienen).
 * Verglichen werden Resultcode und ALLE beruehrten Bytes (Zustand, Ausgabe,
 * degraded), mit Waechterwerten und Alias-Faellen. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "moo_compositor_effects_state.c"

_Static_assert(sizeof(MooCompEffectState) == 96, "State 96");
_Static_assert(sizeof(MooCompEffectSurfaceState) == 344 &&
               offsetof(MooCompEffectSurfaceState, owner) == 8 &&
               offsetof(MooCompEffectSurfaceState, surface) == 16 &&
               offsetof(MooCompEffectSurfaceState, commit_sequence) == 24 &&
               offsetof(MooCompEffectSurfaceState, base) == 32 &&
               offsetof(MooCompEffectSurfaceState, requested) == 128 &&
               offsetof(MooCompEffectSurfaceState, effective) == 224 &&
               offsetof(MooCompEffectSurfaceState, degraded_mask) == 320 &&
               offsetof(MooCompEffectSurfaceState, scratch_bytes) == 336, "SurfaceState-Layout");
_Static_assert(sizeof(MooCompEffectPreflight) == 344 &&
               offsetof(MooCompEffectPreflight, expected_commit_sequence) == 16 &&
               offsetof(MooCompEffectPreflight, next_commit_sequence) == 24 &&
               offsetof(MooCompEffectPreflight, base) == 32 &&
               offsetof(MooCompEffectPreflight, requested) == 128 &&
               offsetof(MooCompEffectPreflight, effective) == 224 &&
               offsetof(MooCompEffectPreflight, degraded_mask) == 320 &&
               offsetof(MooCompEffectPreflight, work_units) == 328, "Preflight-Layout");
_Static_assert(sizeof(MooCompEffectUsage) == 16 && offsetof(MooCompEffectUsage, affected_pixels) == 8, "Usage");
_Static_assert(sizeof(MooCompEffectStatus) == 200 && offsetof(MooCompEffectStatus, effective) == 96 &&
               offsetof(MooCompEffectStatus, degraded_mask) == 192, "Status");
_Static_assert(MOO_COMP_INVALID == 1 && MOO_COMP_LIMIT == 2 && MOO_COMP_STALE_HANDLE == 3 &&
               MOO_COMP_ACCESS == 4 && MOO_COMP_BAD_STATE == 6 && MOO_COMP_UNSUPPORTED == 9, "Resultcodes");
_Static_assert(MOO_COMP_EFFECT_FALLBACK_REQUIRE == 0 && MOO_COMP_EFFECT_FALLBACK_ALLOW_DISABLE == 1 &&
               MOO_COMP_EFFECT_FALLBACK_ALLOW_APPROXIMATE == 2 && MOO_COMP_EFFECT_ANIMATION == 128u, "Politik/Bits");

void moo_n_effect_disable(MooCompEffectState *s, uint64_t bit);
uint32_t moo_n_effect_resolve_one(MooCompEffectState *s, uint64_t required, uint32_t policy, uint64_t bit,
                                  uint32_t unavailable, uint32_t over, uint64_t *degraded);
uint32_t moo_n_effect_resolve(const MooCompEffectStateConfig *c, const MooCompEffectState *req,
                              MooCompEffectState *out, uint64_t *degraded);
uint32_t moo_n_effect_access(const MooCompEffectSurfaceState *s, uint64_t owner, uint64_t surface);
uint32_t moo_n_effect_surface_init(MooCompEffectSurfaceState *s, uint64_t owner, uint64_t surface);
uint32_t moo_n_effect_surface_destroy(MooCompEffectSurfaceState *s, uint64_t owner, uint64_t surface);
uint32_t moo_n_effect_state_preflight(const MooCompEffectStateConfig *c, const MooCompEffectSurfaceState *s,
                                      uint64_t owner, uint64_t surface, const MooCompEffectState *req,
                                      const MooCompEffectUsage *u, MooCompEffectPreflight *out);
uint32_t moo_n_effect_state_apply(MooCompEffectSurfaceState *s, const MooCompEffectPreflight *p);
uint32_t moo_n_effect_surface_status(const MooCompEffectSurfaceState *s, MooCompEffectStatus *out);
uint64_t moo_n_effect_state_hash(const MooCompEffectState *s);
uint64_t moo_n_effect_surface_hash(const MooCompEffectSurfaceState *s);

static uint64_t rng_state = 0x853c49e6748fea9bull;
static uint64_t rng64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static uint32_t rng(void) { return (uint32_t)(rng64() >> 32); }
static uint64_t bereich(uint64_t lo, uint64_t hi) { return lo + rng64() % (hi - lo + 1u); }

static long faelle, fehler;
static long rc_zahl[10], degradiert_faelle, alias_faelle, apply_ok, hash_faelle;

static void melde(const char *was, long fall, uint64_t c, uint64_t m) {
    if (fehler < 20) fprintf(stderr, "ABWEICHUNG %s Fall %ld: C=%llu Moo=%llu\n", was, fall,
                             (unsigned long long)c, (unsigned long long)m);
    fehler++;
}
static void zaehle(uint32_t rc) { if (rc < 10u) rc_zahl[rc]++; }

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
        l->max_effect_work_units_per_frame = bereich(1, 268435456u);
        l->max_effect_scratch_bytes = bereich(1, 67108864u);
    }
    if (rng() % 16u == 0u) l->reserved = 1u;            /* ungueltig fuer preflight */
}

static int32_t i32_um(int32_t g) {
    uint32_t a = rng() % 6u;
    if (a == 0u) return (int32_t)rng();
    if (a == 1u) return g;
    if (a == 2u && g < INT32_MAX) return g + 1;
    return (int32_t)((int64_t)(rng64() % ((uint64_t)g + 1u)) * ((rng() & 1u) ? 1 : -1));
}

/* Gueltige Zustaende mit Werten um die Limits; selten strukturell ungueltig. */
static void zustand_wuerfeln(MooCompEffectState *s, const MooCompEffectLimits *l) {
    *s = moo_comp_effect_state_neutral();
    uint64_t m = rng() % 4u == 0u ? 0xFFu : (rng() & 0xFFu);
    s->enabled_mask = m;
    s->required_mask = rng() % 3u == 0u ? (m & rng()) : 0u;
    s->fallback_policy = rng() % 3u;
    if (m & 1u) {
        s->corners.top_left = (uint16_t)bereich(0, (uint64_t)l->max_corner_radius + 1u);
        s->corners.bottom_left = (uint16_t)bereich(0, l->max_corner_radius);
    }
    if (m & 2u) {
        s->shadow.offset_x = i32_um(l->max_abs_shadow_offset);
        s->shadow.offset_y = i32_um(l->max_abs_shadow_offset);
        s->shadow.blur_radius = (uint16_t)bereich(0, (uint64_t)l->max_shadow_blur_radius + 1u);
        s->shadow.spread_radius = (uint16_t)bereich(0, l->max_shadow_spread_radius);
        s->shadow.color = (MooCompRgba8){ (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng(), (uint8_t)rng() };
    }
    if (m & 4u) s->backdrop.blur_radius = (uint16_t)bereich(0, (uint64_t)l->max_backdrop_blur_radius + 1u);
    if (m & 8u) s->backdrop.saturation_q8_8 = (uint16_t)bereich(0, (uint64_t)l->max_saturation_q8_8 + 1u);
    if (m & 16u) { s->backdrop.tint = (MooCompRgba8){ (uint8_t)rng(), 1u, 2u, (uint8_t)rng() }; s->backdrop.tint_mix = (uint8_t)rng(); }
    if (m & 32u) { s->backdrop.noise = (uint8_t)rng(); s->backdrop.noise_seed = rng(); }
    if (m & 64u) {
        s->affine.m11 = i32_um(l->max_abs_affine_coefficient);
        s->affine.m22 = i32_um(l->max_abs_affine_coefficient);
        s->affine.tx = i32_um(l->max_abs_affine_translation);
        s->affine.origin_y = i32_um(l->max_abs_affine_origin);
    }
    uint32_t art = rng() % 32u;
    if (art == 0u) s->fallback_policy = 3u + rng() % 4u;        /* resolve: unbekannte Politik -> disable */
    else if (art == 1u) s->reserved = 1u;                        /* state_valid 0 -> preflight INVALID */
    else if (art == 2u) s->required_mask = 0xFFu;
}

static void config_wuerfeln(MooCompEffectStateConfig *c) {
    memset(c, 0, sizeof *c);
    c->capabilities = rng() % 3u == 0u ? 0xFFu : (rng() & 0xFFu);
    limits_wuerfeln(&c->limits);
}

static void disable_faelle(long runden) {
    MooCompEffectLimits l = moo_comp_effect_limits_default();
    for (long k = 0; k < runden; k++) {
        MooCompEffectState a, b;
        zustand_wuerfeln(&a, &l);
        a.required_mask |= rng() & 0xFFu;
        b = a;
        uint64_t bit = rng() % 8u == 0u ? (rng() % 2u ? 0u : rng64()) : (uint64_t)1u << (rng() % 9u);
        moo_comp_effect_disable(&a, bit);
        moo_n_effect_disable(&b, bit);
        faelle++;
        if (memcmp(&a, &b, sizeof a) != 0) melde("disable", k, bit, 0);
    }
}

static void resolve_one_faelle(long runden) {
    MooCompEffectLimits l = moo_comp_effect_limits_default();
    for (long k = 0; k < runden; k++) {
        MooCompEffectState a, b;
        zustand_wuerfeln(&a, &l);
        b = a;
        uint64_t req = rng() & 0xFFu, bit = (uint64_t)1u << (rng() % 8u);
        uint32_t pol = rng() % 5u, nv = rng() % 2u, ue = rng() % 2u;
        uint64_t dc = rng64() & 0xF0F0u, dm = dc;
        uint32_t rc = (uint32_t)moo_comp_effect_resolve_one(&a, req, pol, bit, (int)nv, (int)ue, &dc);
        uint32_t rm = moo_n_effect_resolve_one(&b, req, pol, bit, nv, ue, &dm);
        faelle++;
        if (rc != rm || dc != dm || memcmp(&a, &b, sizeof a) != 0) { melde("resolve_one", k, rc, rm); continue; }
        zaehle(rc);
    }
}

static void resolve_faelle(long runden) {
    for (long k = 0; k < runden; k++) {
        MooCompEffectStateConfig cfg;
        config_wuerfeln(&cfg);
        cfg.limits.reserved = 0u;
        MooCompEffectState req, oc, om, rc_req, rm_req;
        zustand_wuerfeln(&req, &cfg.limits);
        memset(&oc, 0x5A, sizeof oc); memset(&om, 0x5A, sizeof om);
        uint64_t dc = 0xD00Du, dm = 0xD00Du;
        int alias = rng() % 6u == 0u;
        uint32_t rc, rm;
        if (alias) {
            rc_req = req; rm_req = req; alias_faelle++;
            rc = (uint32_t)moo_comp_effect_resolve(&cfg, &rc_req, &rc_req, &dc);
            rm = moo_n_effect_resolve(&cfg, &rm_req, &rm_req, &dm);
            faelle++;
            if (rc != rm || dc != dm || memcmp(&rc_req, &rm_req, sizeof rc_req) != 0) melde("resolve alias", k, rc, rm);
            continue;
        }
        rc = (uint32_t)moo_comp_effect_resolve(&cfg, &req, &oc, &dc);
        rm = moo_n_effect_resolve(&cfg, &req, &om, &dm);
        faelle++;
        if (rc != rm || dc != dm || memcmp(&oc, &om, sizeof oc) != 0) { melde("resolve", k, rc, rm); continue; }
        zaehle(rc);
        if (rc == 0u && dc != 0u) degradiert_faelle++;
    }
}

static void flaechen_faelle(long runden) {
    static MooCompEffectSurfaceState sc, sm;
    for (long k = 0; k < runden; k++) {
        uint64_t owner = rng() % 8u == 0u ? 0u : (rng64() | 1u);
        uint64_t surface = rng() % 8u == 0u ? 0u : (rng64() | 1u);
        memset(&sc, 0x77, sizeof sc); memset(&sm, 0x77, sizeof sm);
        uint32_t rc = (uint32_t)moo_comp_effect_surface_init(&sc, owner, surface);
        uint32_t rm = moo_n_effect_surface_init(&sm, owner, surface);
        faelle++;
        if (rc != rm || memcmp(&sc, &sm, sizeof sc) != 0) { melde("surface_init", k, rc, rm); continue; }
        zaehle(rc);
        if (rc != 0u) continue;
        /* Zustand zufaellig stoeren, dann access/status/destroy vergleichen */
        switch (rng() % 6u) {
            case 0: sc.active = sm.active = rng() % 3u; break;
            case 1: sc.reserved = sm.reserved = 1u; break;
            default: break;
        }
        sc.degraded_mask = sm.degraded_mask = rng64();
        uint64_t ao = rng() % 4u == 0u ? owner ^ 2u : owner, as = rng() % 4u == 0u ? surface ^ 4u : surface;
        if (rng() % 16u == 0u) ao = 0u;
        rc = (uint32_t)moo_comp_effect_access(&sc, ao, as);
        rm = moo_n_effect_access(&sm, ao, as);
        faelle++;
        if (rc != rm) { melde("access", k, rc, rm); continue; }
        zaehle(rc);
        MooCompEffectStatus xc, xm;
        memset(&xc, 0x33, sizeof xc); memset(&xm, 0x33, sizeof xm);
        rc = (uint32_t)moo_comp_effect_surface_status(&sc, &xc);
        rm = moo_n_effect_surface_status(&sm, &xm);
        faelle++;
        if (rc != rm || memcmp(&xc, &xm, sizeof xc) != 0) { melde("surface_status", k, rc, rm); continue; }
        rc = (uint32_t)moo_comp_effect_surface_destroy(&sc, ao, as);
        rm = moo_n_effect_surface_destroy(&sm, ao, as);
        faelle++;
        if (rc != rm || memcmp(&sc, &sm, sizeof sc) != 0) melde("surface_destroy", k, rc, rm);
    }
    faelle += 3;
    if ((uint32_t)moo_comp_effect_surface_init(NULL, 1u, 1u) != moo_n_effect_surface_init(NULL, 1u, 1u)) melde("init NULL", -1, 0, 0);
    if ((uint32_t)moo_comp_effect_access(NULL, 1u, 1u) != moo_n_effect_access(NULL, 1u, 1u)) melde("access NULL", -2, 0, 0);
    if ((uint32_t)moo_comp_effect_surface_status(NULL, NULL) != moo_n_effect_surface_status(NULL, NULL)) melde("status NULL", -3, 0, 0);
}

static void preflight_apply_faelle(long runden) {
    static MooCompEffectSurfaceState sc, sm;
    for (long k = 0; k < runden; k++) {
        MooCompEffectStateConfig cfg;
        config_wuerfeln(&cfg);
        if (rng() % 16u == 0u) cfg.capabilities |= 0x100u;
        uint64_t owner = rng64() | 1u, surface = rng64() | 1u;
        (void)moo_comp_effect_surface_init(&sc, owner, surface);
        uint64_t seq = rng() % 16u == 0u ? UINT64_MAX : rng64() >> (rng() % 64u);
        sc.commit_sequence = seq;
        sm = sc;
        MooCompEffectState req;
        zustand_wuerfeln(&req, &cfg.limits);
        MooCompEffectUsage use = { (uint32_t)bereich(0, 10), (uint32_t)bereich(0, 70),
                                   rng() % 4u == 0u ? rng64() : bereich(0, 8294400u) };
        uint64_t po = rng() % 16u == 0u ? owner ^ 8u : owner, ps = surface;
        MooCompEffectPreflight pc, pm;
        memset(&pc, 0x44, sizeof pc); memset(&pm, 0x44, sizeof pm);
        uint32_t rc = (uint32_t)moo_comp_effect_state_preflight(&cfg, &sc, po, ps, &req, &use, &pc);
        uint32_t rm = moo_n_effect_state_preflight(&cfg, &sm, po, ps, &req, &use, &pm);
        faelle++;
        if (rc != rm || memcmp(&pc, &pm, sizeof pc) != 0) { melde("preflight", k, rc, rm); continue; }
        zaehle(rc);
        if (rc != 0u) continue;
        /* apply: gueltig oder gezielt gestoert */
        switch (rng() % 8u) {
            case 0: pc.owner ^= 2u; break;
            case 1: pc.expected_commit_sequence = UINT64_MAX; break;
            case 2: pc.next_commit_sequence += 1u; break;
            case 3: sc.commit_sequence += 1u; break;
            case 4: sc.active = 0u; break;
            default: break;
        }
        pm = pc; sm.commit_sequence = sc.commit_sequence; sm.active = sc.active;
        rc = (uint32_t)moo_comp_effect_state_apply(&sc, &pc);
        rm = moo_n_effect_state_apply(&sm, &pm);
        faelle++;
        if (rc != rm || memcmp(&sc, &sm, sizeof sc) != 0) { melde("apply", k, rc, rm); continue; }
        zaehle(rc);
        if (rc == 0u) apply_ok++;
    }
    MooCompEffectStateConfig cfg; config_wuerfeln(&cfg);
    MooCompEffectState req = moo_comp_effect_state_neutral();
    MooCompEffectUsage use = { 0u, 0u, 1u };
    MooCompEffectPreflight p;
    faelle += 3;
    if ((uint32_t)moo_comp_effect_state_preflight(&cfg, NULL, 1u, 1u, &req, &use, &p) !=
        moo_n_effect_state_preflight(&cfg, NULL, 1u, 1u, &req, &use, &p)) melde("preflight NULL state", -1, 0, 0);
    if ((uint32_t)moo_comp_effect_state_preflight(NULL, NULL, 1u, 1u, &req, &use, &p) !=
        moo_n_effect_state_preflight(NULL, NULL, 1u, 1u, &req, &use, &p)) melde("preflight NULL cfg", -2, 0, 0);
    if ((uint32_t)moo_comp_effect_state_apply(NULL, &p) != moo_n_effect_state_apply(NULL, &p)) melde("apply NULL", -3, 0, 0);
}

/* Hashes: zufaellige Bytes (auch nicht-neutrale/ungueltige Zustaende), reservierte
 * Felder muessen auf BEIDEN Seiten ohne Einfluss bleiben. */
static void hash_faelle_lauf(long runden) {
    static MooCompEffectSurfaceState s;
    for (long k = 0; k < runden; k++) {
        unsigned char *b = (unsigned char *)&s;
        for (size_t i = 0; i < sizeof s; i++) b[i] = (unsigned char)rng();
        uint64_t hc = moo_comp_effect_surface_hash(&s), hm = moo_n_effect_surface_hash(&s);
        uint64_t sc = moo_comp_effect_state_hash(&s.requested), sm = moo_n_effect_state_hash(&s.requested);
        faelle += 2; hash_faelle += 2;
        if (hc != hm) { melde("surface_hash", k, hc, hm); continue; }
        if (sc != sm) { melde("state_hash", k, sc, sm); continue; }
        s.reserved ^= rng() | 1u;
        s.base.reserved ^= rng() | 1u;
        s.effective.backdrop.reserved ^= (uint16_t)(rng() | 1u);
        faelle++;
        if (moo_n_effect_surface_hash(&s) != hm || moo_comp_effect_surface_hash(&s) != hc)
            melde("surface_hash reserviert", k, moo_comp_effect_surface_hash(&s), moo_n_effect_surface_hash(&s));
    }
    faelle += 2;
    if (moo_comp_effect_state_hash(NULL) != moo_n_effect_state_hash(NULL)) melde("state_hash NULL", -1, 0, 0);
    if (moo_comp_effect_surface_hash(NULL) != moo_n_effect_surface_hash(NULL)) melde("surface_hash NULL", -2, 0, 0);
}

int main(int argc, char **argv) {
    long runden = argc > 1 ? atol(argv[1]) : 200000;
    hash_faelle_lauf(runden);
    disable_faelle(runden);
    resolve_one_faelle(runden);
    resolve_faelle(runden);
    flaechen_faelle(runden);
    preflight_apply_faelle(runden);
    printf("DIFF faelle=%ld abweichungen=%ld\n", faelle, fehler);
    printf("DIFF rc OK=%ld INVALID=%ld LIMIT=%ld STALE=%ld ACCESS=%ld BAD_STATE=%ld UNSUPPORTED=%ld\n",
           rc_zahl[0], rc_zahl[1], rc_zahl[2], rc_zahl[3], rc_zahl[4], rc_zahl[6], rc_zahl[9]);
    printf("DIFF resolve degradiert=%ld alias=%ld apply_ok=%ld hash=%ld\n", degradiert_faelle, alias_faelle, apply_ok, hash_faelle);
    if (runden > 0 && (rc_zahl[0] == 0 || rc_zahl[1] == 0 || rc_zahl[2] == 0 || rc_zahl[3] == 0 ||
                       rc_zahl[4] == 0 || rc_zahl[6] == 0 || rc_zahl[9] == 0 || degradiert_faelle == 0 ||
                       alias_faelle == 0 || apply_ok == 0)) {
        puts("DIFF: nicht alle Pfade erreicht"); fehler++;
    }
    if (fehler) { puts("ERGEBNIS: ROT"); return 1; }
    puts("ERGEBNIS: GRUEN (natives Moo identisch zu moo_compositor_effects_state.c)");
    return 0;
}

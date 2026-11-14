/* P2-T65 Damage-Paket: Differenztest + Benchmark natives Moo
 * (effekt_damage_nativ.moos, rect_* aus effekt_mathe_nativ.moos) gegen
 * compiler/runtime/moo_compositor_effects_damage.c. Verglichen werden je Fall
 * Resultcode, Ausgabe-Datensatz, der GESAMTE Regionenspeicher von Workspace
 * und Ausgabe (inkl. Waechterwerten hinter der Kapazitaet) sowie der Hash.
 * Workspace und Ausgabe liegen pro Seite im selben Puffer mit gleichen
 * Relativ-Offsets, damit auch der Overlap-Pfad identisch geprueft wird. */
#define _POSIX_C_SOURCE 199309L
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "moo_compositor_effects_damage.h"

_Static_assert(sizeof(MooCompRect) == 16, "MooCompRect-Groesse");
_Static_assert(MOO_COMP_OK == 0 && MOO_COMP_INVALID == 1 && MOO_COMP_LIMIT == 2, "Resultcodes");
_Static_assert(MOO_COMP_EFFECTS_V2 == 0xFFu && MOO_COMP_EFFECT_BACKDROP_BLUR == 4u,
               "Maskenwerte wie in effekt_damage_nativ.moos");
_Static_assert(sizeof(MooCompEffectDamageSurface) == 80 &&
               offsetof(MooCompEffectDamageSurface, enabled_mask) == 8 &&
               offsetof(MooCompEffectDamageSurface, old_visual_bounds) == 16 &&
               offsetof(MooCompEffectDamageSurface, new_visual_bounds) == 32 &&
               offsetof(MooCompEffectDamageSurface, backdrop_coverage_bounds) == 48 &&
               offsetof(MooCompEffectDamageSurface, backdrop_blur_radius) == 64 &&
               offsetof(MooCompEffectDamageSurface, reserved16) == 66 &&
               offsetof(MooCompEffectDamageSurface, changed) == 68 &&
               offsetof(MooCompEffectDamageSurface, reserved) == 72,
               "MooCompEffectDamageSurface-Layout = KompDamageFlaeche");
_Static_assert(sizeof(MooCompEffectDamageWorkspace) == 16 &&
               offsetof(MooCompEffectDamageWorkspace, capacity) == 8 &&
               offsetof(MooCompEffectDamageWorkspace, reserved) == 12,
               "MooCompEffectDamageWorkspace-Layout = KompDamageArbeit");
_Static_assert(sizeof(MooCompEffectDamageOutput) == 24 &&
               offsetof(MooCompEffectDamageOutput, capacity) == 8 &&
               offsetof(MooCompEffectDamageOutput, count) == 12 &&
               offsetof(MooCompEffectDamageOutput, full_damage) == 16 &&
               offsetof(MooCompEffectDamageOutput, reserved) == 20,
               "MooCompEffectDamageOutput-Layout = KompDamageAusgabe");

/* Natives Moo (@einstieg). API-Differenz (dokumentiert): Rechtecke per Zeiger,
 * kernel_radius als uint32_t (Werte > 65535 -> INVALID). */
uint32_t moo_n_damage_propagate_backdrop(const MooCompRect *dirty, const MooCompRect *coverage,
                                         uint32_t kernel_radius, MooCompRect *out);
uint32_t moo_n_damage_build(const MooCompRect *full_output,
                            const MooCompRect *background, uint32_t background_count,
                            const MooCompEffectDamageSurface *surfaces, uint32_t surface_count,
                            MooCompEffectDamageWorkspace *workspace, MooCompEffectDamageOutput *out);
uint64_t moo_n_damage_hash(const MooCompEffectDamageOutput *damage);

static uint32_t rng_state = 0xda3a6u;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static long faelle, fehler;
static long rc_build[3], rc_prop[3], kollaps, ueberlapp, hash_null;

static void melde(const char *was, long fall, uint32_t rc_c, uint32_t rc_m) {
    if (fehler < 20)
        fprintf(stderr, "ABWEICHUNG %s Fall %ld: rc C=%u Moo=%u\n", was, fall, rc_c, rc_m);
    fehler++;
}

/* Kleine Szenen-Koordinaten, gelegentlich ungueltig (negativ) oder extrem
 * (LIMIT beim Erweitern). */
static MooCompRect rechteck(void) {
    uint32_t art = rng() % 64u;
    MooCompRect r;
    r.x = (int32_t)(rng() % 300u) - 50;
    r.y = (int32_t)(rng() % 300u) - 50;
    r.width = (int32_t)(rng() % 120u);
    r.height = (int32_t)(rng() % 120u);
    if (art == 0u) r.width = -1 - (int32_t)(rng() % 5u);
    else if (art == 1u) r.height = -1;
    else if (art == 2u) { r.x = INT32_MAX - (int32_t)(rng() % 64u); r.width = (int32_t)(rng() % 64u); }
    else if (art == 3u) { r.y = INT32_MIN + (int32_t)(rng() % 16u); }
    else if (art == 4u) r.width = 0;
    else if (art == 5u) { r.x = INT32_MAX; r.width = INT32_MAX; }
    return r;
}

static uint16_t radius(void) {
    uint32_t art = rng() % 32u;
    if (art == 0u) return UINT16_MAX;
    if (art == 1u) return 0u;
    return (uint16_t)(rng() % 40u);
}

static void flaeche(MooCompEffectDamageSurface *s) {
    uint32_t art = rng() % 96u;
    memset(s, 0, sizeof *s);
    s->surface = ((uint64_t)(rng() % 7u + 1u) << 32) | (rng() | 1u);
    s->enabled_mask = rng() & 0xFFu;
    s->old_visual_bounds = rechteck();
    s->new_visual_bounds = rechteck();
    s->backdrop_coverage_bounds = rechteck();
    if (s->enabled_mask & MOO_COMP_EFFECT_BACKDROP_BLUR) s->backdrop_blur_radius = radius();
    s->changed = rng() & 1u;
    if (art == 0u) s->surface = MOO_COMP_HANDLE_INVALID;
    else if (art == 1u) s->reserved16 = 1u;
    else if (art == 2u) s->reserved = 0x80000000u;
    else if (art == 3u) s->changed = 2u;
    else if (art == 4u) s->enabled_mask |= (uint64_t)1u << (8u + rng() % 56u);
    else if (art == 5u) { s->enabled_mask &= ~(uint64_t)MOO_COMP_EFFECT_BACKDROP_BLUR; s->backdrop_blur_radius = 3u; }
}

#define PUFFER 160
#define MAXHINTER 8
#define MAXFL 8

/* Eine Seite (C oder Moo): eigener Regionenpuffer, eigener Workspace/Ausgabe. */
typedef struct {
    MooCompRect puffer[PUFFER];
    MooCompEffectDamageWorkspace ws;
    MooCompEffectDamageOutput out;
} Seite;

typedef struct {
    MooCompRect voll;
    MooCompRect hinter[MAXHINTER];
    uint32_t hinter_zahl;
    int hinter_null;
    MooCompEffectDamageSurface fl[MAXFL];
    uint32_t fl_zahl;
    int fl_null;
    uint32_t ws_kap, out_kap, ws_res, out_res;
    uint32_t out_off;          /* Offset der Ausgabe im Puffer (Overlap-Faelle) */
    int ws_null, out_null, ws_reg_null, out_reg_null;
} Szene;

static void seite_bauen(Seite *s, const Szene *z) {
    for (int i = 0; i < PUFFER; i++)
        s->puffer[i] = (MooCompRect){ (int32_t)(0x5A5A0000u + (uint32_t)i), -7, 0x11, 0x22 };
    s->ws.regions = z->ws_reg_null ? NULL : s->puffer;
    s->ws.capacity = z->ws_kap;
    s->ws.reserved = z->ws_res;
    s->out.regions = z->out_reg_null ? NULL : s->puffer + z->out_off;
    s->out.capacity = z->out_kap;
    s->out.count = 0xC0FFEEu;
    s->out.full_damage = 0xBEEFu;
    s->out.reserved = z->out_res;
}

static void szene_wuerfeln(Szene *z) {
    uint32_t art = rng() % 64u;
    memset(z, 0, sizeof *z);
    z->voll = (MooCompRect){ 0, 0, (int32_t)(rng() % 240u + 1u), (int32_t)(rng() % 200u + 1u) };
    if (art == 0u) z->voll.width = 0;
    else if (art == 1u) z->voll.height = -3;
    else if (art == 2u) z->voll = rechteck();
    z->hinter_zahl = rng() % (MAXHINTER + 1u);
    for (uint32_t i = 0; i < z->hinter_zahl; i++) z->hinter[i] = rechteck();
    z->fl_zahl = rng() % (MAXFL + 1u);
    for (uint32_t i = 0; i < z->fl_zahl; i++) flaeche(&z->fl[i]);
    /* Kapazitaeten oft knapp -> Kollaps-Pfade */
    z->ws_kap = rng() % 4u == 0u ? rng() % 3u + 1u : rng() % 40u + 1u;
    z->out_kap = rng() % 4u == 0u ? rng() % 3u + 1u : rng() % 40u + 1u;
    z->out_off = 64u;
    if (art == 3u) z->ws_kap = 0u;
    else if (art == 4u) z->out_kap = 0u;
    else if (art == 5u) z->ws_res = 1u;
    else if (art == 6u) z->out_res = 2u;
    else if (art == 7u) z->ws_reg_null = 1;
    else if (art == 8u) z->out_reg_null = 1;
    else if (art == 9u) z->ws_null = 1;
    else if (art == 10u) z->out_null = 1;
    else if (art == 11u && z->hinter_zahl) z->hinter_null = 1;
    else if (art == 12u && z->fl_zahl) z->fl_null = 1;
    else if (art >= 13u && art <= 16u) {
        /* Overlap-Grenzen: Ausgabe direkt hinter (disjunkt) oder ins
         * Workspace-Ende hinein (overlap). */
        uint32_t k = z->ws_kap > 40u ? 40u : z->ws_kap;
        z->ws_kap = k;
        z->out_off = art == 13u ? k : (art == 14u ? k - 1u : rng() % (k + 1u));
        z->out_kap = rng() % 40u + 1u;
    }
}

static uint32_t lauf_c(Seite *s, const Szene *z) {
    return (uint32_t)moo_comp_effect_damage_build(
        z->voll, z->hinter_null ? NULL : z->hinter, z->hinter_zahl,
        z->fl_null ? NULL : z->fl, z->fl_zahl,
        z->ws_null ? NULL : &s->ws, z->out_null ? NULL : &s->out);
}

static uint32_t lauf_m(Seite *s, const Szene *z) {
    return moo_n_damage_build(
        &z->voll, z->hinter_null ? NULL : z->hinter, z->hinter_zahl,
        z->fl_null ? NULL : z->fl, z->fl_zahl,
        z->ws_null ? NULL : &s->ws, z->out_null ? NULL : &s->out);
}

static int seiten_gleich(const Seite *a, const Seite *b) {
    return memcmp(a->puffer, b->puffer, sizeof a->puffer) == 0 &&
           a->ws.capacity == b->ws.capacity && a->ws.reserved == b->ws.reserved &&
           a->out.capacity == b->out.capacity && a->out.count == b->out.count &&
           a->out.full_damage == b->out.full_damage && a->out.reserved == b->out.reserved;
}

static Seite sc, sm;

static void build_fall(long fall, const Szene *z) {
    seite_bauen(&sc, z);
    seite_bauen(&sm, z);
    uint32_t rc_c = lauf_c(&sc, z);
    uint32_t rc_m = lauf_m(&sm, z);
    faelle++;
    if (rc_c != rc_m) { melde("damage_build", fall, rc_c, rc_m); return; }
    if (rc_c < 3u) rc_build[rc_c]++;
    if (!seiten_gleich(&sc, &sm)) { melde("damage_build Speicher/Datensatz", fall, rc_c, rc_m); return; }
    if (rc_c == MOO_COMP_OK) {
        if (sc.out.full_damage) kollaps++;
        if (sc.out.count > sc.out.capacity) melde("damage_build count>capacity", fall, rc_c, rc_m);
    } else if (sc.out.count != 0xC0FFEEu || sc.out.full_damage != 0xBEEFu) {
        melde("damage_build Ausgabe bei Fehler veraendert", fall, rc_c, rc_m);
    }
    if (z->out_off < z->ws_kap && rc_c == MOO_COMP_INVALID) ueberlapp++;
    /* Hash auf dem Ergebnis bzw. dem (evtl. ungueltigen) Datensatz */
    if (!z->out_null) {
        uint64_t hc = moo_comp_effect_damage_hash(&sc.out);
        uint64_t hm = moo_n_damage_hash(&sm.out);
        faelle++;
        if (hc != hm) melde("damage_hash", fall, (uint32_t)hc, (uint32_t)hm);
        if (hc == 0u) hash_null++;
    }
}

static void propagate_fall(long fall) {
    MooCompRect d = rechteck(), c = rechteck();
    uint16_t r = radius();
    MooCompRect oc = { 0x51, 0x52, 0x53, 0x54 }, om = oc;
    uint32_t rc_c = (uint32_t)moo_comp_effect_damage_propagate_backdrop(d, c, r, &oc);
    uint32_t rc_m = moo_n_damage_propagate_backdrop(&d, &c, r, &om);
    faelle++;
    if (rc_c != rc_m || memcmp(&oc, &om, sizeof oc) != 0) { melde("propagate_backdrop", fall, rc_c, rc_m); return; }
    if (rc_c < 3u) rc_prop[rc_c]++;
}

/* Gezielte Faelle, die der Zufall selten oder nie trifft. */
static void gezielte_faelle(void) {
    Szene z;
    /* 1) NULL-Ausgabe bei propagate */
    MooCompRect d = { 0, 0, 4, 4 }, c = { 0, 0, 10, 10 };
    uint32_t rc_c = (uint32_t)moo_comp_effect_damage_propagate_backdrop(d, c, 2u, NULL);
    uint32_t rc_m = moo_n_damage_propagate_backdrop(&d, &c, 2u, NULL);
    faelle++;
    if (rc_c != rc_m) melde("propagate NULL", -1, rc_c, rc_m);
    /* 2) Workspace-Kapazitaet 1, zwei Hintergrundschaeden -> Kollaps */
    memset(&z, 0, sizeof z);
    z.voll = (MooCompRect){ 0, 0, 100, 80 };
    z.hinter_zahl = 2; z.hinter[0] = (MooCompRect){ 1, 1, 5, 5 }; z.hinter[1] = (MooCompRect){ 20, 20, 5, 5 };
    z.ws_kap = 1; z.out_kap = 8; z.out_off = 64;
    build_fall(-2, &z);
    if (!(sc.out.full_damage == 1u && sc.out.count == 1u && sc.out.regions[0].width == 100)) melde("Kollaps Workspace", -2, 0, 0);
    /* 3) Ausgabe-Kapazitaet kleiner als count -> Kollaps */
    z.ws_kap = 8; z.out_kap = 1;
    build_fall(-3, &z);
    if (!(sc.out.full_damage == 1u && sc.out.count == 1u)) melde("Kollaps Ausgabe", -3, 0, 0);
    /* 4) Blur-Kette: Schaden unten breitet sich ueber zwei Backdrop-Flaechen aus */
    memset(&z, 0, sizeof z);
    z.voll = (MooCompRect){ 0, 0, 200, 200 };
    z.hinter_zahl = 1; z.hinter[0] = (MooCompRect){ 50, 50, 10, 10 };
    z.fl_zahl = 2;
    for (int i = 0; i < 2; i++) {
        z.fl[i].surface = 0x100000002ull + (uint64_t)i;
        z.fl[i].enabled_mask = MOO_COMP_EFFECT_BACKDROP_BLUR;
        z.fl[i].backdrop_blur_radius = 8u;
        z.fl[i].backdrop_coverage_bounds = (MooCompRect){ 30 + 10 * i, 30, 80, 80 };
        z.fl[i].changed = (uint32_t)i;
        z.fl[i].new_visual_bounds = (MooCompRect){ 150, 150, 10, 10 };
    }
    z.ws_kap = 16; z.out_kap = 16; z.out_off = 64;
    build_fall(-4, &z);
    if (!(sc.out.full_damage == 0u && sc.out.count >= 4u)) melde("Blur-Kette", -4, 0, 0);
    /* 5) Overlap exakt an der Grenze: disjunkt (OK) bzw. 1 Rechteck Ueberlappung (INVALID) */
    z.ws_kap = 16; z.out_off = 16;
    build_fall(-5, &z);
    if (sc.out.count == 0xC0FFEEu) melde("Grenze disjunkt", -5, 0, 0);
    z.out_off = 15;
    build_fall(-6, &z);
    if (sc.out.count != 0xC0FFEEu) melde("Grenze overlap", -6, 0, 0);
    /* 6) Adressraum-Ueberlauf im Overlap-Test (vor jeder Dereferenzierung -> INVALID) */
    {
        MooCompEffectDamageWorkspace wc = { (MooCompRect *)(uintptr_t)(UINTPTR_MAX - 15u), 2u, 0u }, wm = wc;
        MooCompRect pc[4], pm[4];
        MooCompEffectDamageOutput oc = { pc, 4u, 7u, 7u, 0u }, om = { pm, 4u, 7u, 7u, 0u };
        rc_c = (uint32_t)moo_comp_effect_damage_build(z.voll, NULL, 0u, NULL, 0u, &wc, &oc);
        rc_m = moo_n_damage_build(&z.voll, NULL, 0u, NULL, 0u, &wm, &om);
        faelle++;
        if (rc_c != rc_m || rc_c != MOO_COMP_INVALID || om.count != 7u) melde("Adressraum-Ueberlauf", -7, rc_c, rc_m);
    }
    /* 7) Hash: NULL, count>capacity, full_damage>1, reserved, regions NULL */
    {
        MooCompRect r[2] = { { -1, 2, 3, INT32_MIN }, { INT32_MAX, 0, 1, 1 } };
        MooCompEffectDamageOutput o[6] = {
            { r, 2u, 2u, 0u, 0u }, { r, 2u, 3u, 0u, 0u }, { r, 2u, 1u, 2u, 0u },
            { r, 2u, 1u, 1u, 9u }, { NULL, 2u, 1u, 0u, 0u }, { r, 2u, 0u, 1u, 0u } };
        faelle++;
        if (moo_comp_effect_damage_hash(NULL) != moo_n_damage_hash(NULL)) melde("hash NULL", -8, 0, 0);
        for (int i = 0; i < 6; i++) {
            uint64_t hc = moo_comp_effect_damage_hash(&o[i]), hm = moo_n_damage_hash(&o[i]);
            faelle++;
            if (hc != hm) melde("hash gezielt", -10 - i, (uint32_t)hc, (uint32_t)hm);
        }
    }
}

static void differenztest(long runden) {
    Szene z;
    gezielte_faelle();
    for (long i = 0; i < runden; i++) {
        propagate_fall(i);
        szene_wuerfeln(&z);
        build_fall(i, &z);
    }
}

static double jetzt_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

#define POOL 64
static Szene bench_szene[POOL];

__attribute__((noinline)) static uint64_t lauf_build_c(long n) {
    uint64_t s = 0;
    for (long i = 0; i < n; i++) {
        const Szene *z = &bench_szene[i & (POOL - 1)];
        sc.out.count = 0u; sc.out.full_damage = 0u;
        s += lauf_c(&sc, z);
        s = s * 31u + moo_comp_effect_damage_hash(&sc.out);
    }
    return s;
}
__attribute__((noinline)) static uint64_t lauf_build_m(long n) {
    uint64_t s = 0;
    for (long i = 0; i < n; i++) {
        const Szene *z = &bench_szene[i & (POOL - 1)];
        sm.out.count = 0u; sm.out.full_damage = 0u;
        s += lauf_m(&sm, z);
        s = s * 31u + moo_n_damage_hash(&sm.out);
    }
    return s;
}

static void bench(long n, int laeufe) {
    double tc[16], tm[16];
    uint64_t pc = 0, pm = 0;
    if (laeufe > 16) laeufe = 16;
    /* Gueltige Szenen mit realistischer Last: 4 Hintergrundschaeden, 6 Flaechen */
    for (int k = 0; k < POOL; k++) {
        Szene *z = &bench_szene[k];
        memset(z, 0, sizeof *z);
        z->voll = (MooCompRect){ 0, 0, 1920, 1080 };
        z->hinter_zahl = 4;
        for (int i = 0; i < 4; i++)
            z->hinter[i] = (MooCompRect){ (int32_t)(rng() % 1800u), (int32_t)(rng() % 1000u),
                                          (int32_t)(rng() % 200u + 1u), (int32_t)(rng() % 120u + 1u) };
        z->fl_zahl = 6;
        for (int i = 0; i < 6; i++) {
            MooCompEffectDamageSurface *f = &z->fl[i];
            f->surface = 0x100000002ull + (uint64_t)i;
            f->enabled_mask = (rng() & 1u) ? MOO_COMP_EFFECT_BACKDROP_BLUR : 0u;
            f->backdrop_blur_radius = f->enabled_mask ? (uint16_t)(rng() % 24u) : 0u;
            f->backdrop_coverage_bounds = (MooCompRect){ (int32_t)(rng() % 1500u), (int32_t)(rng() % 700u), 400, 300 };
            f->old_visual_bounds = f->backdrop_coverage_bounds;
            f->new_visual_bounds = (MooCompRect){ f->old_visual_bounds.x + 3, f->old_visual_bounds.y, 400, 300 };
            f->changed = rng() & 1u;
        }
        z->ws_kap = 64; z->out_kap = 64; z->out_off = 64;
    }
    seite_bauen(&sc, &bench_szene[0]);
    seite_bauen(&sm, &bench_szene[0]);
    lauf_build_c(n / 4); lauf_build_m(n / 4);
    for (int r = 0; r < laeufe; r++) {
        double t0 = jetzt_ns(); pc = lauf_build_c(n); tc[r] = (jetzt_ns() - t0) / (double)n;
        t0 = jetzt_ns(); pm = lauf_build_m(n); tm[r] = (jetzt_ns() - t0) / (double)n;
    }
    if (pc != pm) { fprintf(stderr, "BENCH damage_build: Pruefsumme C=%016llx Moo=%016llx\n",
                            (unsigned long long)pc, (unsigned long long)pm); fehler++; }
    qsort(tc, (size_t)laeufe, sizeof tc[0], cmp_double);
    qsort(tm, (size_t)laeufe, sizeof tm[0], cmp_double);
    printf("BENCH damage_build+hash n=%ld laeufe=%d median C=%.2f ns Moo=%.2f ns Faktor=%.3f summe=%016llx\n",
           n, laeufe, tc[laeufe / 2], tm[laeufe / 2], tm[laeufe / 2] / tc[laeufe / 2], (unsigned long long)pc);
}

int main(int argc, char **argv) {
    long runden = argc > 1 ? atol(argv[1]) : 300000;
    long n = argc > 2 ? atol(argv[2]) : 400000;
    int laeufe = argc > 3 ? atoi(argv[3]) : 7;
    differenztest(runden);
    printf("DIFF faelle=%ld abweichungen=%ld\n", faelle, fehler);
    printf("DIFF damage_build       OK=%ld INVALID=%ld LIMIT=%ld kollaps=%ld overlap_invalid=%ld\n",
           rc_build[0], rc_build[1], rc_build[2], kollaps, ueberlapp);
    printf("DIFF propagate_backdrop OK=%ld INVALID=%ld LIMIT=%ld\n", rc_prop[0], rc_prop[1], rc_prop[2]);
    printf("DIFF damage_hash        null=%ld\n", hash_null);
    if (runden > 0 && (rc_build[0] == 0 || rc_build[1] == 0 || rc_build[2] == 0 || kollaps == 0 ||
                       ueberlapp == 0 || rc_prop[0] == 0 || rc_prop[1] == 0 || rc_prop[2] == 0)) {
        puts("DIFF: nicht alle Resultcodes/Pfade erreicht"); fehler++;
    }
    if (n > 0 && laeufe > 0) bench(n, laeufe);
    if (fehler) { puts("ERGEBNIS: ROT"); return 1; }
    puts("ERGEBNIS: GRUEN (natives Moo identisch zu moo_compositor_effects_damage.c)");
    return 0;
}

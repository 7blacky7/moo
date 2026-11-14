/* P2-T68: Differenztest + Benchmark natives Moo (animation_nativ.moos) gegen
 * compiler/runtime/moo_compositor_animation.c. Die C-Datei wird direkt
 * eingebunden, damit auch die statischen Helfer (decode/encode signed,
 * round signed/unsigned div, direction_forward, eval_slot) als Orakel dienen.
 * Verglichen werden je Fall Resultcode UND jedes Ausgabewort; bei Fehlern muss
 * die Ausgabe identisch zur C-Seite sein (Waechterwerte vorbelegt). */
#define _POSIX_C_SOURCE 199309L
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "moo_compositor_animation.c"

_Static_assert(MOO_COMP_EFFECT_ANIMATION_VALUE_WORDS == 8, "8 Wertwoerter");
_Static_assert(MOO_COMP_ANIMATION_PROGRESS_ONE == 65535u, "Q = 65535");
_Static_assert(MOO_COMP_ANIMATION_PROPERTY_OPACITY == 1 && MOO_COMP_ANIMATION_PROPERTY_CORNERS == 2 &&
               MOO_COMP_ANIMATION_PROPERTY_SHADOW == 3 && MOO_COMP_ANIMATION_PROPERTY_BACKDROP == 4 &&
               MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D == 5, "Property-Werte wie animation_nativ.moos");
_Static_assert(MOO_COMP_ANIMATION_EASING_LINEAR == 0 && MOO_COMP_ANIMATION_EASING_IN_QUAD == 1 &&
               MOO_COMP_ANIMATION_EASING_OUT_QUAD == 2 && MOO_COMP_ANIMATION_EASING_IN_OUT_QUAD == 3,
               "Easing-Werte");
_Static_assert(MOO_COMP_ANIMATION_DIRECTION_NORMAL == 0 && MOO_COMP_ANIMATION_DIRECTION_REVERSE == 1 &&
               MOO_COMP_ANIMATION_DIRECTION_ALTERNATE == 2 &&
               MOO_COMP_ANIMATION_DIRECTION_ALTERNATE_REVERSE == 3, "Richtungs-Werte");
_Static_assert(sizeof(MooCompAnimationValue) == 32, "Wert = 32 Byte");
_Static_assert(sizeof(MooCompAnimationDesc) == 112 &&
               offsetof(MooCompAnimationDesc, duration_ns) == 16 &&
               offsetof(MooCompAnimationDesc, repeat_count) == 24 &&
               offsetof(MooCompAnimationDesc, property) == 28 &&
               offsetof(MooCompAnimationDesc, easing) == 32 &&
               offsetof(MooCompAnimationDesc, direction) == 36 &&
               offsetof(MooCompAnimationDesc, from) == 48 &&
               offsetof(MooCompAnimationDesc, to) == 80, "Desc-Layout");
_Static_assert(sizeof(MooCompAnimationSlot) == 176 &&
               offsetof(MooCompAnimationSlot, desc) == 8 &&
               offsetof(MooCompAnimationSlot, start_ns) == 152 &&
               offsetof(MooCompAnimationSlot, end_ns) == 160 &&
               offsetof(MooCompAnimationSlot, active) == 168,
               "Slot-Layout = Offsets in moo_n_anim_eval_slot");

/* Natives Moo (@einstieg). Zeiger als u64-Adresse = gleiche C-ABI. */
int64_t moo_n_anim_decode_signed(uint32_t raw);
uint32_t moo_n_anim_encode_signed(int64_t value);
int64_t moo_n_anim_round_signed_div(int64_t numerator, uint64_t denominator);
uint64_t moo_n_anim_round_unsigned_div(uint64_t numerator, uint64_t denominator);
uint32_t moo_n_anim_ease(uint32_t easing, uint32_t progress, uint32_t *out);
uint32_t moo_n_anim_interpolate(uint32_t property, const MooCompAnimationValue *from,
                                const MooCompAnimationValue *to, uint32_t eased,
                                MooCompAnimationValue *out);
uint32_t moo_n_anim_direction_forward(uint32_t direction, uint64_t play);
uint32_t moo_n_anim_eval_slot(const MooCompAnimationSlot *slot, uint64_t timestamp_ns,
                              uint32_t reduced_motion, MooCompAnimationValue *out,
                              uint32_t *out_terminal);

static uint64_t rng_state = 0x9e3779b97f4a7c15ull;
static uint64_t rng64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static uint32_t rng(void) { return (uint32_t)(rng64() >> 32); }

static long faelle, fehler;
static long rc_ease[3], rc_interp[3], rc_slot[3], slot_terminal;

static void melde(const char *was, long fall, uint64_t c, uint64_t m) {
    if (fehler < 20)
        fprintf(stderr, "ABWEICHUNG %s Fall %ld: C=%llu Moo=%llu\n", was, fall,
                (unsigned long long)c, (unsigned long long)m);
    fehler++;
}

static uint32_t wort(void) {
    static const uint32_t sonder[] = {
        0u, 1u, 2u, 255u, 256u, 65535u, 65536u, 0x7fffffffu, 0x80000000u, 0x80000001u,
        0xffffffffu, 0xfffffffeu, 0xff000000u, 0x00ff00ffu, 0xffff0000u, 0x10000u,
    };
    uint32_t art = rng() % 8u;
    if (art == 0u) return sonder[rng() % (sizeof sonder / sizeof sonder[0])];
    if (art == 1u) return rng() % 512u;
    if (art == 2u) return (uint32_t)(-(int32_t)(rng() % 200000u));
    return rng();
}

static void wert_wuerfeln(MooCompAnimationValue *v) {
    for (int i = 0; i < 8; i++) v->word[i] = wort();
}

static uint32_t eased_wert(void) {
    static const uint32_t sonder[] = { 0u, 1u, 2u, 32767u, 32768u, 65533u, 65534u, 65535u };
    uint32_t art = rng() % 64u;
    if (art == 0u) return 65536u + rng() % 1000u;   /* INVALID */
    if (art < 24u) return sonder[rng() % 8u];
    return rng() % 65536u;
}

static void helfer_faelle(long runden) {
    static const uint32_t raw_sonder[] = { 0u, 1u, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xffffffffu };
    for (int i = 0; i < 6; i++) {
        faelle++;
        if (moo_comp_animation_decode_signed(raw_sonder[i]) != moo_n_anim_decode_signed(raw_sonder[i]))
            melde("decode_signed", -i, raw_sonder[i], 0);
    }
    for (long k = 0; k < runden; k++) {
        uint32_t raw = rng();
        int64_t c = moo_comp_animation_decode_signed(raw), m = moo_n_anim_decode_signed(raw);
        faelle++;
        if (c != m) melde("decode_signed", k, (uint64_t)c, (uint64_t)m);

        int64_t v = (int64_t)(rng64() % (UINT64_C(1) << 41)) - (INT64_C(1) << 40);
        if (rng() % 4u == 0u) v = (int64_t)(rng() % 0x100000000ull) - INT64_C(0x80000000);
        uint32_t ec = moo_comp_animation_encode_signed(v), em = moo_n_anim_encode_signed(v);
        faelle++;
        if (ec != em) melde("encode_signed", k, ec, em);

        int64_t n = (int64_t)(rng64() % (UINT64_C(1) << 51)) - (INT64_C(1) << 50);
        uint64_t d = rng() % 4u == 0u ? 65535u : (uint64_t)(rng() % 0xffffffffu) + 1u;
        if (rng() % 8u == 0u) n = (int64_t)(d / 2u) * ((rng() & 1u) ? 1 : -1) + (int64_t)(rng() % 3u) - 1;
        int64_t rc = moo_comp_animation_round_signed_div(n, d), rm = moo_n_anim_round_signed_div(n, d);
        faelle++;
        if (rc != rm) melde("round_signed_div", k, (uint64_t)rc, (uint64_t)rm);

        uint64_t un = rng64(), ud = (rng64() % 0xffffffffffull) + 1u;
        uint64_t uc = moo_comp_animation_round_unsigned_div(un, ud), um = moo_n_anim_round_unsigned_div(un, ud);
        faelle++;
        if (uc != um) melde("round_unsigned_div", k, uc, um);

        uint32_t dir = rng() % 6u;
        uint64_t play = rng() % 4u == 0u ? rng64() : rng() % 16u;
        faelle++;
        if (moo_comp_animation_direction_forward(dir, play) != moo_n_anim_direction_forward(dir, play))
            melde("direction_forward", k, dir, play);
    }
}

static void ease_faelle(void) {
    /* Vollstaendig: alle Easings 0..5 x alle Fortschritte 0..65535 + ungueltige */
    for (uint32_t e = 0u; e < 6u; e++) {
        for (uint32_t p = 0u; p <= 65536u + 3u; p++) {
            uint32_t pp = p <= 65536u ? p : 0xffffffffu - (p - 65537u);
            uint32_t oc = 0xA5A5A5A5u, om = 0xA5A5A5A5u;
            uint32_t rc = (uint32_t)moo_comp_animation_ease(e, pp, &oc);
            uint32_t rm = moo_n_anim_ease(e, pp, &om);
            faelle++;
            if (rc != rm || oc != om) { melde("ease", (long)(e * 70000u + p), oc, om); continue; }
            if (rc < 3u) rc_ease[rc]++;
        }
    }
    faelle++;
    if ((uint32_t)moo_comp_animation_ease(0u, 5u, NULL) != moo_n_anim_ease(0u, 5u, NULL)) melde("ease NULL", -1, 0, 0);
}

static void interp_fall(long fall, uint32_t prop, const MooCompAnimationValue *von,
                        const MooCompAnimationValue *nach, uint32_t e, int alias) {
    /* alias 0: eigene Ausgabe, 1: Ausgabe == from, 2: Ausgabe == to */
    MooCompAnimationValue fc = *von, tc = *nach, fm = *von, tm = *nach, oc, om;
    for (int i = 0; i < 8; i++) { oc.word[i] = 0xA5A50000u + (uint32_t)i; om.word[i] = oc.word[i]; }
    MooCompAnimationValue *zc = alias == 1 ? &fc : (alias == 2 ? &tc : &oc);
    MooCompAnimationValue *zm = alias == 1 ? &fm : (alias == 2 ? &tm : &om);
    uint32_t rc = (uint32_t)moo_comp_animation_interpolate(prop, &fc, &tc, e, zc);
    uint32_t rm = moo_n_anim_interpolate(prop, &fm, &tm, e, zm);
    faelle++;
    if (rc != rm) { melde("interpolate rc", fall, rc, rm); return; }
    if (rc < 3u) rc_interp[rc]++;
    if (memcmp(&fc, &fm, sizeof fc) != 0 || memcmp(&tc, &tm, sizeof tc) != 0 ||
        memcmp(&oc, &om, sizeof oc) != 0) {
        for (int i = 0; i < 8; i++)
            if (zc->word[i] != zm->word[i]) { melde("interpolate Wort", fall * 8 + i, zc->word[i], zm->word[i]); return; }
        melde("interpolate Speicher", fall, 0, 0);
    }
}

static void interp_faelle(long runden) {
    MooCompAnimationValue a, b;
    for (long k = 0; k < runden; k++) {
        wert_wuerfeln(&a);
        wert_wuerfeln(&b);
        uint32_t prop = rng() % 16u == 0u ? (rng() % 2u ? 0u : 6u + rng() % 100u) : rng() % 5u + 1u;
        int alias = rng() % 8u == 0u ? (int)(rng() % 2u) + 1 : 0;
        interp_fall(k, prop, &a, &b, eased_wert(), alias);
    }
    /* NULL-Zeiger */
    wert_wuerfeln(&a); b = a;
    MooCompAnimationValue o;
    faelle += 3;
    if ((uint32_t)moo_comp_animation_interpolate(5u, NULL, &b, 1u, &o) != moo_n_anim_interpolate(5u, NULL, &b, 1u, &o)) melde("interp NULL from", -1, 0, 0);
    if ((uint32_t)moo_comp_animation_interpolate(5u, &a, NULL, 1u, &o) != moo_n_anim_interpolate(5u, &a, NULL, 1u, &o)) melde("interp NULL to", -2, 0, 0);
    if ((uint32_t)moo_comp_animation_interpolate(5u, &a, &b, 1u, NULL) != moo_n_anim_interpolate(5u, &a, &b, 1u, NULL)) melde("interp NULL out", -3, 0, 0);
}

static void slot_wuerfeln(MooCompAnimationSlot *s, uint64_t *zeit) {
    uint32_t art = rng() % 64u;
    memset(s, 0, sizeof *s);
    s->surface = 0x200000001ull;
    s->active = 1u;
    s->desc.token = rng64() | 1u;
    s->desc.duration_ns = rng() % 4u == 0u ? (uint64_t)(rng() % 64u) + 1u : (uint64_t)(rng() % 1000000000u) + 1u;
    s->desc.repeat_count = rng() % 5u + 1u;
    s->desc.property = rng() % 5u + 1u;
    s->desc.easing = rng() % 4u;
    s->desc.direction = rng() % 4u;
    wert_wuerfeln(&s->desc.from);
    wert_wuerfeln(&s->desc.to);
    s->start_ns = rng64() % (UINT64_C(1) << 50);
    s->end_ns = s->start_ns + s->desc.duration_ns * s->desc.repeat_count;
    if (art == 0u) s->active = 0u;
    else if (art == 1u) s->desc.easing = 4u + rng() % 10u;     /* INVALID nach terminal */
    else if (art == 2u) s->desc.direction = 4u + rng() % 10u;  /* unbekannt = rueckwaerts */
    else if (art == 3u) s->desc.property = rng() % 2u ? 0u : 6u;
    else if (art == 4u) s->desc.repeat_count = 0u;             /* play wickelt wie C */
    uint64_t gesamt = s->end_ns - s->start_ns;
    uint32_t z = rng() % 10u;
    if (z == 0u) *zeit = s->start_ns - (rng64() % (s->start_ns + 1u));
    else if (z == 1u) *zeit = s->start_ns;
    else if (z == 2u) *zeit = s->end_ns;
    else if (z == 3u) *zeit = s->end_ns + rng64() % 1000000u;
    else if (z == 4u && s->desc.repeat_count > 1u)              /* exakte Wiederholungsgrenze */
        *zeit = s->start_ns + s->desc.duration_ns * (rng() % (s->desc.repeat_count - 1u) + 1u);
    else if (z == 5u && gesamt > 0u) *zeit = s->end_ns - 1u;
    else *zeit = s->start_ns + (gesamt ? rng64() % gesamt : 0u);
}

static void slot_faelle(long runden) {
    MooCompAnimationSlot s;
    for (long k = 0; k < runden; k++) {
        uint64_t zeit = 0;
        slot_wuerfeln(&s, &zeit);
        uint32_t reduziert = rng() % 16u == 0u ? 1u + rng() % 3u : 0u;
        MooCompAnimationValue oc, om;
        for (int i = 0; i < 8; i++) { oc.word[i] = 0x5A5A0000u + (uint32_t)i; om.word[i] = oc.word[i]; }
        uint32_t tc = 0xC0FFEEu, tm = 0xC0FFEEu;
        uint32_t rc = (uint32_t)moo_comp_animation_eval_slot(&s, zeit, reduziert, &oc, &tc);
        uint32_t rm = moo_n_anim_eval_slot(&s, zeit, reduziert, &om, &tm);
        faelle++;
        if (rc != rm) { melde("eval_slot rc", k, rc, rm); continue; }
        if (rc < 3u) rc_slot[rc]++;
        if (tc != tm) { melde("eval_slot terminal", k, tc, tm); continue; }
        if (memcmp(&oc, &om, sizeof oc) != 0) { melde("eval_slot Wert", k, oc.word[0], om.word[0]); continue; }
        if (rc == 0u && tc == 1u) slot_terminal++;
    }
    /* NULL-Zeiger */
    slot_wuerfeln(&s, &(uint64_t){0});
    MooCompAnimationValue o;
    uint32_t t;
    faelle += 3;
    if ((uint32_t)moo_comp_animation_eval_slot(NULL, 0u, 0u, &o, &t) != moo_n_anim_eval_slot(NULL, 0u, 0u, &o, &t)) melde("slot NULL", -1, 0, 0);
    if ((uint32_t)moo_comp_animation_eval_slot(&s, 0u, 0u, NULL, &t) != moo_n_anim_eval_slot(&s, 0u, 0u, NULL, &t)) melde("slot NULL out", -2, 0, 0);
    if ((uint32_t)moo_comp_animation_eval_slot(&s, 0u, 0u, &o, NULL) != moo_n_anim_eval_slot(&s, 0u, 0u, &o, NULL)) melde("slot NULL terminal", -3, 0, 0);
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
static MooCompAnimationSlot bench_slot[POOL];
static uint64_t bench_zeit[POOL];

__attribute__((noinline)) static uint64_t lauf_c(long n) {
    uint64_t s = 0; MooCompAnimationValue o; uint32_t t = 0;
    for (long i = 0; i < n; i++) {
        s += (uint64_t)moo_comp_animation_eval_slot(&bench_slot[i & (POOL - 1)], bench_zeit[i & (POOL - 1)] + (uint64_t)i, 0u, &o, &t);
        s = s * 31u + o.word[i & 7] + t;
    }
    return s;
}
__attribute__((noinline)) static uint64_t lauf_m(long n) {
    uint64_t s = 0; MooCompAnimationValue o; uint32_t t = 0;
    for (long i = 0; i < n; i++) {
        s += moo_n_anim_eval_slot(&bench_slot[i & (POOL - 1)], bench_zeit[i & (POOL - 1)] + (uint64_t)i, 0u, &o, &t);
        s = s * 31u + o.word[i & 7] + t;
    }
    return s;
}

static void bench(long n, int laeufe) {
    double tc[16], tm[16];
    uint64_t pc = 0, pm = 0;
    if (laeufe > 16) laeufe = 16;
    for (int k = 0; k < POOL; k++) {
        MooCompAnimationSlot *s = &bench_slot[k];
        memset(s, 0, sizeof *s);
        s->active = 1u;
        s->desc.duration_ns = 250000000u;         /* 250 ms */
        s->desc.repeat_count = 3u;
        s->desc.property = k % 2 ? MOO_COMP_ANIMATION_PROPERTY_AFFINE_2D : MOO_COMP_ANIMATION_PROPERTY_SHADOW;
        s->desc.easing = (uint32_t)k % 4u;
        s->desc.direction = (uint32_t)(k / 4) % 4u;
        for (int i = 0; i < 8; i++) {
            s->desc.from.word[i] = (uint32_t)(int32_t)((int32_t)(rng() % 200000u) - 100000);
            s->desc.to.word[i] = (uint32_t)(int32_t)((int32_t)(rng() % 200000u) - 100000);
        }
        s->start_ns = 1000000u;
        s->end_ns = s->start_ns + 750000000u;
        bench_zeit[k] = s->start_ns + rng() % 700000000u;
    }
    lauf_c(n / 4); lauf_m(n / 4);
    for (int r = 0; r < laeufe; r++) {
        double t0 = jetzt_ns(); pc = lauf_c(n); tc[r] = (jetzt_ns() - t0) / (double)n;
        t0 = jetzt_ns(); pm = lauf_m(n); tm[r] = (jetzt_ns() - t0) / (double)n;
    }
    if (pc != pm) { fprintf(stderr, "BENCH eval_slot: Pruefsumme C=%016llx Moo=%016llx\n",
                            (unsigned long long)pc, (unsigned long long)pm); fehler++; }
    qsort(tc, (size_t)laeufe, sizeof tc[0], cmp_double);
    qsort(tm, (size_t)laeufe, sizeof tm[0], cmp_double);
    printf("BENCH eval_slot n=%ld laeufe=%d median C=%.2f ns Moo=%.2f ns Faktor=%.3f summe=%016llx\n",
           n, laeufe, tc[laeufe / 2], tm[laeufe / 2], tm[laeufe / 2] / tc[laeufe / 2], (unsigned long long)pc);
}

int main(int argc, char **argv) {
    long runden = argc > 1 ? atol(argv[1]) : 300000;
    long n = argc > 2 ? atol(argv[2]) : 2000000;
    int laeufe = argc > 3 ? atoi(argv[3]) : 7;
    helfer_faelle(runden);
    ease_faelle();
    interp_faelle(runden);
    slot_faelle(runden);
    printf("DIFF faelle=%ld abweichungen=%ld\n", faelle, fehler);
    printf("DIFF ease        OK=%ld INVALID=%ld\n", rc_ease[0], rc_ease[1]);
    printf("DIFF interpolate OK=%ld INVALID=%ld\n", rc_interp[0], rc_interp[1]);
    printf("DIFF eval_slot   OK=%ld INVALID=%ld terminal=%ld\n", rc_slot[0], rc_slot[1], slot_terminal);
    if (runden > 0 && (rc_ease[0] == 0 || rc_ease[1] == 0 || rc_interp[0] == 0 || rc_interp[1] == 0 ||
                       rc_slot[0] == 0 || rc_slot[1] == 0 || slot_terminal == 0)) {
        puts("DIFF: nicht alle Resultcodes/Pfade erreicht"); fehler++;
    }
    if (n > 0 && laeufe > 0) bench(n, laeufe);
    if (fehler) { puts("ERGEBNIS: ROT"); return 1; }
    puts("ERGEBNIS: GRUEN (natives Moo identisch zu moo_compositor_animation.c)");
    return 0;
}

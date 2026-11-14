/* P2-T71: Differenztest natives Moo (animation_zeitleiste_nativ.moos +
 * animation_nativ.moos + animation_pruefung_nativ.moos) gegen die Timeline in
 * compiler/runtime/moo_compositor_animation.c (eingebunden). Zwei getrennte
 * Timelines (C / Moo) durchlaufen dieselbe zufaellige Operationsfolge
 * (init, start, evaluate, cancel, destroy_surface, active_count, state_hash);
 * nach JEDER Operation werden Resultcode, alle Ausgaben (Wert, Samples,
 * Completions, Zaehler, mit Waechterwerten) und der komplette Timeline- und
 * Slot-Speicher verglichen (Slot-Zeiger ausgenommen). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "moo_compositor_animation.c"

_Static_assert(sizeof(MooCompAnimationTimeline) == 112 &&
               offsetof(MooCompAnimationTimeline, capacity) == 8 &&
               offsetof(MooCompAnimationTimeline, active_count) == 12 &&
               offsetof(MooCompAnimationTimeline, limits) == 16 &&
               offsetof(MooCompAnimationTimeline, last_timestamp_ns) == 96 &&
               offsetof(MooCompAnimationTimeline, has_timestamp) == 104, "Timeline-Layout");
_Static_assert(sizeof(MooCompAnimationSlot) == 176 && offsetof(MooCompAnimationSlot, current) == 120 &&
               offsetof(MooCompAnimationSlot, start_ns) == 152 && offsetof(MooCompAnimationSlot, active) == 168,
               "Slot-Layout");
_Static_assert(sizeof(MooCompAnimationCompletion) == 32 && offsetof(MooCompAnimationCompletion, status) == 16 &&
               offsetof(MooCompAnimationCompletion, timestamp_ns) == 24, "Completion-Layout");
_Static_assert(sizeof(MooCompAnimationSample) == 56 && offsetof(MooCompAnimationSample, property) == 16 &&
               offsetof(MooCompAnimationSample, terminal) == 20 && offsetof(MooCompAnimationSample, value) == 24,
               "Sample-Layout");
_Static_assert(offsetof(MooCompEffectLimits, max_animations_per_surface) == 28 &&
               offsetof(MooCompEffectLimits, max_animations_per_client) == 32, "Limits-Felder");
_Static_assert(MOO_COMP_ANIMATION_DONE_COMPLETED == 1 && MOO_COMP_ANIMATION_DONE_CANCELLED == 2 &&
               MOO_COMP_ANIMATION_DONE_REPLACED == 3 && MOO_COMP_ANIMATION_DONE_SURFACE_DESTROYED == 4 &&
               MOO_COMP_ANIMATION_DONE_REDUCED_MOTION == 5, "Completion-Status");

uint32_t moo_n_anim_timeline_init(MooCompAnimationTimeline *t, MooCompAnimationSlot *slots, uint32_t cap,
                                  const MooCompEffectLimits *limits);
uint32_t moo_n_anim_start(MooCompAnimationTimeline *t, uint64_t surface, const MooCompAnimationDesc *desc,
                          const MooCompAnimationValue *current, uint64_t mask, uint64_t frame_ns,
                          uint32_t reduced, MooCompAnimationValue *out_value, MooCompAnimationCompletion *compl,
                          uint32_t compl_cap, uint32_t *out_count);
uint32_t moo_n_anim_evaluate(MooCompAnimationTimeline *t, uint64_t ts, uint32_t reduced,
                             MooCompAnimationSample *samples, uint32_t sample_cap, uint32_t *out_samples,
                             MooCompAnimationCompletion *compl, uint32_t compl_cap, uint32_t *out_compl);
uint32_t moo_n_anim_cancel(MooCompAnimationTimeline *t, uint64_t surface, uint64_t token, uint64_t ts,
                           MooCompAnimationCompletion *compl, uint32_t cap, uint32_t *out_count);
uint32_t moo_n_anim_destroy_surface(MooCompAnimationTimeline *t, uint64_t surface, uint64_t ts,
                                    MooCompAnimationCompletion *compl, uint32_t cap, uint32_t *out_count);
uint32_t moo_n_anim_active_count(const MooCompAnimationTimeline *t);
uint64_t moo_n_anim_state_hash(const MooCompAnimationTimeline *t);

static uint64_t rng_state = 0x2b992ddfa23249d6ull;
static uint64_t rng64(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}
static uint32_t rng(void) { return (uint32_t)(rng64() >> 32); }
static uint64_t bereich(uint64_t lo, uint64_t hi) { return lo + rng64() % (hi - lo + 1u); }

static long faelle, fehler, rc_zahl[3], status_zahl[6], ersetzt, ablaeufe;

static void melde(const char *was, long fall, uint64_t c, uint64_t m) {
    if (fehler < 20) fprintf(stderr, "ABWEICHUNG %s Schritt %ld: C=%llu Moo=%llu\n", was, fall,
                             (unsigned long long)c, (unsigned long long)m);
    fehler++;
}

#define MAXKAP 12
#define MAXAUS 16
static MooCompAnimationTimeline tc, tm;
static MooCompAnimationSlot sc[MAXKAP], sm[MAXKAP];

/* Timeline- und Slot-Speicher vergleichen (slots-Zeiger @0 ausgenommen). */
static void zustand_vergleichen(const char *was, long schritt) {
    faelle++;
    if (memcmp((const char *)&tc + 8, (const char *)&tm + 8, sizeof tc - 8) != 0 ||
        memcmp(sc, sm, sizeof sc) != 0) { melde(was, schritt, tc.active_count, tm.active_count); return; }
    faelle += 2;
    if (moo_comp_animation_active_count(&tc) != moo_n_anim_active_count(&tm))
        melde("active_count", schritt, moo_comp_animation_active_count(&tc), moo_n_anim_active_count(&tm));
    if (moo_comp_animation_state_hash(&tc) != moo_n_anim_state_hash(&tm))
        melde("state_hash", schritt, moo_comp_animation_state_hash(&tc), moo_n_anim_state_hash(&tm));
}

static void zaehle(uint32_t rc) { if (rc < 3u) rc_zahl[rc]++; }
static void status_zaehlen(const MooCompAnimationCompletion *c, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (c[i].status < 6u) status_zahl[c[i].status]++;
}

static void limits_wuerfeln(MooCompEffectLimits *l) {
    *l = moo_comp_effect_limits_default();
    if (rng() % 2u) {
        l->max_animations_per_surface = (uint32_t)bereich(1, 4);
        l->max_animations_per_client = (uint32_t)bereich(l->max_animations_per_surface, 10);
    }
}

/* Wert passend zur Property (meist gueltig, damit start durch validate_desc kommt). */
static void wert_wuerfeln(uint32_t prop, MooCompAnimationValue *v) {
    memset(v, 0, sizeof *v);
    switch (prop) {
        case 1: v->word[0] = (uint32_t)bereich(0, 65536); break;
        case 2: for (int i = 0; i < 4; i++) v->word[i] = (uint32_t)bereich(0, 4096); break;
        case 3: v->word[0] = (uint32_t)(int32_t)((int32_t)bereich(0, 200) - 100);
                v->word[1] = (uint32_t)(int32_t)((int32_t)bereich(0, 200) - 100);
                v->word[2] = (uint32_t)bereich(0, 64); v->word[3] = (uint32_t)bereich(0, 64); v->word[4] = rng(); break;
        case 4: v->word[0] = (uint32_t)bereich(0, 32); v->word[1] = (uint32_t)bereich(256, 1024); v->word[2] = rng();
                v->word[3] = (uint32_t)bereich(0, 255); v->word[4] = (uint32_t)bereich(0, 255); v->word[5] = rng(); break;
        default: for (int i = 0; i < 8; i++) v->word[i] = (uint32_t)(int32_t)((int32_t)bereich(0, 400000) - 200000); break;
    }
}

static const MooCompAnimationSlot *aktiver_slot(uint64_t surface, uint32_t prop) {
    for (uint32_t i = 0; i < tc.capacity; i++)
        if (sc[i].active && sc[i].surface == surface && sc[i].desc.property == prop) return &sc[i];
    return NULL;
}

static void ablauf(long nr, long schritte) {
    MooCompEffectLimits l;
    limits_wuerfeln(&l);
    uint32_t kap = (uint32_t)bereich(1, MAXKAP);
    if (rng() % 8u == 0u) kap = l.max_animations_per_client + 1u;           /* init INVALID */
    memset(&tc, 0xEE, sizeof tc); memset(&tm, 0xEE, sizeof tm);
    memset(sc, 0xDD, sizeof sc); memset(sm, 0xDD, sizeof sm);
    uint32_t rc = (uint32_t)moo_comp_animation_timeline_init(&tc, sc, kap, &l);
    uint32_t rm = moo_n_anim_timeline_init(&tm, sm, kap, &l);
    faelle++;
    if (rc != rm) { melde("timeline_init", nr, rc, rm); return; }
    zaehle(rc);
    if (rc != 0u) return;
    zustand_vergleichen("init Speicher", nr);
    ablaeufe++;
    uint64_t jetzt = bereich(0, 1000000000u);
    for (long k = 0; k < schritte; k++) {
        long schritt = nr * 1000 + k;
        MooCompAnimationCompletion cc[MAXAUS], cm[MAXAUS];
        memset(cc, 0xA1, sizeof cc); memset(cm, 0xA1, sizeof cm);
        uint32_t ncc = 0xBEEFu, ncm = 0xBEEFu;
        uint32_t ccap = rng() % 6u == 0u ? 0u : (uint32_t)bereich(1, MAXAUS);
        MooCompAnimationCompletion *cpc = (ccap == 0u && rng() % 2u) ? NULL : cc;
        MooCompAnimationCompletion *cpm = cpc ? cm : NULL;
        uint64_t zeit = jetzt + bereich(0, 400000000u);
        if (rng() % 24u == 0u && jetzt > 0u) zeit = jetzt - 1u;               /* rueckwaerts -> INVALID */
        switch (rng() % 8u) {
            case 0: case 1: case 2: {                                            /* start */
                MooCompAnimationDesc d;
                memset(&d, 0, sizeof d);
                uint64_t surface = rng() % 16u == 0u ? 0u : bereich(1, 3);
                d.token = bereich(rng() % 16u == 0u ? 0u : 1u, 12);
                d.repeat_count = (uint32_t)bereich(1, 3);
                d.property = (uint32_t)bereich(1, 5);
                d.easing = (uint32_t)bereich(0, 3);
                d.direction = (uint32_t)bereich(0, 3);
                d.duration_ns = bereich(1000000u, 900000000u);
                d.delay_ns = rng() % 2u ? bereich(0, 100000000u) : 0u;
                if (rng() % 32u == 0u) d.flags = 1u;
                wert_wuerfeln(d.property, &d.to);
                MooCompAnimationValue aktuell;
                wert_wuerfeln(d.property, &aktuell);
                const MooCompAnimationSlot *alt = aktiver_slot(surface, d.property);
                if (alt && rng() % 4u != 0u) d.from = alt->current;              /* Ersetzen moeglich */
                else d.from = rng() % 8u == 0u ? d.to : aktuell;                 /* gelegentlich Mismatch */
                uint64_t maske = rng() % 8u == 0u ? (rng() & 0xFFu) : 0xFFu;
                uint32_t red = rng() % 10u == 0u ? (rng() % 8u == 0u ? 2u : 1u) : 0u;
                MooCompAnimationValue oc, om;
                memset(&oc, 0x5C, sizeof oc); memset(&om, 0x5C, sizeof om);
                int ohne_zahl = rng() % 64u == 0u;
                rc = (uint32_t)moo_comp_animation_start(&tc, surface, &d, &aktuell, maske, zeit, red, &oc, cpc, ccap,
                                                        ohne_zahl ? NULL : &ncc);
                rm = moo_n_anim_start(&tm, surface, &d, &aktuell, maske, zeit, red, &om, cpm, ccap,
                                      ohne_zahl ? NULL : &ncm);
                faelle++;
                if (rc != rm || ncc != ncm || memcmp(&oc, &om, sizeof oc) != 0 || memcmp(cc, cm, sizeof cc) != 0)
                    { melde("start", schritt, rc, rm); break; }
                zaehle(rc);
                if (rc == 0u) { status_zaehlen(cc, ncc); if (alt && ncc > 0u && cc[0].status == 3u) ersetzt++; }
                break;
            }
            case 3: case 4: case 5: {                                            /* evaluate */
                MooCompAnimationSample pc[MAXKAP], pm[MAXKAP];
                memset(pc, 0x6D, sizeof pc); memset(pm, 0x6D, sizeof pm);
                uint32_t pcap = rng() % 8u == 0u ? (uint32_t)bereich(0, tc.active_count) : MAXKAP;
                uint32_t npc = 0xBEEFu, npm = 0xBEEFu;
                uint32_t red = rng() % 12u == 0u ? 1u : 0u;
                rc = (uint32_t)moo_comp_animation_evaluate(&tc, zeit, red, pc, pcap, &npc, cpc, ccap, &ncc);
                rm = moo_n_anim_evaluate(&tm, zeit, red, pm, pcap, &npm, cpm, ccap, &ncm);
                faelle++;
                if (rc != rm || npc != npm || ncc != ncm || memcmp(pc, pm, sizeof pc) != 0 ||
                    memcmp(cc, cm, sizeof cc) != 0) { melde("evaluate", schritt, rc, rm); break; }
                zaehle(rc);
                if (rc == 0u) status_zaehlen(cc, ncc);
                break;
            }
            case 6: {                                                            /* cancel */
                uint64_t surface = bereich(rng() % 16u == 0u ? 0u : 1u, 3);
                uint64_t token = bereich(rng() % 16u == 0u ? 0u : 1u, 12);
                rc = (uint32_t)moo_comp_animation_cancel(&tc, surface, token, zeit, cpc, ccap, &ncc);
                rm = moo_n_anim_cancel(&tm, surface, token, zeit, cpm, ccap, &ncm);
                faelle++;
                if (rc != rm || ncc != ncm || memcmp(cc, cm, sizeof cc) != 0) { melde("cancel", schritt, rc, rm); break; }
                zaehle(rc);
                if (rc == 0u) status_zaehlen(cc, ncc);
                break;
            }
            default: {                                                           /* destroy_surface */
                uint64_t surface = bereich(rng() % 16u == 0u ? 0u : 1u, 3);
                rc = (uint32_t)moo_comp_animation_destroy_surface(&tc, surface, zeit, cpc, ccap, &ncc);
                rm = moo_n_anim_destroy_surface(&tm, surface, zeit, cpm, ccap, &ncm);
                faelle++;
                if (rc != rm || ncc != ncm || memcmp(cc, cm, sizeof cc) != 0) { melde("destroy_surface", schritt, rc, rm); break; }
                zaehle(rc);
                if (rc == 0u) status_zaehlen(cc, ncc);
                break;
            }
        }
        zustand_vergleichen("Timeline-Speicher", schritt);
        if (tc.has_timestamp && tc.last_timestamp_ns > jetzt) jetzt = tc.last_timestamp_ns;
    }
}

int main(int argc, char **argv) {
    long runden = argc > 1 ? atol(argv[1]) : 20000;
    long schritte = argc > 2 ? atol(argv[2]) : 60;
    for (long r = 0; r < runden; r++) ablauf(r, schritte);
    /* NULL-/Ungueltig-Faelle ohne Timeline */
    uint32_t n = 7u;
    faelle += 4;
    if ((uint32_t)moo_comp_animation_timeline_init(NULL, sc, 1u, NULL) != moo_n_anim_timeline_init(NULL, sm, 1u, NULL)) melde("init NULL", -1, 0, 0);
    if ((uint32_t)moo_comp_animation_cancel(NULL, 1u, 1u, 0u, NULL, 0u, &n) != moo_n_anim_cancel(NULL, 1u, 1u, 0u, NULL, 0u, &n)) melde("cancel NULL", -2, 0, 0);
    if (moo_comp_animation_active_count(NULL) != moo_n_anim_active_count(NULL)) melde("active_count NULL", -3, 0, 0);
    if (moo_comp_animation_state_hash(NULL) != moo_n_anim_state_hash(NULL)) melde("state_hash NULL", -4, 0, 0);
    printf("DIFF faelle=%ld abweichungen=%ld ablaeufe=%ld\n", faelle, fehler, ablaeufe);
    printf("DIFF rc OK=%ld INVALID=%ld LIMIT=%ld\n", rc_zahl[0], rc_zahl[1], rc_zahl[2]);
    printf("DIFF status COMPLETED=%ld CANCELLED=%ld REPLACED=%ld DESTROYED=%ld REDUCED=%ld ersetzt=%ld\n",
           status_zahl[1], status_zahl[2], status_zahl[3], status_zahl[4], status_zahl[5], ersetzt);
    if (runden > 0 && (rc_zahl[0] == 0 || rc_zahl[1] == 0 || rc_zahl[2] == 0 || status_zahl[1] == 0 ||
                       status_zahl[2] == 0 || status_zahl[3] == 0 || status_zahl[4] == 0 || status_zahl[5] == 0)) {
        puts("DIFF: nicht alle Pfade erreicht"); fehler++;
    }
    if (fehler) { puts("ERGEBNIS: ROT"); return 1; }
    puts("ERGEBNIS: GRUEN (native Timeline identisch zu moo_compositor_animation.c)");
    return 0;
}

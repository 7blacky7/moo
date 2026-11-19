#define _POSIX_C_SOURCE 200809L
#include "native_suite.h"
#include "moo_noise.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum Last { SUMME, VEKTOR, MATRIX, FUELLEN, OBJEKTE, BUILTIN, RAUSCHEN };
enum { WIEDERHOLUNGEN = 5, BREITE = 512 };
enum Seite { C, MOO, SEITEN };

struct Daten {
    enum Last last;
    const char *name;
    uint64_t n;
    void *a;
    void *b;
    float matrix[16];
    int32_t seed;
    size_t groesse;
    void *ausgabe[SEITEN];
};

static uint64_t uhr_ns(void) {
    struct timespec zeit;
    if (clock_gettime(CLOCK_MONOTONIC, &zeit) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)zeit.tv_sec * UINT64_C(1000000000) + (uint64_t)zeit.tv_nsec;
}

static void *reserviere(size_t groesse) {
    void *p = malloc(groesse);
    if (!p) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    return p;
}

static void vorbereiten(struct Daten *d) {
    switch (d->last) {
    case SUMME: {
        uint64_t *werte = reserviere(d->n * sizeof(*werte));
        for (uint64_t i = 0; i < d->n; ++i)
            werte[i] = i;
        d->a = werte;
        d->groesse = sizeof(uint64_t);
        break;
    }
    case VEKTOR: {
        float *a = reserviere(d->n * sizeof(*a));
        float *b = reserviere(d->n * sizeof(*b));
        for (uint64_t i = 0; i < d->n; ++i)
            a[i] = b[i] = (float)(i % 100);
        d->a = a;
        d->b = b;
        d->groesse = d->n * sizeof(float);
        break;
    }
    case MATRIX: {
        const float m[] = {1, 2, 3, 4, 2, 3, 4, 5, 3, 4, 5, 6, 4, 5, 6, 7};
        memcpy(d->matrix, m, sizeof(m));
        float *v = reserviere(d->n * 4 * sizeof(*v));
        for (uint64_t i = 0; i < d->n; ++i) {
            v[4 * i] = (float)(i % 100);
            v[4 * i + 1] = 2.0f;
            v[4 * i + 2] = 3.0f;
            v[4 * i + 3] = 4.0f;
        }
        d->a = v;
        d->groesse = d->n * 4 * sizeof(float);
        break;
    }
    case FUELLEN:
        d->groesse = d->n * sizeof(float);
        break;
    case OBJEKTE: {
        Punkt *punkte = reserviere(d->n * sizeof(*punkte));
        for (uint64_t i = 0; i < d->n; ++i)
            punkte[i] = (Punkt){(double)(i % 100), 2.0};
        d->a = punkte;
        d->groesse = sizeof(double);
        break;
    }
    case BUILTIN:
        d->groesse = sizeof(double);
        break;
    case RAUSCHEN: {
        float *koordinaten = reserviere(BREITE * sizeof(*koordinaten));
        for (unsigned i = 0; i < BREITE; ++i)
            koordinaten[i] = (float)i / 32.0f;
        d->a = koordinaten;
        d->seed = moo_noise_scramble_seed(17);
        d->groesse = d->n * sizeof(float);
        break;
    }
    }
    for (enum Seite s = C; s < SEITEN; ++s)
        d->ausgabe[s] = reserviere(d->groesse);
}

/* Beide Seiten werden durch denselben Messrahmen über die C-ABI aufgerufen. */
static void kern(const struct Daten *d, enum Seite seite) {
    void *ausgabe = d->ausgabe[seite];
    switch (d->last) {
    case SUMME:
        *(uint64_t *)ausgabe =
            (seite == C ? p2_c_summe : p2_moo_summe)(d->a, d->n);
        break;
    case VEKTOR:
        (seite == C ? p2_c_vektor : p2_moo_vektor)(d->a, d->b, ausgabe, d->n);
        break;
    case MATRIX:
        (seite == C ? p2_c_matrix : p2_moo_matrix)(d->matrix, d->a, ausgabe, d->n);
        break;
    case FUELLEN:
        (seite == C ? p2_c_fuellen : p2_moo_fuellen)(ausgabe, d->n);
        break;
    case OBJEKTE:
        *(double *)ausgabe =
            (seite == C ? p2_c_objekte : p2_moo_objekte)(d->a, d->n);
        break;
    case BUILTIN: {
        double (*betrag)(double) = seite == C ? p2_c_betrag : p2_moo_betrag;
        double summe = 0.0;
        for (uint64_t i = 0; i < d->n; ++i)
            summe += betrag((double)(i % 100) - 50.0);
        *(double *)ausgabe = summe;
        break;
    }
    case RAUSCHEN:
        (seite == C ? p2_c_rauschen : p2_moo_rauschen)(
            d->a, ausgabe, d->seed, BREITE);
        break;
    }
}

static void vergleiche(const struct Daten *d) {
    if (memcmp(d->ausgabe[C], d->ausgabe[MOO], d->groesse) != 0) {
        fprintf(stderr, "%s: unterschiedliche C- und Moo-Ergebnisse\n", d->name);
        exit(EXIT_FAILURE);
    }
}

static uint64_t pruefsumme(const void *daten, size_t groesse) {
    const unsigned char *p = daten;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < groesse; ++i)
        hash = (hash ^ p[i]) * UINT64_C(1099511628211);
    return hash;
}

static void zeitreihe(const uint64_t zeiten[WIEDERHOLUNGEN]) {
    for (unsigned i = 0; i < WIEDERHOLUNGEN; ++i)
        printf("%s%" PRIu64, i ? "," : "", zeiten[i]);
}

static void messen(struct Daten *d) {
    const uint64_t aufbau_start = uhr_ns();
    vorbereiten(d);
    memset(d->ausgabe[C], 0xa5, d->groesse);
    memset(d->ausgabe[MOO], 0x5a, d->groesse);
    const uint64_t aufbau = uhr_ns() - aufbau_start;

    kern(d, C);
    kern(d, MOO);
    vergleiche(d);
    const uint64_t soll = pruefsumme(d->ausgabe[C], d->groesse);

    uint64_t zeiten[SEITEN][WIEDERHOLUNGEN];
    for (unsigned runde = 0; runde < WIEDERHOLUNGEN; ++runde) {
        for (unsigned index = 0; index < SEITEN; ++index) {
            const enum Seite seite = (runde + index) % SEITEN;
            const uint64_t start = uhr_ns();
            kern(d, seite);
            zeiten[seite][runde] = uhr_ns() - start;
        }
        vergleiche(d);
    }
    if (pruefsumme(d->ausgabe[C], d->groesse) != soll) {
        fprintf(stderr, "%s: Ergebnis änderte sich nach dem Aufwärmlauf\n", d->name);
        exit(EXIT_FAILURE);
    }

    printf("{\"last\":\"%s\",\"operationen\":%" PRIu64
           ",\"aufbau_ns\":%" PRIu64 ",\"ausgabe_bytes\":%zu"
           ",\"fnv1a64\":\"%016" PRIx64 "\",\"c_ns\":[",
           d->name, d->n, aufbau, d->groesse, soll);
    zeitreihe(zeiten[C]);
    fputs("],\"moo_ns\":[", stdout);
    zeitreihe(zeiten[MOO]);
    puts("]}");

    free(d->a);
    free(d->b);
    free(d->ausgabe[C]);
    free(d->ausgabe[MOO]);
}

int main(void) {
    struct Daten lasten[] = {
        {.last = SUMME, .name = "summe", .n = 10000000},
        {.last = VEKTOR, .name = "vektor", .n = 10000000},
        {.last = MATRIX, .name = "matrix", .n = 1000000},
        {.last = FUELLEN, .name = "fuellen", .n = 10000000},
        {.last = OBJEKTE, .name = "objekte", .n = 100000},
        {.last = BUILTIN, .name = "builtin", .n = 10000000},
        {.last = RAUSCHEN, .name = "rauschen", .n = BREITE * BREITE},
    };
    for (size_t i = 0; i < sizeof(lasten) / sizeof(lasten[0]); ++i)
        messen(&lasten[i]);
    return EXIT_SUCCESS;
}

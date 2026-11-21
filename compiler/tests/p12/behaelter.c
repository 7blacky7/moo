#define _POSIX_C_SOURCE 200112L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    void *daten;
    uint64_t groesse;
    uint64_t ausrichtung;
} Allokation;

static Allokation aktiv[8];
static bool sperre;
static uint64_t fehlerfaelle;

void p12_allokation_stoppen(bool stoppt) { sperre = stoppt; }

static uint8_t *anfordern(uint64_t kontext, uint64_t groesse, uint64_t ausrichtung) {
    assert(kontext == 0x5012 && groesse != 0);
    assert(ausrichtung && !(ausrichtung & (ausrichtung - 1)));
    if (sperre) {
        ++fehlerfaelle;
        return NULL;
    }
    void *daten = NULL;
    uint64_t abi_align = ausrichtung < sizeof(void *) ? sizeof(void *) : ausrichtung;
    if (posix_memalign(&daten, abi_align, groesse) != 0) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof aktiv / sizeof aktiv[0]; ++i) {
        if (aktiv[i].daten == NULL) {
            aktiv[i] = (Allokation){daten, groesse, ausrichtung};
            return daten;
        }
    }
    abort();
}

static void abgeben(uint64_t kontext, uint8_t *daten, uint64_t groesse, uint64_t ausrichtung) {
    assert(kontext == 0x5012 && daten != NULL);
    for (size_t i = 0; i < sizeof aktiv / sizeof aktiv[0]; ++i) {
        if (aktiv[i].daten == daten) {
            assert(aktiv[i].groesse == groesse && aktiv[i].ausrichtung == ausrichtung);
            aktiv[i] = (Allokation){0};
            free(daten);
            return;
        }
    }
    abort();
}

static uint64_t hashwert(uint64_t *k) { return *k * UINT64_C(11400714819323198485); }
static bool gleich(uint64_t *links, uint64_t *rechts) { return *links == *rechts; }

extern uint64_t p12_behaelter_probe(
    uint64_t,
    uint8_t *(*)(uint64_t, uint64_t, uint64_t),
    void (*)(uint64_t, uint8_t *, uint64_t, uint64_t),
    uint64_t (*)(uint64_t *),
    bool (*)(uint64_t *, uint64_t *)
);

int main(void) {
    uint64_t code = p12_behaelter_probe(0x5012, anfordern, abgeben, hashwert, gleich);
    if (code != 0) {
        fprintf(stderr, "Behälterprobe meldet %llu\n", (unsigned long long)code);
        return 1;
    }
    assert(fehlerfaelle == 3 && !sperre);
    for (size_t i = 0; i < sizeof aktiv / sizeof aktiv[0]; ++i) {
        assert(aktiv[i].daten == NULL);
    }
    puts("Tabelle 10000 Einträge, Liste, Ring und drei Allokationsfehler geprüft");
    return 0;
}

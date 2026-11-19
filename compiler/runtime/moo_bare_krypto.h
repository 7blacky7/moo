#ifndef MOO_BARE_KRYPTO_H
#define MOO_BARE_KRYPTO_H

#include "moo_krypto.h"
#include "monocypher/monocypher-ed25519.h"

/* SHA-512, HMAC-SHA512 und Ed25519: Monocypher 4.0.3, unveraenderter
 * Upstream ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f samt Lizenz
 * in monocypher/. Die uebrigen Byte-APIs kommen
 * aus moo_krypto.h. Ed25519 verwendet die reine RFC-8032-Variante. */

typedef enum {
    MOO_ENTROPY_UNSUPPORTED = 0,
    MOO_ENTROPY_OK = 1,
    MOO_ENTROPY_EXHAUSTED = 2,
    MOO_ENTROPY_REPEATED = 3,
} MooEntropyStatus;

typedef enum {
    MOO_ENTROPY_NONE = 0,
    MOO_ENTROPY_RDSEED = 1,
    MOO_ENTROPY_RDRAND = 2,
} MooEntropySource;

typedef struct {
    MooEntropyStatus rdseed;
    MooEntropyStatus rdrand;
    MooEntropySource source;
} MooEntropyReport;

typedef struct {
    uint8_t key[64];
    uint8_t value[64];
    unsigned requests_left;
} MooBareDrbg;

/* Der Aufrufer verwaltet einen Zustand pro Nutzer und synchronisiert Zugriffe.
 * Start prueft beide CPU-Quellen und instanziiert HMAC-DRBG(SHA-512).
 * Nach Fehler bleibt der Zustand geloescht. 0 = bereit, -1 = Fehler. */
int moo_bare_drbg_start(MooBareDrbg *rng, MooEntropyReport *report);

/* 1 bis 65536 Bytes je Aufruf, maximal 4096 Aufrufe je Instanziierung.
 * Bei -1 bleibt die Ausgabe unveraendert. Erneut starten zieht neues Seed-Material. */
int moo_bare_drbg_generate(MooBareDrbg *rng, uint8_t *out, size_t n);
void moo_bare_drbg_wipe(MooBareDrbg *rng);

/* Ergebnisbits der KATs und Quellenauswahl, keine Nutz-/Seed-Daten. */
uint64_t moo_bare_krypto_selbsttest(void);
uint64_t moo_bare_drbg_kat(void);
uint64_t moo_bare_entropie_selbsttest(void);

#endif

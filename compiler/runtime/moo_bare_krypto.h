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

/* Seed-Ausgabe fuer den Moo-DRBG. Ergebnisbits 0..3 und 4..7 sind
 * RDSEED-/RDRAND-Status, 8..11 enthalten die gewaehlte Quelle.
 * Ohne gueltige Quelle wird die Ausgabe geloescht. */
uint64_t moo_bare_entropie_seed(uint8_t seed[48]);

/* Nur Ergebnisbits, niemals Seed- oder Schluesseldaten. */
uint64_t moo_bare_entropie_kat(void);
uint64_t moo_bare_krypto_provider_kat(void);

#endif

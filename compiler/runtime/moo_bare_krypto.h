#ifndef MOO_BARE_KRYPTO_H
#define MOO_BARE_KRYPTO_H

#include "moo_krypto.h"
#include "monocypher/monocypher-ed25519.h"

/* SHA-512, HMAC-SHA512 und Ed25519: Monocypher 4.0.3, unveraenderter
 * Upstream ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f samt Lizenz
 * in monocypher/. Die uebrigen Byte-APIs kommen
 * aus moo_krypto.h. Ed25519 verwendet die reine RFC-8032-Variante. */

/* Primitive-KATs liefern nur Ergebnisbits. Die Protokolltests und die
 * CPU-Quellenpolitik werden im Moo-Kern ausgefuehrt. */
uint64_t moo_bare_krypto_provider_kat(void);
uint64_t moo_bare_mlkem_kat(void);

#endif

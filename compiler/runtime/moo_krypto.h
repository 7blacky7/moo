/* ============================================================
 * moo_krypto.h — Krypto-Kern (NETZK-1). Freestanding: nur
 * <stdint.h>/<stddef.h>. Bewusst eigener Header (nicht in
 * moo_runtime.h verwoben), damit das Modul 1:1 ins moOS wandern
 * kann. Ehrlichkeit + Einschraenkungen: siehe moo_krypto.c-Kopf.
 * ============================================================ */
#ifndef MOO_KRYPTO_H
#define MOO_KRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t h[8];
    uint64_t bits;
    uint8_t  buf[64];
    size_t   bufn;
} MooSha256Ctx;

typedef struct {
    uint32_t h[5];
    uint64_t bits;
    uint8_t  buf[64];
    size_t   bufn;
} MooSha1Ctx;

/* SHA-256 (FIPS 180-4): One-Shot + Streaming */
void moo_krypto_sha256(const uint8_t* daten, size_t n, uint8_t aus[32]);
void moo_krypto_sha256_init(MooSha256Ctx* c);
void moo_krypto_sha256_update(MooSha256Ctx* c, const uint8_t* daten, size_t n);
void moo_krypto_sha256_final(MooSha256Ctx* c, uint8_t aus[32]);

/* SHA-1 (FIPS 180-4): NUR fuer WPA2/802.11i-Kompatibilitaet (HMAC-SHA1
 * in der PTK/GTK-PRF). Kollisionsgebrochen — nicht fuer Neues. */
void moo_krypto_sha1(const uint8_t* daten, size_t n, uint8_t aus[20]);
void moo_krypto_sha1_init(MooSha1Ctx* c);
void moo_krypto_sha1_update(MooSha1Ctx* c, const uint8_t* daten, size_t n);
void moo_krypto_sha1_final(MooSha1Ctx* c, uint8_t aus[20]);

/* HMAC (RFC 2104) */
void moo_krypto_hmac_sha256(const uint8_t* key, size_t key_n,
                     const uint8_t* daten, size_t n, uint8_t aus[32]);
void moo_krypto_hmac_sha1(const uint8_t* key, size_t key_n,
                   const uint8_t* daten, size_t n, uint8_t aus[20]);

/* HKDF (RFC 5869, HMAC-SHA256). salz==NULL/0 => 32 Nullbytes.
 * Rueckgabe 0 = ok, -1 = okm_n > 255*32. */
void moo_krypto_hkdf_sha256_extract(const uint8_t* salz, size_t salz_n,
                             const uint8_t* ikm, size_t ikm_n,
                             uint8_t prk[32]);
int moo_krypto_hkdf_sha256(const uint8_t* salz, size_t salz_n,
                    const uint8_t* ikm, size_t ikm_n,
                    const uint8_t* info, size_t info_n,
                    uint8_t* okm, size_t okm_n);

/* ---------------------------------------------------------- AES (NETZK-2) --
 * FIPS 197 Blockcipher (128 & 256 Bit) + AEAD-Modi GCM (RFC 5116 /
 * NIST SP 800-38D, TLS-Pfad) und CCM (SP 800-38C, WPA2-CCMP-Vorleistung).
 * Freestanding, Zustand beim Aufrufer. Ehrlichkeit: siehe .c-Kopf
 * (Referenz-Implementierung, KEINE Konstant-Zeit-/Cache-Haertung —
 * Tabellen-AES ist cache-timing-angreifbar; fuer Produktion AES-NI/lib). */

typedef struct {
    uint32_t rk[60];   /* Rundenschluessel (max 14 Runden -> 60 Woerter) */
    int      runden;   /* 10 (AES-128) oder 14 (AES-256) */
} MooAesCtx;

/* key_bits = 128 oder 256. Rueckgabe 0 = ok, -1 = ungueltige Groesse. */
int  moo_krypto_aes_init(MooAesCtx* c, const uint8_t* key, int key_bits);
void moo_krypto_aes_encrypt_block(const MooAesCtx* c, const uint8_t in[16],
                           uint8_t out[16]);

/* AES-GCM (SP 800-38D). iv_n beliebig (12 Byte = schneller Standardpfad).
 * tag_n in {4..16}. Verify gibt 0 bei gueltigem Tag, -1 sonst (out dann
 * undefiniert/nicht verwenden). */
void moo_krypto_aes_gcm_encrypt(const MooAesCtx* c,
                         const uint8_t* iv, size_t iv_n,
                         const uint8_t* aad, size_t aad_n,
                         const uint8_t* pt, size_t pt_n,
                         uint8_t* ct, uint8_t* tag, size_t tag_n);
int  moo_krypto_aes_gcm_decrypt(const MooAesCtx* c,
                         const uint8_t* iv, size_t iv_n,
                         const uint8_t* aad, size_t aad_n,
                         const uint8_t* ct, size_t ct_n,
                         const uint8_t* tag, size_t tag_n,
                         uint8_t* pt);

/* AES-CCM (SP 800-38C). nonce_n in {7..13} (WPA2/CCMP nutzt 13),
 * tag_n in {4,6,8,10,12,14,16} (CCMP: 8). Verify wie bei GCM. */
void moo_krypto_aes_ccm_encrypt(const MooAesCtx* c,
                         const uint8_t* nonce, size_t nonce_n,
                         const uint8_t* aad, size_t aad_n,
                         const uint8_t* pt, size_t pt_n,
                         uint8_t* ct, uint8_t* tag, size_t tag_n);
int  moo_krypto_aes_ccm_decrypt(const MooAesCtx* c,
                         const uint8_t* nonce, size_t nonce_n,
                         const uint8_t* aad, size_t aad_n,
                         const uint8_t* ct, size_t ct_n,
                         const uint8_t* tag, size_t tag_n,
                         uint8_t* pt);

/* ------------------------------------ ChaCha20-Poly1305 (NETZK-3) --
 * RFC 8439. Freestanding, Zustand beim Aufrufer. Ehrlichkeit: siehe
 * moo_krypto_chacha.c-Kopf (rein arithmetisch, also ohne die
 * Cache-Timing-Schwaeche von Tabellen-AES; NICHT gegen Power-/EM-
 * Seitenkanaele gehaertet; Nonce-Eindeutigkeit ist Aufrufer-Pflicht). */

typedef struct {
    uint32_t r[5];      /* geklemmter Schluesselteil r, 5x26 Bit */
    uint32_t h[5];      /* Akkumulator, 5x26 Bit */
    uint32_t pad[4];    /* Schluesselteil s */
    uint8_t  buf[16];
    size_t   bufn;
    int      fertig;
} MooPoly1305Ctx;

/* ChaCha20 (2.3): ein 64-Byte-Block der Keystream-Funktion. */
void moo_krypto_chacha20_block(const uint8_t key[32], uint32_t zaehler,
                               const uint8_t nonce[12], uint8_t aus[64]);

/* ChaCha20 (2.4): Strom-XOR. ein == aus (in-place) ist erlaubt.
 * zaehler ist der Startwert des 32-Bit-Blockzaehlers. */
void moo_krypto_chacha20_xor(const uint8_t key[32], uint32_t zaehler,
                             const uint8_t nonce[12],
                             const uint8_t* ein, uint8_t* aus, size_t n);

/* Poly1305 (2.5): One-Shot + Streaming. Ein Schluessel darf nur EINMAL
 * verwendet werden — Poly1305 ist ein One-Time-Authenticator. */
void moo_krypto_poly1305(const uint8_t key[32], const uint8_t* daten,
                         size_t n, uint8_t tag[16]);
void moo_krypto_poly1305_init(MooPoly1305Ctx* c, const uint8_t key[32]);
void moo_krypto_poly1305_update(MooPoly1305Ctx* c,
                                const uint8_t* daten, size_t n);
void moo_krypto_poly1305_final(MooPoly1305Ctx* c, uint8_t tag[16]);

/* Einmal-Schluessel-Ableitung (2.6): ChaCha20-Block mit Zaehler 0. */
void moo_krypto_chacha20_poly1305_keygen(const uint8_t key[32],
                                         const uint8_t nonce[12],
                                         uint8_t otk[32]);

/* AEAD (2.8). Nutzdaten laufen ab Blockzaehler 1. ct braucht pt_n Byte.
 * Decrypt gibt 0 bei gueltigem Tag zurueck, -1 sonst; bei -1 wird pt
 * NICHT beschrieben (fail-closed). Tag-Vergleich ist konstant-zeitig. */
void moo_krypto_chacha20_poly1305_encrypt(const uint8_t key[32],
                                          const uint8_t nonce[12],
                                          const uint8_t* aad, size_t aad_n,
                                          const uint8_t* pt, size_t pt_n,
                                          uint8_t* ct, uint8_t tag[16]);
int  moo_krypto_chacha20_poly1305_decrypt(const uint8_t key[32],
                                          const uint8_t nonce[12],
                                          const uint8_t* aad, size_t aad_n,
                                          const uint8_t* ct, size_t ct_n,
                                          const uint8_t tag[16],
                                          uint8_t* pt);

/* ------------------------------------------------- X25519 (NETZK-3) --
 * RFC 7748. Der Skalar wird intern geklemmt, der Aufrufer muss das
 * nicht vorbereiten. Rueckgabe 0 = ok.
 * Rueckgabe -1 = Shared Secret ist komplett null (Punkt kleiner
 * Ordnung, RFC 7748 Abschnitt 6.1) — fail-closed, das Ergebnis DARF
 * dann nicht als Schluesselmaterial verwendet werden. aus ist auch im
 * Fehlerfall vollstaendig beschrieben (mit Nullen), nie uninitialisiert. */
int moo_krypto_x25519(uint8_t aus[32], const uint8_t skalar[32],
                      const uint8_t u_punkt[32]);

/* Oeffentlicher Schluessel = X25519(skalar, 9). */
int moo_krypto_x25519_basis(uint8_t aus[32], const uint8_t skalar[32]);

#ifdef __cplusplus
}
#endif

#endif /* MOO_KRYPTO_H */

#include "moo_runtime.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <string.h>
#include <stdlib.h>

/* ============================================================
 * Nativer AES-Backend (OpenSSL-EVP, AES-NI-beschleunigt).
 * Aktiv unter build.rs MOO_TLS... nein: MOO_AES_BACKEND=openssl
 * (build.rs definiert dann MOO_AES_NATIVE, wodurch die self-Impl
 * in moo_crypto.c ausgeblendet wird -> keine Symbol-Kollision).
 *
 * KRITISCH: byte-identisches Container-Format wie die self-Impl,
 * damit Blobs zwischen beiden Backends interoperabel sind:
 *   IV(16) || AES-256-CTR-ciphertext || HMAC-SHA256(32)
 *   mac_key = sha256(key);  HMAC ueber IV||ciphertext
 *   CTR: IV ist der 128-bit big-endian Initial-Counter
 * EVP_aes_256_ctr nutzt genau dieses BE-Counter-Schema.
 * ============================================================ */

/* CTR ist ein Stromchiffre: Ver- und Entschluesselung identisch. */
static int aes_native_ctr(const uint8_t key[32], const uint8_t iv[16],
                          const uint8_t *in, uint8_t *out, size_t len) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;
    int ok = 0, outl = 0;
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), NULL, key, iv) == 1) {
        if (len == 0 || EVP_EncryptUpdate(ctx, out, &outl, in, (int)len) == 1)
            ok = 1;
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok ? 0 : -1;
}

MooValue moo_aes_encrypt(MooValue key, MooValue plaintext) {
    if (key.tag != MOO_STRING) return moo_error("aes_verschluessle: schluessel (32-Byte String) erwartet");
    if (plaintext.tag != MOO_STRING) return moo_error("aes_verschluessle: klartext (String) erwartet");
    MooString *k = MV_STR(key);
    MooString *p = MV_STR(plaintext);
    if (k->length != 32) return moo_error("aes_verschluessle: schluessel muss genau 32 Byte sein");
    size_t pl = (size_t)p->length;
    size_t total = 16 + pl + 32;
    uint8_t *out = (uint8_t*)malloc(total);
    if (!out) return moo_error("aes_verschluessle: malloc fehlgeschlagen");
    if (RAND_bytes(out, 16) != 1) { free(out); return moo_error("aes_verschluessle: RNG fehlgeschlagen"); }
    uint8_t mac_key[32];
    SHA256((const unsigned char*)k->chars, 32, mac_key);
    if (aes_native_ctr((const uint8_t*)k->chars, out, (const uint8_t*)p->chars, out + 16, pl) != 0) {
        memset(mac_key, 0, sizeof mac_key); free(out);
        return moo_error("aes_verschluessle: EVP-Fehler");
    }
    unsigned int maclen = 32;
    HMAC(EVP_sha256(), mac_key, 32, out, (int)(16 + pl), out + 16 + pl, &maclen);
    MooValue result = moo_string_new_len((const char*)out, (int)total);
    memset(mac_key, 0, sizeof mac_key);
    free(out);
    return result;
}

MooValue moo_aes_decrypt(MooValue key, MooValue blob) {
    if (key.tag != MOO_STRING) return moo_error("aes_entschluessle: schluessel (32-Byte String) erwartet");
    if (blob.tag != MOO_STRING) return moo_error("aes_entschluessle: daten (String) erwartet");
    MooString *k = MV_STR(key);
    MooString *b = MV_STR(blob);
    if (k->length != 32) return moo_error("aes_entschluessle: schluessel muss genau 32 Byte sein");
    if (b->length < 16 + 32) return moo_error("aes_entschluessle: daten zu kurz oder beschaedigt");
    size_t cl = (size_t)b->length - 16 - 32;
    const uint8_t *iv  = (const uint8_t*)b->chars;
    const uint8_t *ct  = (const uint8_t*)b->chars + 16;
    const uint8_t *mac = (const uint8_t*)b->chars + 16 + cl;
    uint8_t mac_key[32], calc[32];
    unsigned int maclen = 32;
    SHA256((const unsigned char*)k->chars, 32, mac_key);
    HMAC(EVP_sha256(), mac_key, 32, (const unsigned char*)b->chars, (int)(16 + cl), calc, &maclen);
    if (CRYPTO_memcmp(calc, mac, 32) != 0) {
        memset(mac_key, 0, 32); memset(calc, 0, 32);
        return moo_error("aes_entschluessle: Authentifizierung fehlgeschlagen (falscher Schluessel oder manipulierte Daten)");
    }
    uint8_t *out = (uint8_t*)malloc(cl ? cl : 1);
    if (!out) { memset(mac_key, 0, 32); memset(calc, 0, 32); return moo_error("aes_entschluessle: malloc fehlgeschlagen"); }
    if (aes_native_ctr((const uint8_t*)k->chars, iv, ct, out, cl) != 0) {
        memset(mac_key, 0, 32); memset(calc, 0, 32); free(out);
        return moo_error("aes_entschluessle: EVP-Fehler");
    }
    MooValue result = moo_string_new_len((const char*)out, (int)cl);
    memset(mac_key, 0, sizeof mac_key);
    memset(calc, 0, sizeof calc);
    free(out);
    return result;
}

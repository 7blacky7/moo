#include "moo_bare_krypto.h"
#include "mlkem-native/mlkem_native.h"

#include <stdbool.h>
#include <string.h>

#include "moo_bare_ssh_mlkem_kat.inc"

enum {
    SSH_PACKET_MAX = 35000,
    SSH_TAG_BYTES = 16,
    SSH_KEY_BYTES = 64,
    SSH_AUTH_PREFIX_MAX = 128,
};

/* Der Moo-Orchestrator reserviert 14 u64 fuer einen SHA-256-Kontext und
 * verwendet die SSH-Drahtlaengen aus dem ML-KEM-768-Provider. */
_Static_assert(sizeof(MooSha256Ctx) == 112 && _Alignof(MooSha256Ctx) <= 8,
               "Moo SSH SHA-256 context ABI");
_Static_assert(MLKEM768_PUBLICKEYBYTES == 1184 && MLKEM768_CIPHERTEXTBYTES == 1088 &&
               MLKEM768_BYTES == 32 && MLKEM_SYMBYTES == 32, "Moo SSH ML-KEM-768 lengths");

static uint8_t *bytes_at(uint64_t address)
{
    return (uint8_t *)(uintptr_t)address;
}

static void put_u32be(uint8_t out[4], uint32_t n)
{
    for (unsigned i = 0; i < 4; i++)
        out[3 - i] = (uint8_t)(n >> (8 * i));
}

/* RFC 8731 3.1/3.2: X25519 liefert 32 Oktette. Fuer SSH bilden genau
 * diese Oktette den Big-Endian-Betrag des mpint (RFC 4251 5). */
size_t moo_bare_ssh_encode_k(uint8_t out[37], const uint8_t raw[32])
{
    size_t first = 0;
    while (first < 32 && raw[first] == 0)
        first++;
    size_t n = 32 - first;
    size_t pad = n != 0 && (raw[first] & 0x80) != 0;
    put_u32be(out, (uint32_t)(n + pad));
    if (pad)
        out[4] = 0;
    memcpy(out + 4 + pad, raw + first, n);
    return 4 + pad + n;
}

/* OpenSSH V_9_9_P2 cipher-chachapoly.c:48,82-112. Beim IETF-Block
 * belegt das erste Nonce-Wort die hohen 32 Bit des 64-Bit-Counters.
 * Alle SSH-Paketbloecke passen in das niedrige Counter-Wort. */
static void make_nonce(uint8_t nonce[12], uint64_t sequence)
{
    memset(nonce, 0, 12);
    for (unsigned i = 0; i < 8; i++)
        nonce[11 - i] = (uint8_t)(sequence >> (8 * i));
}

static void crypt_packet(uint8_t *packet, size_t n,
                         const uint8_t key[SSH_KEY_BYTES], const uint8_t nonce[12])
{
    moo_krypto_chacha20_xor(key + 32, 0, nonce, packet, packet, 4);
    moo_krypto_chacha20_xor(key, 1, nonce, packet + 4, packet + 4, n - 4);
}

uint64_t moo_bare_ssh_paketlaenge(const uint8_t key[SSH_KEY_BYTES],
                                 uint64_t address, uint64_t sequence)
{
    uint8_t nonce[12], length[4];
    if (sequence > UINT32_MAX)
        return 0;
    make_nonce(nonce, sequence);
    moo_krypto_chacha20_xor(key + 32, 0, nonce, bytes_at(address), length, 4);
    return ((uint64_t)length[0] << 24) | ((uint64_t)length[1] << 16) |
           ((uint64_t)length[2] << 8) | length[3];
}

uint64_t moo_bare_ssh_paket_seal(const uint8_t key[SSH_KEY_BYTES],
                                uint64_t address, uint64_t n, uint64_t sequence)
{
    if (n < 12 || n > SSH_PACKET_MAX - SSH_TAG_BYTES || sequence > UINT32_MAX)
        return 0;
    uint8_t nonce[12], poly_key[32];
    uint8_t *packet = bytes_at(address);
    make_nonce(nonce, sequence);
    moo_krypto_chacha20_poly1305_keygen(key, nonce, poly_key);
    crypt_packet(packet, n, key, nonce);
    moo_krypto_poly1305(poly_key, packet, n, packet + n);
    crypto_wipe(poly_key, sizeof(poly_key));
    return 1;
}

static bool open_with_key(uint8_t *packet, size_t n,
                          const uint8_t key[SSH_KEY_BYTES], const uint8_t nonce[12])
{
    uint8_t poly_key[32], tag[SSH_TAG_BYTES];
    moo_krypto_chacha20_poly1305_keygen(key, nonce, poly_key);
    moo_krypto_poly1305(poly_key, packet, n, tag);
    const bool ok = crypto_verify16(tag, packet + n) == 0;
    crypto_wipe(poly_key, sizeof(poly_key));
    crypto_wipe(tag, sizeof(tag));
    if (ok)
        crypt_packet(packet, n, key, nonce);
    return ok;
}

uint64_t moo_bare_ssh_paket_open(const uint8_t key[SSH_KEY_BYTES],
                                uint64_t address, uint64_t n, uint64_t sequence)
{
    if (n < 12 || n > SSH_PACKET_MAX - SSH_TAG_BYTES || sequence > UINT32_MAX)
        return 0;
    uint8_t nonce[12];
    make_nonce(nonce, sequence);
    return open_with_key(bytes_at(address), n, key, nonce);
}

typedef enum {
    SSH_CREDENTIAL_MISSING,
    SSH_CREDENTIAL_LOCKED,
    SSH_CREDENTIAL_ACTIVE,
} CredentialState;

static bool credential_key(CredentialState state,
                           const uint8_t *stored, size_t stored_n,
                           const uint8_t *offered, size_t offered_n)
{
    if (state != SSH_CREDENTIAL_ACTIVE || stored_n != 32 || offered_n != 32 ||
        !stored || !offered)
        return false;
    return crypto_verify32(stored, offered) == 0;
}

static bool credential_matches(CredentialState state,
                               const uint8_t *stored, size_t stored_n,
                               const uint8_t *offered, size_t offered_n,
                               size_t signature_n, int verification)
{
    if (signature_n != 64 || verification != 0)
        return false;
    return credential_key(state, stored, stored_n, offered, offered_n);
}

/* Der unsignierte Test kann nur PK_OK ausloesen. */
uint64_t moo_bare_ssh_key_erlaubt(uint64_t state, uint64_t stored, uint64_t offered)
{
    return state == SSH_CREDENTIAL_ACTIVE &&
           credential_key(SSH_CREDENTIAL_ACTIVE, bytes_at(stored), 32, bytes_at(offered), 32);
}

/* RFC 4252 7. Das signierte Praefix endet unmittelbar vor dem Signatur-String. */
static bool userauth(const uint8_t id[32], CredentialState state,
                     const uint8_t *stored, const uint8_t *offered,
                     const uint8_t *prefix, size_t prefix_n,
                     const uint8_t *signature, size_t signature_n)
{
    if (state != SSH_CREDENTIAL_ACTIVE || !stored || !offered || !prefix ||
        prefix_n == 0 || prefix_n > SSH_AUTH_PREFIX_MAX || !signature || signature_n != 64)
        return false;

    uint8_t signed_data[36 + SSH_AUTH_PREFIX_MAX];
    put_u32be(signed_data, 32);
    memcpy(signed_data + 4, id, 32);
    memcpy(signed_data + 36, prefix, prefix_n);
    const int rc = crypto_ed25519_check(signature, offered, signed_data, 36 + prefix_n);
    crypto_wipe(signed_data, sizeof(signed_data));
    return credential_matches(state, stored, 32, offered, 32, signature_n, rc);
}

uint64_t moo_bare_ssh_session_userauth(const uint8_t id[32],
                                      uint64_t state, uint64_t stored, uint64_t offered,
                                      uint64_t prefix, uint64_t prefix_n, uint64_t signature)
{
    return state == SSH_CREDENTIAL_ACTIVE &&
           userauth(id, SSH_CREDENTIAL_ACTIVE, bytes_at(stored), bytes_at(offered),
                    bytes_at(prefix), prefix_n, bytes_at(signature), 64);
}

static bool userauth_kat(void)
{
    const uint8_t id[32] = {0x4d}, other_session[32] = {0x53};
    const uint8_t prefix[] = "moo-auth-kat";
    uint8_t message[36 + sizeof(prefix) - 1] = {0, 0, 0, 32};
    uint8_t seed[32] = {0x73}, public[32], secret[64], signature[64], altered[64];

    memcpy(message + 4, id, sizeof(id));
    memcpy(message + 36, prefix, sizeof(prefix) - 1);
    crypto_ed25519_key_pair(secret, public, seed);
    crypto_ed25519_sign(signature, secret, message, sizeof(message));
    crypto_wipe(secret, sizeof(secret));
    crypto_wipe(seed, sizeof(seed));
    memcpy(altered, signature, sizeof(altered));
    altered[0] ^= 1;

    const size_t n = sizeof(prefix) - 1;
    return userauth(id, SSH_CREDENTIAL_ACTIVE, public, public, prefix, n, signature, 64) &&
           !userauth(id, SSH_CREDENTIAL_ACTIVE, public, public, prefix, n, altered, 64) &&
           !userauth(other_session, SSH_CREDENTIAL_ACTIVE, public, public, prefix, n, signature, 64) &&
           !userauth(id, SSH_CREDENTIAL_LOCKED, public, public, prefix, n, signature, 64) &&
           !userauth(id, SSH_CREDENTIAL_ACTIVE, public, public, prefix, n, signature, 65) &&
           !userauth(id, SSH_CREDENTIAL_ACTIVE, public, public, prefix, SIZE_MAX, signature, 64);
}

static bool credential_kat(void)
{
    uint8_t key[32] = {1}, altered[32] = {1}, large[257] = {1};
    const uint8_t sentinel[1] = {'*'};
    altered[31] = 1;
    return credential_matches(SSH_CREDENTIAL_ACTIVE, key, 32, key, 32, 64, 0) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, key, 32, altered, 32, 64, 0) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, key, 32, key, 31, 64, 0) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, key, 32, large, 72, 64, 0) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, key, 32, large, sizeof(large), 64, 0) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, large, sizeof(large), key, 32, 64, 0) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, key, 32, key, 32, 63, 0) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, key, 32, key, 32, 65, 0) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, key, 32, key, 32, SIZE_MAX, 0) &&
           !credential_matches(SSH_CREDENTIAL_LOCKED, sentinel, 1, sentinel, 1, 64, 0) &&
           !credential_matches(SSH_CREDENTIAL_LOCKED, key, 32, key, 32, 64, 0) &&
           !credential_matches(SSH_CREDENTIAL_MISSING, NULL, 0, NULL, 0, 64, 0) &&
           !credential_matches(SSH_CREDENTIAL_MISSING, key, 32, key, 32, 64, 0) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, sentinel, 1, sentinel, 1, 64, -1) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, key, 32, key, 32, 64, -1) &&
           !credential_matches(SSH_CREDENTIAL_ACTIVE, key, 32, key, 32, 64, 1);
}

static bool tag_kat(void)
{
    /* Lokale Testdaten fuer die echte Tag-Pruefung im Empfangspfad. */
    const uint8_t key[SSH_KEY_BYTES] = {0};
    const uint8_t nonce[12] = {0};
    const uint8_t plaintext[12] = {0, 0, 0, 8, 4, 1};
    uint8_t packet[sizeof(plaintext) + SSH_TAG_BYTES], copy[sizeof(packet)], poly_key[32];
    memcpy(packet, plaintext, sizeof(plaintext));
    crypt_packet(packet, sizeof(plaintext), key, nonce);
    moo_krypto_chacha20_poly1305_keygen(key, nonce, poly_key);
    moo_krypto_poly1305(poly_key, packet, sizeof(plaintext), packet + sizeof(plaintext));
    crypto_wipe(poly_key, sizeof(poly_key));

    packet[sizeof(packet) - 1] ^= 1;
    memcpy(copy, packet, sizeof(packet));
    bool rejected = !open_with_key(packet, sizeof(plaintext), key, nonce);
    for (size_t i = 0; i < sizeof(packet); i++)
        rejected = (packet[i] == copy[i]) && rejected;
    if (!rejected)
        return false;

    packet[sizeof(packet) - 1] ^= 1;
    if (!open_with_key(packet, sizeof(plaintext), key, nonce))
        return false;
    for (size_t i = 0; i < sizeof(plaintext); i++) {
        if (packet[i] != plaintext[i])
            return false;
    }
    return true;
}

static bool mlkem_kat(void)
{
    uint8_t ciphertext[MLKEM768_CIPHERTEXTBYTES], secret[MLKEM768_BYTES];

    bool ok = moo_mlkem768_enc_derand(ciphertext, secret, nist_tg2_tc26_ek,
                                     nist_tg2_tc26_m) == 0 &&
              crypto_verify32(secret, nist_tg2_tc26_k) == 0;
    if (ok) {
        unsigned difference = 0;
        for (size_t i = 0; i < sizeof(ciphertext); i++)
            difference |= ciphertext[i] ^ nist_tg2_tc26_c[i];

        ok = difference == 0 &&
             moo_mlkem768_check_pk(nist_tg10_tc137_ek) == MLK_ERR_INVALID_PK &&
             moo_mlkem768_enc_derand(ciphertext, secret, nist_tg10_tc137_ek,
                                    nist_tg2_tc26_m) == MLK_ERR_INVALID_PK;
    }
    crypto_wipe(secret, sizeof(secret));
    crypto_wipe(ciphertext, sizeof(ciphertext));
    return ok;
}

uint64_t moo_bare_ssh_sicherheit_pruefen(void)
{
    return (uint64_t)credential_kat() | ((uint64_t)tag_kat() << 1) |
           ((uint64_t)userauth_kat() << 2) | ((uint64_t)mlkem_kat() << 3);
}

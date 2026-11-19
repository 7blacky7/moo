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
    SSH_X25519_BYTES = 32,
    SSH_HYBRID_CLIENT_BYTES = MLKEM768_PUBLICKEYBYTES + SSH_X25519_BYTES,
    SSH_HYBRID_REPLY_BYTES = MLKEM768_CIPHERTEXTBYTES + SSH_X25519_BYTES,
};

/* Ein TCP-Slot. Hostkey und DRBG leben bis zum Ende des Laufprofils.
 * Transkript und Paketschluessel gehoeren zur jeweiligen Verbindung. */
static MooBareDrbg ssh_rng;
static uint8_t host_public[32];
static uint8_t host_secret[64];
static MooSha256Ctx exchange_hash;
static uint8_t send_key[SSH_KEY_BYTES], recv_key[SSH_KEY_BYTES];
static uint8_t session_id[32];
static bool boot_ready, keys_ready;

static uint8_t *bytes_at(uint64_t address)
{
    return (uint8_t *)(uintptr_t)address;
}

static void put_u32be(uint8_t out[4], uint32_t n)
{
    for (unsigned i = 0; i < 4; i++)
        out[3 - i] = (uint8_t)(n >> (8 * i));
}

static bool random_bytes(uint8_t *out, size_t size)
{
    if (ssh_rng.requests_left == 0) {
        MooEntropyReport report;
        if (moo_bare_drbg_start(&ssh_rng, &report) != 0)
            return false;
    }
    return moo_bare_drbg_generate(&ssh_rng, out, size) == 0;
}

uint64_t moo_bare_ssh_boot(void)
{
    if (boot_ready)
        return 1;
    uint8_t seed[32];
    if (!random_bytes(seed, sizeof(seed))) {
        crypto_wipe(seed, sizeof(seed));
        return 0;
    }
    crypto_ed25519_key_pair(host_secret, host_public, seed);
    crypto_wipe(seed, sizeof(seed));
    boot_ready = true;
    return 1;
}

uint64_t moo_bare_ssh_public(void)
{
    return (uint64_t)(uintptr_t)host_public;
}

uint64_t moo_bare_ssh_random(uint64_t out, uint64_t size)
{
    return boot_ready && random_bytes(bytes_at(out), size);
}

void moo_bare_ssh_clear(void)
{
    crypto_wipe(&exchange_hash, sizeof(exchange_hash));
    crypto_wipe(send_key, sizeof(send_key));
    crypto_wipe(recv_key, sizeof(recv_key));
    crypto_wipe(session_id, sizeof(session_id));
    keys_ready = false;
}

static void hash_string(MooSha256Ctx *ctx, const uint8_t *data, size_t size)
{
    uint8_t header[4];
    put_u32be(header, (uint32_t)size);
    moo_krypto_sha256_update(ctx, header, sizeof(header));
    moo_krypto_sha256_update(ctx, data, size);
}

void moo_bare_ssh_versions(uint64_t client, uint64_t client_len,
                           uint64_t server, uint64_t server_len)
{
    moo_bare_ssh_clear();
    moo_krypto_sha256_init(&exchange_hash);
    hash_string(&exchange_hash, bytes_at(client), client_len);
    hash_string(&exchange_hash, bytes_at(server), server_len);
}

void moo_bare_ssh_inits(uint64_t client, uint64_t client_len,
                        uint64_t server, uint64_t server_len)
{
    hash_string(&exchange_hash, bytes_at(client), client_len);
    hash_string(&exchange_hash, bytes_at(server), server_len);
}

/* RFC 8731 3.1/3.2: X25519 liefert 32 Oktette. Fuer SSH bilden genau
 * diese Oktette den Big-Endian-Betrag des mpint (RFC 4251 5). */
static size_t encode_k(uint8_t out[37], const uint8_t raw[32])
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

/* RFC 4253 7.2. H ist beim initialen Austausch auch der Session-Identifier. */
static void derive_key(uint8_t out[SSH_KEY_BYTES], const uint8_t *k, size_t k_len,
                       const uint8_t h[32], uint8_t letter)
{
    MooSha256Ctx ctx;
    moo_krypto_sha256_init(&ctx);
    moo_krypto_sha256_update(&ctx, k, k_len);
    moo_krypto_sha256_update(&ctx, h, 32);
    moo_krypto_sha256_update(&ctx, &letter, 1);
    moo_krypto_sha256_update(&ctx, h, 32);
    moo_krypto_sha256_final(&ctx, out);

    moo_krypto_sha256_init(&ctx);
    moo_krypto_sha256_update(&ctx, k, k_len);
    moo_krypto_sha256_update(&ctx, h, 32);
    moo_krypto_sha256_update(&ctx, out, 32);
    moo_krypto_sha256_final(&ctx, out + 32);
    crypto_wipe(&ctx, sizeof(ctx));
}

/* reply: server blob followed by a raw 64-byte Ed25519 signature.
 * Hybrid layout and transcript: draft-ietf-sshm-mlkem-hybrid-kex-10, 2.2–2.5. */
uint64_t moo_bare_ssh_exchange(uint64_t q_client, uint64_t client_n,
                               uint64_t host_blob, uint64_t blob_len, uint64_t reply)
{
    if (client_n != SSH_X25519_BYTES && client_n != SSH_HYBRID_CLIENT_BYTES)
        return 0;

    const bool hybrid = client_n == SSH_HYBRID_CLIENT_BYTES;
    const size_t server_n = hybrid ? SSH_HYBRID_REPLY_BYTES : SSH_X25519_BYTES;
    uint8_t scalar[SSH_X25519_BYTES], coins[MLKEM_SYMBYTES];
    uint8_t shared[MLKEM768_BYTES + SSH_X25519_BYTES], k[37], h[32];
    uint8_t *q_server = bytes_at(reply);
    const uint8_t *q_peer = bytes_at(q_client);
    const uint8_t *peer_x = q_peer + (client_n - SSH_X25519_BYTES);
    uint8_t *server_x = q_server + (server_n - SSH_X25519_BYTES);
    uint8_t *shared_x = shared + MLKEM768_BYTES;

    bool ok = boot_ready && random_bytes(scalar, sizeof(scalar)) &&
              moo_krypto_x25519_basis(server_x, scalar) == 0 &&
              moo_krypto_x25519(shared_x, scalar, peer_x) == 0;
    if (ok && hybrid) {
        /* The provider checks the public key modulus before encapsulating. */
        ok = random_bytes(coins, sizeof(coins)) &&
             moo_mlkem768_enc_derand(q_server, shared, q_peer, coins) == 0;
        if (ok) {
            /* SSH string containing SHA-256(K_PQ || K_CL). */
            put_u32be(k, 32);
            moo_krypto_sha256(shared, sizeof(shared), k + 4);
        }
    }
    if (ok) {
        const size_t k_len = hybrid ? 36 : encode_k(k, shared_x);
        hash_string(&exchange_hash, bytes_at(host_blob), blob_len);
        hash_string(&exchange_hash, q_peer, client_n);
        hash_string(&exchange_hash, q_server, server_n);
        moo_krypto_sha256_update(&exchange_hash, k, k_len);
        moo_krypto_sha256_final(&exchange_hash, h);
        memcpy(session_id, h, sizeof(session_id));
        crypto_ed25519_sign(q_server + server_n, host_secret, h, sizeof(h));
        derive_key(recv_key, k, k_len, h, 'C');
        derive_key(send_key, k, k_len, h, 'D');
        keys_ready = true;
    }
    crypto_wipe(scalar, sizeof(scalar));
    crypto_wipe(coins, sizeof(coins));
    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(k, sizeof(k));
    crypto_wipe(h, sizeof(h));
    crypto_wipe(&exchange_hash, sizeof(exchange_hash));
    return ok;
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

uint64_t moo_bare_ssh_length(uint64_t address, uint64_t sequence)
{
    uint8_t nonce[12], length[4];
    if (!keys_ready || sequence > UINT32_MAX)
        return 0;
    make_nonce(nonce, sequence);
    moo_krypto_chacha20_xor(recv_key + 32, 0, nonce, bytes_at(address), length, 4);
    return ((uint64_t)length[0] << 24) | ((uint64_t)length[1] << 16) |
           ((uint64_t)length[2] << 8) | length[3];
}

uint64_t moo_bare_ssh_seal(uint64_t address, uint64_t n, uint64_t sequence)
{
    if (!keys_ready || n < 12 || n > SSH_PACKET_MAX - SSH_TAG_BYTES || sequence > UINT32_MAX)
        return 0;
    uint8_t nonce[12], poly_key[32];
    uint8_t *packet = bytes_at(address);
    make_nonce(nonce, sequence);
    moo_krypto_chacha20_poly1305_keygen(send_key, nonce, poly_key);
    crypt_packet(packet, n, send_key, nonce);
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

uint64_t moo_bare_ssh_open(uint64_t address, uint64_t n, uint64_t sequence)
{
    if (!keys_ready || n < 12 || n > SSH_PACKET_MAX - SSH_TAG_BYTES || sequence > UINT32_MAX)
        return 0;
    uint8_t nonce[12];
    make_nonce(nonce, sequence);
    return open_with_key(bytes_at(address), n, recv_key, nonce);
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

uint64_t moo_bare_ssh_userauth(uint64_t state, uint64_t stored, uint64_t offered,
                              uint64_t prefix, uint64_t prefix_n, uint64_t signature)
{
    return keys_ready && state == SSH_CREDENTIAL_ACTIVE &&
           userauth(session_id, SSH_CREDENTIAL_ACTIVE, bytes_at(stored), bytes_at(offered),
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

#include "moo_bare_krypto.h"

#include <stdbool.h>
#include <string.h>

enum {
    SSH_PACKET_MAX = 35000,
    SSH_TAG_BYTES = 16,
    SSH_KEY_BYTES = 64,
};

/* Ein TCP-Slot. Hostkey und DRBG leben bis zum Ende des Laufprofils.
 * Transkript und Paketschluessel gehoeren zur jeweiligen Verbindung. */
static MooBareDrbg ssh_rng;
static uint8_t host_public[32];
static uint8_t host_secret[64];
static MooSha256Ctx exchange_hash;
static uint8_t send_key[SSH_KEY_BYTES], recv_key[SSH_KEY_BYTES];
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

/* reply enthaelt Q_S (32 Bytes) und die rohe Ed25519-Signatur (64 Bytes). */
uint64_t moo_bare_ssh_exchange(uint64_t q_client, uint64_t host_blob,
                               uint64_t blob_len, uint64_t reply)
{
    uint8_t scalar[32], shared[32], k[37], h[32];
    uint8_t *q_server = bytes_at(reply);
    const uint8_t *q_peer = bytes_at(q_client);
    bool ok = boot_ready && random_bytes(scalar, sizeof(scalar)) &&
              moo_krypto_x25519_basis(q_server, scalar) == 0 &&
              moo_krypto_x25519(shared, scalar, q_peer) == 0;
    if (ok) {
        const size_t k_len = encode_k(k, shared);
        hash_string(&exchange_hash, bytes_at(host_blob), blob_len);
        hash_string(&exchange_hash, q_peer, 32);
        hash_string(&exchange_hash, q_server, 32);
        moo_krypto_sha256_update(&exchange_hash, k, k_len);
        moo_krypto_sha256_final(&exchange_hash, h);
        crypto_ed25519_sign(q_server + 32, host_secret, h, sizeof(h));
        derive_key(recv_key, k, k_len, h, 'C');
        derive_key(send_key, k, k_len, h, 'D');
        keys_ready = true;
    }
    crypto_wipe(scalar, sizeof(scalar));
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

/* Vertragsprobe fuer die anschliessende Public-Key-Anmeldung. Der
 * Status der Signaturpruefung ist getrennt von Schluessel-Bytes. */
typedef enum {
    SSH_CREDENTIAL_MISSING,
    SSH_CREDENTIAL_LOCKED,
    SSH_CREDENTIAL_ACTIVE,
} CredentialState;

static bool credential_matches(CredentialState state,
                               const uint8_t *stored, size_t stored_n,
                               const uint8_t *offered, size_t offered_n,
                               size_t signature_n, int verification)
{
    if (state != SSH_CREDENTIAL_ACTIVE || stored_n != 32 || offered_n != 32 ||
        signature_n != 64 || verification != 0 || !stored || !offered)
        return false;
    return crypto_verify32(stored, offered) == 0;
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

uint64_t moo_bare_ssh_sicherheit_pruefen(void)
{
    return (uint64_t)credential_kat() | ((uint64_t)tag_kat() << 1);
}

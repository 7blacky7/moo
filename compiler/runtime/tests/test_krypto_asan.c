/* ============================================================
 * test_krypto_asan.c — NETZK-1-Gates gegen OFFIZIELLE Vektoren:
 *   SHA-256 / SHA-1 : FIPS 180-4 (inkl. Streaming + 1M-'a')
 *   HMAC-SHA256     : RFC 4231 (TC1, TC2, TC3, TC6 Langkey)
 *   HMAC-SHA1       : RFC 2202 (TC1, TC2)
 *   HKDF-SHA256     : RFC 5869 (TC1, TC3 salzlos, Negativ zu lang)
 * Standalone: linkt NUR moo_krypto.c (freestanding-Beweis nebenbei, kein moo-Runtime-Linkset).
 * Lauf: clang -fsanitize=address test_krypto_asan.c ../moo_krypto.c
 * ============================================================ */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "moo_krypto.h"

static int checks = 0;
static int fails = 0;
#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; fprintf(stderr, "FAIL: %s (Zeile %d)\n", msg, __LINE__); } \
} while (0)

/* Hex-String -> Bytes (Laenge = strlen/2) */
static void unhex(const char* h, uint8_t* aus) {
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        aus[i] = (uint8_t)v;
    }
}

static bool gleich_hex(const uint8_t* ist, const char* soll_hex, size_t n) {
    uint8_t soll[128];
    unhex(soll_hex, soll);
    return memcmp(ist, soll, n) == 0;
}

int main(void) {
    uint8_t aus[128];

    /* ===== 1. SHA-256 (FIPS 180-4) ===== */
    moo_krypto_sha256((const uint8_t*)"abc", 3, aus);
    CHECK(gleich_hex(aus,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 32),
        "sha256: 'abc'");

    moo_krypto_sha256((const uint8_t*)"", 0, aus);
    CHECK(gleich_hex(aus,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 32),
        "sha256: leer");

    const char* zwei = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    moo_krypto_sha256((const uint8_t*)zwei, strlen(zwei), aus);
    CHECK(gleich_hex(aus,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", 32),
        "sha256: Zweiblock-Vektor");

    /* Streaming == One-Shot (krumme Chunks ueber Blockgrenzen) */
    {
        MooSha256Ctx c;
        uint8_t s[32];
        moo_krypto_sha256_init(&c);
        moo_krypto_sha256_update(&c, (const uint8_t*)zwei, 7);
        moo_krypto_sha256_update(&c, (const uint8_t*)zwei + 7, 13);
        moo_krypto_sha256_update(&c, (const uint8_t*)zwei + 20, strlen(zwei) - 20);
        moo_krypto_sha256_final(&c, s);
        CHECK(memcmp(s, aus, 32) == 0, "sha256: Streaming == One-Shot");
    }

    /* 1.000.000 x 'a' (FIPS-Langvektor, erzwingt viele Bloecke) */
    {
        MooSha256Ctx c;
        uint8_t block_a[1000];
        memset(block_a, 'a', sizeof block_a);
        moo_krypto_sha256_init(&c);
        for (int i = 0; i < 1000; i++) moo_krypto_sha256_update(&c, block_a, 1000);
        moo_krypto_sha256_final(&c, aus);
        CHECK(gleich_hex(aus,
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", 32),
            "sha256: 1M x 'a' (Streaming)");
    }

    /* ===== 2. SHA-1 (FIPS 180-4 — nur WPA2-Vorleistung) ===== */
    moo_krypto_sha1((const uint8_t*)"abc", 3, aus);
    CHECK(gleich_hex(aus, "a9993e364706816aba3e25717850c26c9cd0d89d", 20),
        "sha1: 'abc'");

    moo_krypto_sha1((const uint8_t*)"", 0, aus);
    CHECK(gleich_hex(aus, "da39a3ee5e6b4b0d3255bfef95601890afd80709", 20),
        "sha1: leer");

    moo_krypto_sha1((const uint8_t*)zwei, strlen(zwei), aus);
    CHECK(gleich_hex(aus, "84983e441c3bd26ebaae4aa1f95129e5e54670f1", 20),
        "sha1: Zweiblock-Vektor");

    /* ===== 3. HMAC-SHA256 (RFC 4231) ===== */
    {
        uint8_t key[131];
        /* TC1: key = 20 x 0x0b, msg = "Hi There" */
        memset(key, 0x0b, 20);
        moo_krypto_hmac_sha256(key, 20, (const uint8_t*)"Hi There", 8, aus);
        CHECK(gleich_hex(aus,
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", 32),
            "hmac-sha256: RFC4231 TC1");

        /* TC2: key = "Jefe" */
        moo_krypto_hmac_sha256((const uint8_t*)"Jefe", 4,
            (const uint8_t*)"what do ya want for nothing?", 28, aus);
        CHECK(gleich_hex(aus,
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32),
            "hmac-sha256: RFC4231 TC2");

        /* TC3: key = 20 x 0xaa, msg = 50 x 0xdd */
        uint8_t msg[50];
        memset(key, 0xaa, 20);
        memset(msg, 0xdd, 50);
        moo_krypto_hmac_sha256(key, 20, msg, 50, aus);
        CHECK(gleich_hex(aus,
            "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe", 32),
            "hmac-sha256: RFC4231 TC3");

        /* TC6: 131-Byte-Key (> Blockgroesse => Key wird erst gehasht) */
        memset(key, 0xaa, 131);
        const char* m6 = "Test Using Larger Than Block-Size Key - Hash Key First";
        moo_krypto_hmac_sha256(key, 131, (const uint8_t*)m6, strlen(m6), aus);
        CHECK(gleich_hex(aus,
            "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54", 32),
            "hmac-sha256: RFC4231 TC6 (Langkey)");
    }

    /* ===== 4. HMAC-SHA1 (RFC 2202) ===== */
    {
        uint8_t key[20];
        memset(key, 0x0b, 20);
        moo_krypto_hmac_sha1(key, 20, (const uint8_t*)"Hi There", 8, aus);
        CHECK(gleich_hex(aus, "b617318655057264e28bc0b6fb378c8ef146be00", 20),
            "hmac-sha1: RFC2202 TC1");

        moo_krypto_hmac_sha1((const uint8_t*)"Jefe", 4,
            (const uint8_t*)"what do ya want for nothing?", 28, aus);
        CHECK(gleich_hex(aus, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", 20),
            "hmac-sha1: RFC2202 TC2");
    }

    /* ===== 5. HKDF-SHA256 (RFC 5869) ===== */
    {
        /* TC1 */
        uint8_t ikm[22], salz[13], info[10], okm[82];
        memset(ikm, 0x0b, 22);
        for (int i = 0; i < 13; i++) salz[i] = (uint8_t)i;          /* 000102..0c */
        for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i); /* f0f1..f9 */
        uint8_t prk[32];
        moo_krypto_hkdf_sha256_extract(salz, 13, ikm, 22, prk);
        CHECK(gleich_hex(prk,
            "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", 32),
            "hkdf: RFC5869 TC1 PRK");
        int r = moo_krypto_hkdf_sha256(salz, 13, ikm, 22, info, 10, okm, 42);
        CHECK(r == 0 && gleich_hex(okm,
            "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
            "34007208d5b887185865", 42),
            "hkdf: RFC5869 TC1 OKM(42)");

        /* TC3: salzlos, ohne Info */
        r = moo_krypto_hkdf_sha256(NULL, 0, ikm, 22, (const uint8_t*)"", 0, okm, 42);
        CHECK(r == 0 && gleich_hex(okm,
            "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
            "9d201395faa4b61a96c8", 42),
            "hkdf: RFC5869 TC3 (salzlos)");

        /* NEGATIV: okm_n > 255*HashLen wird abgelehnt */
        r = moo_krypto_hkdf_sha256(NULL, 0, ikm, 22, (const uint8_t*)"", 0, okm, 255u*32u + 1u);
        CHECK(r == -1, "hkdf: NEGATIV — okm zu lang wird abgelehnt");
    }

    if (fails == 0)
        printf("test_krypto_asan: alle %d Checks bestanden\n", checks);
    else
        printf("test_krypto_asan: %d/%d FAILS\n", fails, checks);
    return fails == 0 ? 0 : 1;
}

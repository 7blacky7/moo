/* ============================================================
 * test_krypto_aes_asan.c — NETZK-2-Gates gegen OFFIZIELLE Vektoren:
 *   AES-128/256 Block : FIPS 197 Anhang B/C
 *   AES-GCM           : NIST SP 800-38D / GCM-Spec Test Case 2,3,4
 *   AES-CCM           : NIST SP 800-38C Anhang C Beispiel 1 + 2
 *   Roundtrip + Tag-Manipulation (Negativ) fuer GCM und CCM
 * Standalone: linkt NUR moo_krypto_aes.c + moo_krypto.c.
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

static void unhex(const char* h, uint8_t* aus) {
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned v; sscanf(h + 2 * i, "%2x", &v); aus[i] = (uint8_t)v;
    }
}
static bool gleich_hex(const uint8_t* ist, const char* soll_hex, size_t n) {
    uint8_t soll[256];
    unhex(soll_hex, soll);
    return memcmp(ist, soll, n) == 0;
}

int main(void) {
    uint8_t key[32], in[64], out[64], tag[16];

    /* ===== 1. AES-128 Block (FIPS 197 Anhang C.1) ===== */
    {
        MooAesCtx c;
        unhex("000102030405060708090a0b0c0d0e0f", key);
        unhex("00112233445566778899aabbccddeeff", in);
        CHECK(moo_krypto_aes_init(&c, key, 128) == 0, "aes128: init");
        moo_krypto_aes_encrypt_block(&c, in, out);
        CHECK(gleich_hex(out, "69c4e0d86a7b0430d8cdb78070b4c55a", 16),
              "aes128: FIPS197 C.1 Block");
    }

    /* ===== 2. AES-256 Block (FIPS 197 Anhang C.3) ===== */
    {
        MooAesCtx c;
        unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key);
        unhex("00112233445566778899aabbccddeeff", in);
        CHECK(moo_krypto_aes_init(&c, key, 256) == 0, "aes256: init");
        moo_krypto_aes_encrypt_block(&c, in, out);
        CHECK(gleich_hex(out, "8ea2b7ca516745bfeafc49904b496089", 16),
              "aes256: FIPS197 C.3 Block");
    }

    /* Negativ: ungueltige Keygroesse */
    {
        MooAesCtx c;
        CHECK(moo_krypto_aes_init(&c, key, 192) == -1, "aes: NEGATIV ungueltige Keygroesse");
    }

    /* ===== 3. AES-128-GCM (SP 800-38D Test Case 3: 64 Byte PT, kein AAD) ===== */
    {
        MooAesCtx c;
        uint8_t iv[12], pt[64], ct[64];
        unhex("feffe9928665731c6d6a8f9467308308", key);
        unhex("cafebabefacedbaddecaf888", iv);
        unhex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
              "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255", pt);
        moo_krypto_aes_init(&c, key, 128);
        moo_krypto_aes_gcm_encrypt(&c, iv, 12, NULL, 0, pt, 64, ct, tag, 16);
        CHECK(gleich_hex(ct,
            "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
            "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f5985", 64),
            "aes128-gcm: TC3 Ciphertext");
        CHECK(gleich_hex(tag, "4d5c2af327cd64a62cf35abd2ba6fab4", 16),
            "aes128-gcm: TC3 Tag");
    }

    /* ===== 4. AES-128-GCM (Test Case 4: 60 Byte PT + 20 Byte AAD) ===== */
    {
        MooAesCtx c;
        uint8_t iv[12], pt[60], aad[20], ct[60];
        unhex("feffe9928665731c6d6a8f9467308308", key);
        unhex("cafebabefacedbaddecaf888", iv);
        unhex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
              "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39", pt);
        unhex("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad);
        moo_krypto_aes_init(&c, key, 128);
        moo_krypto_aes_gcm_encrypt(&c, iv, 12, aad, 20, pt, 60, ct, tag, 16);
        CHECK(gleich_hex(ct,
            "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e"
            "21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091", 60),
            "aes128-gcm: TC4 Ciphertext (mit AAD)");
        CHECK(gleich_hex(tag, "5bc94fbc3221a5db94fae95ae7121a47", 16),
            "aes128-gcm: TC4 Tag");

        /* Roundtrip: decrypt verifiziert Tag + stellt PT wieder her */
        uint8_t rec[60];
        int r = moo_krypto_aes_gcm_decrypt(&c, iv, 12, aad, 20, ct, 60, tag, 16, rec);
        CHECK(r == 0 && memcmp(rec, pt, 60) == 0, "aes128-gcm: Roundtrip decrypt");

        /* NEGATIV: gekipptes Tag-Bit wird abgelehnt */
        uint8_t badtag[16];
        memcpy(badtag, tag, 16); badtag[0] ^= 0x01;
        r = moo_krypto_aes_gcm_decrypt(&c, iv, 12, aad, 20, ct, 60, badtag, 16, rec);
        CHECK(r == -1, "aes128-gcm: NEGATIV Tag-Manipulation abgelehnt");
    }

    /* ===== 5. AES-128-CCM (SP 800-38C Anhang C Beispiel 1) ===== */
    /* K=404142..4f, N=10111213141516 (7), A=0001020304050607 (8),
     * P=20212223 (4), T-Laenge 4. Erwartet CT=7162015b, Tag=4dac255d */
    {
        MooAesCtx c;
        uint8_t nonce[7], aad[8], pt[4], ct[4];
        unhex("404142434445464748494a4b4c4d4e4f", key);
        unhex("10111213141516", nonce);
        unhex("0001020304050607", aad);
        unhex("20212223", pt);
        moo_krypto_aes_init(&c, key, 128);
        moo_krypto_aes_ccm_encrypt(&c, nonce, 7, aad, 8, pt, 4, ct, tag, 4);
        CHECK(gleich_hex(ct, "7162015b", 4), "aes128-ccm: C.1 Ciphertext");
        CHECK(gleich_hex(tag, "4dac255d", 4), "aes128-ccm: C.1 Tag");

        uint8_t rec[4];
        int r = moo_krypto_aes_ccm_decrypt(&c, nonce, 7, aad, 8, ct, 4, tag, 4, rec);
        CHECK(r == 0 && memcmp(rec, pt, 4) == 0, "aes128-ccm: Roundtrip decrypt");
        uint8_t badtag[4];
        memcpy(badtag, tag, 4); badtag[0] ^= 0x80;
        r = moo_krypto_aes_ccm_decrypt(&c, nonce, 7, aad, 8, ct, 4, badtag, 4, rec);
        CHECK(r == -1, "aes128-ccm: NEGATIV Tag-Manipulation abgelehnt");
    }

    /* ===== 6. AES-128-CCM (SP 800-38C Anhang C Beispiel 2) ===== */
    /* N=1011..17 (8), A=0..0f (16), P=20..2f (16), T-Laenge 6.
     * Erwartet CT=d2a1f0e051ea5f62081a7792073d593d, Tag=1fc64fbfaccd */
    {
        MooAesCtx c;
        uint8_t nonce[8], aad[16], pt[16], ct[16];
        unhex("404142434445464748494a4b4c4d4e4f", key);
        unhex("1011121314151617", nonce);
        unhex("000102030405060708090a0b0c0d0e0f", aad);
        unhex("202122232425262728292a2b2c2d2e2f", pt);
        moo_krypto_aes_init(&c, key, 128);
        moo_krypto_aes_ccm_encrypt(&c, nonce, 8, aad, 16, pt, 16, ct, tag, 6);
        CHECK(gleich_hex(ct, "d2a1f0e051ea5f62081a7792073d593d", 16),
              "aes128-ccm: C.2 Ciphertext");
        CHECK(gleich_hex(tag, "1fc64fbfaccd", 6), "aes128-ccm: C.2 Tag");
    }

    if (fails == 0)
        printf("test_krypto_aes_asan: alle %d Checks bestanden\n", checks);
    else
        printf("test_krypto_aes_asan: %d/%d FAILS\n", fails, checks);
    return fails == 0 ? 0 : 1;
}

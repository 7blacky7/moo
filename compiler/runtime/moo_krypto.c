/* ============================================================
 * moo_krypto.c — Krypto-Kern (NETZK-1): SHA-256, SHA-1, HMAC, HKDF.
 *
 * FREESTANDING-FAEHIG (Moos Vorgabe fuer das zukuenftige moOS):
 * keine OS-Aufrufe, kein malloc, nur <stdint.h>/<stddef.h>/<string.h>
 * (memcpy/memset — im moOS durch die eigene Mini-libc gedeckt).
 * Alle Zustaende liegen beim Aufrufer (Ctx-Structs / Ausgabepuffer).
 *
 * EHRLICHKEIT: Lern-/Referenz-Primitiven, verifiziert gegen offizielle
 * Testvektoren (FIPS 180-4, RFC 2202, RFC 4231, RFC 5869), ABER ohne
 * Seitenkanal-Haertung (kein konstantes Timing garantiert, kein
 * Speicher-Zeroisieren-Erzwingen ueber Compiler-Barrieren). Fuer
 * produktive Geheimnisse auf Fremd-Hardware eine gepruefte Bibliothek
 * linken. SHA-1 ist kollisionsgebrochen und hier NUR enthalten, weil
 * WPA2 (802.11i) HMAC-SHA1 in der PTK/GTK-Ableitung verlangt —
 * fuer neue Protokolle SHA-256 nutzen.
 *
 * Getrennt vom KI-Zweig (kein Bezug zu moo_nn/moo_quant).
 * ============================================================ */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "moo_krypto.h"

/* ---------------------------------------------------------- SHA-256 -- */

static const uint32_t K256[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,
    0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,
    0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,
    0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,
    0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,
    0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8a1u,0xa81a664bu,
    0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,
    0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,
    0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

static uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(MooSha256Ctx* c, const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4*i] << 24) | ((uint32_t)p[4*i+1] << 16) |
               ((uint32_t)p[4*i+2] << 8) | (uint32_t)p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i-15], 7) ^ rotr32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr32(w[i-2], 17) ^ rotr32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
    uint32_t e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}

void moo_krypto_sha256_init(MooSha256Ctx* c) {
    c->h[0] = 0x6a09e667u; c->h[1] = 0xbb67ae85u; c->h[2] = 0x3c6ef372u;
    c->h[3] = 0xa54ff53au; c->h[4] = 0x510e527fu; c->h[5] = 0x9b05688cu;
    c->h[6] = 0x1f83d9abu; c->h[7] = 0x5be0cd19u;
    c->bits = 0; c->bufn = 0;
}

void moo_krypto_sha256_update(MooSha256Ctx* c, const uint8_t* d, size_t n) {
    c->bits += (uint64_t)n * 8u;
    while (n > 0) {
        size_t frei = 64 - c->bufn;
        size_t k = (n < frei) ? n : frei;
        memcpy(c->buf + c->bufn, d, k);
        c->bufn += k; d += k; n -= k;
        if (c->bufn == 64) { sha256_block(c, c->buf); c->bufn = 0; }
    }
}

void moo_krypto_sha256_final(MooSha256Ctx* c, uint8_t aus[32]) {
    uint64_t bits = c->bits;
    uint8_t pad = 0x80;
    moo_krypto_sha256_update(c, &pad, 1);
    c->bits -= 8;  /* Padding zaehlt nicht zur Laenge */
    uint8_t null = 0;
    while (c->bufn != 56) { moo_krypto_sha256_update(c, &null, 1); c->bits -= 8; }
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    moo_krypto_sha256_update(c, len, 8);
    for (int i = 0; i < 8; i++) {
        aus[4*i]   = (uint8_t)(c->h[i] >> 24);
        aus[4*i+1] = (uint8_t)(c->h[i] >> 16);
        aus[4*i+2] = (uint8_t)(c->h[i] >> 8);
        aus[4*i+3] = (uint8_t)(c->h[i]);
    }
}

void moo_krypto_sha256(const uint8_t* d, size_t n, uint8_t aus[32]) {
    MooSha256Ctx c;
    moo_krypto_sha256_init(&c);
    moo_krypto_sha256_update(&c, d, n);
    moo_krypto_sha256_final(&c, aus);
}

/* ------------------------------------------------------------ SHA-1 -- */
/* NUR fuer WPA2/802.11i (HMAC-SHA1 in der PRF) — nicht fuer Neues. */

static void sha1_block(MooSha1Ctx* c, const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4*i] << 24) | ((uint32_t)p[4*i+1] << 16) |
               ((uint32_t)p[4*i+2] << 8) | (uint32_t)p[4*i+3];
    for (int i = 16; i < 80; i++) {
        uint32_t x = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
        w[i] = (x << 1) | (x >> 31);
    }
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & cc) | ((~b) & d);          k = 0x5a827999u; }
        else if (i < 40) { f = b ^ cc ^ d;                     k = 0x6ed9eba1u; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d);  k = 0x8f1bbcdcu; }
        else             { f = b ^ cc ^ d;                     k = 0xca62c1d6u; }
        uint32_t t = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d; d = cc; cc = (b << 30) | (b >> 2); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

void moo_krypto_sha1_init(MooSha1Ctx* c) {
    c->h[0] = 0x67452301u; c->h[1] = 0xefcdab89u; c->h[2] = 0x98badcfeu;
    c->h[3] = 0x10325476u; c->h[4] = 0xc3d2e1f0u;
    c->bits = 0; c->bufn = 0;
}

void moo_krypto_sha1_update(MooSha1Ctx* c, const uint8_t* d, size_t n) {
    c->bits += (uint64_t)n * 8u;
    while (n > 0) {
        size_t frei = 64 - c->bufn;
        size_t k = (n < frei) ? n : frei;
        memcpy(c->buf + c->bufn, d, k);
        c->bufn += k; d += k; n -= k;
        if (c->bufn == 64) { sha1_block(c, c->buf); c->bufn = 0; }
    }
}

void moo_krypto_sha1_final(MooSha1Ctx* c, uint8_t aus[20]) {
    uint64_t bits = c->bits;
    uint8_t pad = 0x80;
    moo_krypto_sha1_update(c, &pad, 1);
    uint8_t null = 0;
    while (c->bufn != 56) moo_krypto_sha1_update(c, &null, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
    moo_krypto_sha1_update(c, len, 8);
    for (int i = 0; i < 5; i++) {
        aus[4*i]   = (uint8_t)(c->h[i] >> 24);
        aus[4*i+1] = (uint8_t)(c->h[i] >> 16);
        aus[4*i+2] = (uint8_t)(c->h[i] >> 8);
        aus[4*i+3] = (uint8_t)(c->h[i]);
    }
}

void moo_krypto_sha1(const uint8_t* d, size_t n, uint8_t aus[20]) {
    MooSha1Ctx c;
    moo_krypto_sha1_init(&c);
    moo_krypto_sha1_update(&c, d, n);
    moo_krypto_sha1_final(&c, aus);
}

/* ------------------------------------------------------------- HMAC -- */
/* RFC 2104: H((K^opad) || H((K^ipad) || msg)), Blockgroesse 64 fuer
 * SHA-1 und SHA-256. Generisch ueber einen kleinen Dispatch statt
 * Funktionszeigern — freestanding-schlicht. */

void moo_krypto_hmac_sha256(const uint8_t* key, size_t kn,
                     const uint8_t* d, size_t n, uint8_t aus[32]) {
    uint8_t k0[64];
    memset(k0, 0, 64);
    if (kn > 64) moo_krypto_sha256(key, kn, k0);   /* lange Keys erst hashen */
    else memcpy(k0, key, kn);
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = (uint8_t)(k0[i] ^ 0x36);
        opad[i] = (uint8_t)(k0[i] ^ 0x5c);
    }
    MooSha256Ctx c;
    uint8_t inner[32];
    moo_krypto_sha256_init(&c);
    moo_krypto_sha256_update(&c, ipad, 64);
    moo_krypto_sha256_update(&c, d, n);
    moo_krypto_sha256_final(&c, inner);
    moo_krypto_sha256_init(&c);
    moo_krypto_sha256_update(&c, opad, 64);
    moo_krypto_sha256_update(&c, inner, 32);
    moo_krypto_sha256_final(&c, aus);
}

void moo_krypto_hmac_sha1(const uint8_t* key, size_t kn,
                   const uint8_t* d, size_t n, uint8_t aus[20]) {
    uint8_t k0[64];
    memset(k0, 0, 64);
    if (kn > 64) moo_krypto_sha1(key, kn, k0);
    else memcpy(k0, key, kn);
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = (uint8_t)(k0[i] ^ 0x36);
        opad[i] = (uint8_t)(k0[i] ^ 0x5c);
    }
    MooSha1Ctx c;
    uint8_t inner[20];
    moo_krypto_sha1_init(&c);
    moo_krypto_sha1_update(&c, ipad, 64);
    moo_krypto_sha1_update(&c, d, n);
    moo_krypto_sha1_final(&c, inner);
    moo_krypto_sha1_init(&c);
    moo_krypto_sha1_update(&c, opad, 64);
    moo_krypto_sha1_update(&c, inner, 20);
    moo_krypto_sha1_final(&c, aus);
}

/* ------------------------------------------------------------- HKDF -- */
/* RFC 5869 auf HMAC-SHA256. extract: PRK = HMAC(salz, ikm);
 * expand: T(i) = HMAC(PRK, T(i-1) || info || i). okm_n <= 255*32.
 * Rueckgabe 0 = ok, -1 = okm_n zu gross. */

void moo_krypto_hkdf_sha256_extract(const uint8_t* salz, size_t sn,
                             const uint8_t* ikm, size_t in,
                             uint8_t prk[32]) {
    /* RFC 5869: fehlendes Salz == 32 Nullbytes (HashLen) */
    uint8_t leer[32];
    if (salz == NULL || sn == 0) { memset(leer, 0, 32); salz = leer; sn = 32; }
    moo_krypto_hmac_sha256(salz, sn, ikm, in, prk);
}

int moo_krypto_hkdf_sha256(const uint8_t* salz, size_t sn,
                    const uint8_t* ikm, size_t in,
                    const uint8_t* info, size_t infon,
                    uint8_t* okm, size_t okm_n) {
    if (okm_n > 255u * 32u) return -1;
    uint8_t prk[32];
    moo_krypto_hkdf_sha256_extract(salz, sn, ikm, in, prk);
    uint8_t t[32];
    size_t tn = 0;
    size_t pos = 0;
    uint8_t zaehler = 1;
    while (pos < okm_n) {
        /* HMAC(prk, T || info || zaehler) haendisch als Streaming:
         * inner = SHA256(ipad || T || info || z), aus = SHA256(opad||inner) */
        uint8_t k0[64], ipad[64], opad[64], inner[32];
        memset(k0, 0, 64);
        memcpy(k0, prk, 32);
        for (int i = 0; i < 64; i++) {
            ipad[i] = (uint8_t)(k0[i] ^ 0x36);
            opad[i] = (uint8_t)(k0[i] ^ 0x5c);
        }
        MooSha256Ctx c;
        moo_krypto_sha256_init(&c);
        moo_krypto_sha256_update(&c, ipad, 64);
        moo_krypto_sha256_update(&c, t, tn);
        moo_krypto_sha256_update(&c, info, infon);
        moo_krypto_sha256_update(&c, &zaehler, 1);
        moo_krypto_sha256_final(&c, inner);
        moo_krypto_sha256_init(&c);
        moo_krypto_sha256_update(&c, opad, 64);
        moo_krypto_sha256_update(&c, inner, 32);
        moo_krypto_sha256_final(&c, t);
        tn = 32;
        size_t k = (okm_n - pos < 32) ? (okm_n - pos) : 32;
        memcpy(okm + pos, t, k);
        pos += k;
        zaehler++;
    }
    return 0;
}

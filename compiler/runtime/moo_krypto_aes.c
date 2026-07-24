/* ============================================================
 * moo_krypto_aes.c — AES (FIPS 197) + AEAD-Modi GCM & CCM (NETZK-2).
 *
 * FREESTANDING wie moo_krypto.c: nur <stdint.h>/<stddef.h>/<string.h>,
 * kein malloc, kein OS. Zustand (MooAesCtx / Ausgabepuffer) beim Aufrufer.
 *
 * EHRLICHKEIT: Referenz-Implementierung mit S-Box-Tabellen. Das ist
 * CACHE-TIMING-ANGREIFBAR (die Standard-Warnung fuer Tabellen-AES) und
 * NICHT konstant-zeit. GCMs GF(2^128)-Multiplikation ist eine simple
 * Bit-Schleife (langsam, aber tabellenfrei = kein zusaetzlicher
 * Timing-Kanal). Fuer Produktion auf Fremd-Hardware AES-NI/PCLMUL oder
 * eine gepruefte Bibliothek. Verifiziert gegen FIPS-197- und
 * NIST-SP-800-38C/D-Vektoren. Getrennt vom KI-Zweig.
 * ============================================================ */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "moo_krypto.h"

/* ---------------------------------------------------------- AES-Kern -- */

static const uint8_t SBOX[256] = {
  0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
  0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
  0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
  0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
  0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
  0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
  0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
  0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
  0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
  0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
  0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
  0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
  0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
  0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
  0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
  0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t RCON[11] = {
  0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

static uint32_t sub_word(uint32_t w) {
    return ((uint32_t)SBOX[(w >> 24) & 0xff] << 24) |
           ((uint32_t)SBOX[(w >> 16) & 0xff] << 16) |
           ((uint32_t)SBOX[(w >> 8)  & 0xff] << 8)  |
           ((uint32_t)SBOX[w & 0xff]);
}
static uint32_t rot_word(uint32_t w) { return (w << 8) | (w >> 24); }

int moo_krypto_aes_init(MooAesCtx* c, const uint8_t* key, int key_bits) {
    int nk;
    if (key_bits == 128) { nk = 4;  c->runden = 10; }
    else if (key_bits == 256) { nk = 8; c->runden = 14; }
    else return -1;
    int woerter = 4 * (c->runden + 1);
    for (int i = 0; i < nk; i++)
        c->rk[i] = ((uint32_t)key[4*i] << 24) | ((uint32_t)key[4*i+1] << 16) |
                   ((uint32_t)key[4*i+2] << 8) | (uint32_t)key[4*i+3];
    for (int i = nk; i < woerter; i++) {
        uint32_t t = c->rk[i-1];
        if (i % nk == 0)
            t = sub_word(rot_word(t)) ^ ((uint32_t)RCON[i/nk] << 24);
        else if (nk > 6 && i % nk == 4)
            t = sub_word(t);
        c->rk[i] = c->rk[i-nk] ^ t;
    }
    return 0;
}

/* GF(2^8) xtime + Multiplikation fuer MixColumns */
static uint8_t xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b));
}
static uint8_t gmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (int i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        uint8_t hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0x1b;
        b >>= 1;
    }
    return p;
}

void moo_krypto_aes_encrypt_block(const MooAesCtx* c, const uint8_t in[16],
                           uint8_t out[16]) {
    uint8_t s[16];
    memcpy(s, in, 16);
    /* AddRoundKey 0 */
    for (int i = 0; i < 4; i++) {
        uint32_t k = c->rk[i];
        s[4*i]   ^= (uint8_t)(k >> 24);
        s[4*i+1] ^= (uint8_t)(k >> 16);
        s[4*i+2] ^= (uint8_t)(k >> 8);
        s[4*i+3] ^= (uint8_t)k;
    }
    for (int r = 1; r <= c->runden; r++) {
        uint8_t t[16];
        /* SubBytes + ShiftRows (Spalten-orientiertes Layout: s[col*4+row]) */
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                int src_col = (col + row) & 3;
                t[col*4 + row] = SBOX[s[src_col*4 + row]];
            }
        }
        if (r < c->runden) {
            /* MixColumns */
            for (int col = 0; col < 4; col++) {
                uint8_t a0 = t[col*4], a1 = t[col*4+1],
                        a2 = t[col*4+2], a3 = t[col*4+3];
                s[col*4]   = (uint8_t)(xtime(a0) ^ (xtime(a1) ^ a1) ^ a2 ^ a3);
                s[col*4+1] = (uint8_t)(a0 ^ xtime(a1) ^ (xtime(a2) ^ a2) ^ a3);
                s[col*4+2] = (uint8_t)(a0 ^ a1 ^ xtime(a2) ^ (xtime(a3) ^ a3));
                s[col*4+3] = (uint8_t)((xtime(a0) ^ a0) ^ a1 ^ a2 ^ xtime(a3));
            }
        } else {
            memcpy(s, t, 16);
        }
        /* AddRoundKey r */
        for (int i = 0; i < 4; i++) {
            uint32_t k = c->rk[r*4 + i];
            s[4*i]   ^= (uint8_t)(k >> 24);
            s[4*i+1] ^= (uint8_t)(k >> 16);
            s[4*i+2] ^= (uint8_t)(k >> 8);
            s[4*i+3] ^= (uint8_t)k;
        }
    }
    memcpy(out, s, 16);
    (void)gmul;  /* gmul nur fuer evtl. Decrypt-Ausbau; hier ungenutzt */
}

/* ------------------------------------------------------------ CTR -- */
/* Gemeinsamer Zaehler-Modus-Kern fuer GCM & CCM. blk wird als 128-Bit-
 * Big-Endian-Zaehler behandelt (Inkrement der letzten 32 Bit fuer GCM,
 * volle 128 Bit fuer CCM — hier: letzte counter_bytes Bytes). */
static void ctr_xor(const MooAesCtx* c, uint8_t ctr[16],
                    const uint8_t* in, uint8_t* out, size_t n,
                    int inc_bytes) {
    uint8_t ks[16];
    size_t pos = 0;
    while (pos < n) {
        moo_krypto_aes_encrypt_block(c, ctr, ks);
        size_t k = (n - pos < 16) ? (n - pos) : 16;
        for (size_t i = 0; i < k; i++) out[pos + i] = in[pos + i] ^ ks[i];
        pos += k;
        /* Zaehler inkrementieren (die letzten inc_bytes Bytes) */
        for (int i = 15; i >= 16 - inc_bytes; i--) {
            if (++ctr[i] != 0) break;
        }
    }
}

/* ------------------------------------------------------------ GCM -- */
/* GHASH ueber GF(2^128), Polynom x^128 + x^7 + x^2 + x + 1.
 * Bit-serielle Multiplikation (tabellenfrei). */

static void gf_mul(uint8_t x[16], const uint8_t h[16]) {
    uint8_t z[16];
    memset(z, 0, 16);
    uint8_t v[16];
    memcpy(v, h, 16);
    for (int i = 0; i < 128; i++) {
        int byte = i >> 3, bit = 7 - (i & 7);
        if ((x[byte] >> bit) & 1)
            for (int j = 0; j < 16; j++) z[j] ^= v[j];
        /* v = v>>1, bei Uebertrag XOR R (0xe1 im hoechsten Byte) */
        uint8_t lsb = v[15] & 1;
        for (int j = 15; j > 0; j--) v[j] = (uint8_t)((v[j] >> 1) | (v[j-1] << 7));
        v[0] >>= 1;
        if (lsb) v[0] ^= 0xe1;
    }
    memcpy(x, z, 16);
}

static void ghash(const uint8_t h[16], const uint8_t* aad, size_t aad_n,
                  const uint8_t* ct, size_t ct_n, uint8_t out[16]) {
    uint8_t y[16];
    memset(y, 0, 16);
    /* AAD in 16er-Bloecken */
    size_t pos = 0;
    while (pos < aad_n) {
        size_t k = (aad_n - pos < 16) ? (aad_n - pos) : 16;
        for (size_t i = 0; i < k; i++) y[i] ^= aad[pos + i];
        gf_mul(y, h);
        pos += k;
    }
    pos = 0;
    while (pos < ct_n) {
        size_t k = (ct_n - pos < 16) ? (ct_n - pos) : 16;
        for (size_t i = 0; i < k; i++) y[i] ^= ct[pos + i];
        gf_mul(y, h);
        pos += k;
    }
    /* Laengenblock: aad_bits (64) || ct_bits (64), big-endian */
    uint64_t ab = (uint64_t)aad_n * 8u, cb = (uint64_t)ct_n * 8u;
    uint8_t len[16];
    for (int i = 0; i < 8; i++) len[i]   = (uint8_t)(ab >> (56 - 8*i));
    for (int i = 0; i < 8; i++) len[8+i] = (uint8_t)(cb >> (56 - 8*i));
    for (int i = 0; i < 16; i++) y[i] ^= len[i];
    gf_mul(y, h);
    memcpy(out, y, 16);
}

static void gcm_j0(const MooAesCtx* c, const uint8_t h[16],
                   const uint8_t* iv, size_t iv_n, uint8_t j0[16]) {
    if (iv_n == 12) {
        memcpy(j0, iv, 12);
        j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    } else {
        /* J0 = GHASH_H(IV || 0-pad || len(IV)) */
        memset(j0, 0, 16);
        uint8_t y[16];
        memset(y, 0, 16);
        size_t pos = 0;
        while (pos < iv_n) {
            size_t k = (iv_n - pos < 16) ? (iv_n - pos) : 16;
            for (size_t i = 0; i < k; i++) y[i] ^= iv[pos + i];
            gf_mul(y, h);
            pos += k;
        }
        uint64_t ib = (uint64_t)iv_n * 8u;
        uint8_t len[16];
        memset(len, 0, 16);
        for (int i = 0; i < 8; i++) len[8+i] = (uint8_t)(ib >> (56 - 8*i));
        for (int i = 0; i < 16; i++) y[i] ^= len[i];
        gf_mul(y, h);
        memcpy(j0, y, 16);
    }
    (void)c;
}

void moo_krypto_aes_gcm_encrypt(const MooAesCtx* c,
                         const uint8_t* iv, size_t iv_n,
                         const uint8_t* aad, size_t aad_n,
                         const uint8_t* pt, size_t pt_n,
                         uint8_t* ct, uint8_t* tag, size_t tag_n) {
    uint8_t h[16], zero[16];
    memset(zero, 0, 16);
    moo_krypto_aes_encrypt_block(c, zero, h);          /* H = E_K(0^128) */
    uint8_t j0[16];
    gcm_j0(c, h, iv, iv_n, j0);
    /* CTR ab (J0 + 1) ueber den Klartext */
    uint8_t ctr[16];
    memcpy(ctr, j0, 16);
    for (int i = 15; i >= 12; i--) { if (++ctr[i] != 0) break; }
    ctr_xor(c, ctr, pt, ct, pt_n, 4);
    /* Tag = GHASH ^ E_K(J0) */
    uint8_t s[16], ej0[16];
    ghash(h, aad, aad_n, ct, pt_n, s);
    moo_krypto_aes_encrypt_block(c, j0, ej0);
    for (int i = 0; i < 16; i++) s[i] ^= ej0[i];
    memcpy(tag, s, tag_n);
}

int moo_krypto_aes_gcm_decrypt(const MooAesCtx* c,
                        const uint8_t* iv, size_t iv_n,
                        const uint8_t* aad, size_t aad_n,
                        const uint8_t* ct, size_t ct_n,
                        const uint8_t* tag, size_t tag_n,
                        uint8_t* pt) {
    uint8_t h[16], zero[16];
    memset(zero, 0, 16);
    moo_krypto_aes_encrypt_block(c, zero, h);
    uint8_t j0[16];
    gcm_j0(c, h, iv, iv_n, j0);
    /* Tag zuerst pruefen (ueber den Ciphertext) */
    uint8_t s[16], ej0[16];
    ghash(h, aad, aad_n, ct, ct_n, s);
    moo_krypto_aes_encrypt_block(c, j0, ej0);
    for (int i = 0; i < 16; i++) s[i] ^= ej0[i];
    uint8_t diff = 0;
    for (size_t i = 0; i < tag_n; i++) diff |= (uint8_t)(s[i] ^ tag[i]);
    if (diff != 0) return -1;
    /* Tag ok -> entschluesseln */
    uint8_t ctr[16];
    memcpy(ctr, j0, 16);
    for (int i = 15; i >= 12; i--) { if (++ctr[i] != 0) break; }
    ctr_xor(c, ctr, ct, pt, ct_n, 4);
    return 0;
}

/* ------------------------------------------------------------ CCM -- */
/* SP 800-38C: CBC-MAC ueber formatierte Bloecke (B0 + AAD + Payload),
 * dann CTR-Verschluesselung (A0 fuer Tag, A1.. fuer Daten).
 * L = 15 - nonce_n. */

static void ccm_format_b0(const uint8_t* nonce, size_t nonce_n,
                          size_t aad_n, size_t pt_n, size_t tag_n,
                          uint8_t b0[16]) {
    int L = 15 - (int)nonce_n;
    uint8_t flags = (uint8_t)((aad_n > 0 ? 0x40 : 0) |
                    (((tag_n - 2) / 2) << 3) | (L - 1));
    b0[0] = flags;
    memcpy(b0 + 1, nonce, nonce_n);
    /* Payload-Laenge big-endian in die letzten L Bytes */
    size_t q = pt_n;
    for (int i = 15; i >= 16 - L; i--) { b0[i] = (uint8_t)(q & 0xff); q >>= 8; }
}

static void ccm_mac(const MooAesCtx* c,
                    const uint8_t* nonce, size_t nonce_n,
                    const uint8_t* aad, size_t aad_n,
                    const uint8_t* pt, size_t pt_n, size_t tag_n,
                    uint8_t t[16]) {
    uint8_t x[16], b[16];
    uint8_t b0[16];
    ccm_format_b0(nonce, nonce_n, aad_n, pt_n, tag_n, b0);
    moo_krypto_aes_encrypt_block(c, b0, x);             /* X1 = E(B0) */
    /* AAD-Bloecke: erst 2-Byte-Laengenprefix (fuer aad_n < 2^16-2^8) */
    if (aad_n > 0) {
        memset(b, 0, 16);
        size_t off = 0;
        b[0] = (uint8_t)(aad_n >> 8);
        b[1] = (uint8_t)(aad_n & 0xff);
        size_t fill = (aad_n < 14) ? aad_n : 14;
        memcpy(b + 2, aad, fill);
        for (int i = 0; i < 16; i++) x[i] ^= b[i];
        moo_krypto_aes_encrypt_block(c, x, x);
        off = fill;
        while (off < aad_n) {
            memset(b, 0, 16);
            size_t k = (aad_n - off < 16) ? (aad_n - off) : 16;
            memcpy(b, aad + off, k);
            for (int i = 0; i < 16; i++) x[i] ^= b[i];
            moo_krypto_aes_encrypt_block(c, x, x);
            off += k;
        }
    }
    /* Payload-Bloecke */
    size_t off = 0;
    while (off < pt_n) {
        memset(b, 0, 16);
        size_t k = (pt_n - off < 16) ? (pt_n - off) : 16;
        memcpy(b, pt + off, k);
        for (int i = 0; i < 16; i++) x[i] ^= b[i];
        moo_krypto_aes_encrypt_block(c, x, x);
        off += k;
    }
    memcpy(t, x, 16);
}

static void ccm_ctr0(const uint8_t* nonce, size_t nonce_n, uint8_t a[16]) {
    int L = 15 - (int)nonce_n;
    a[0] = (uint8_t)(L - 1);
    memcpy(a + 1, nonce, nonce_n);
    for (int i = 16 - L; i < 16; i++) a[i] = 0;   /* Zaehler = 0 */
}

void moo_krypto_aes_ccm_encrypt(const MooAesCtx* c,
                         const uint8_t* nonce, size_t nonce_n,
                         const uint8_t* aad, size_t aad_n,
                         const uint8_t* pt, size_t pt_n,
                         uint8_t* ct, uint8_t* tag, size_t tag_n) {
    uint8_t t[16];
    ccm_mac(c, nonce, nonce_n, aad, aad_n, pt, pt_n, tag_n, t);
    /* S0 = E(A0) verschluesselt den Tag */
    uint8_t a0[16], s0[16];
    ccm_ctr0(nonce, nonce_n, a0);
    moo_krypto_aes_encrypt_block(c, a0, s0);
    for (size_t i = 0; i < tag_n; i++) tag[i] = t[i] ^ s0[i];
    /* Daten mit A1.. (Zaehler ab 1) */
    uint8_t a[16];
    memcpy(a, a0, 16);
    int L = 15 - (int)nonce_n;
    for (int i = 15; i >= 16 - L; i--) { if (++a[i] != 0) break; }
    ctr_xor(c, a, pt, ct, pt_n, L);
}

int moo_krypto_aes_ccm_decrypt(const MooAesCtx* c,
                        const uint8_t* nonce, size_t nonce_n,
                        const uint8_t* aad, size_t aad_n,
                        const uint8_t* ct, size_t ct_n,
                        const uint8_t* tag, size_t tag_n,
                        uint8_t* pt) {
    /* Erst Daten entschluesseln (CTR ab A1), dann MAC pruefen */
    uint8_t a0[16], a[16];
    ccm_ctr0(nonce, nonce_n, a0);
    memcpy(a, a0, 16);
    int L = 15 - (int)nonce_n;
    for (int i = 15; i >= 16 - L; i--) { if (++a[i] != 0) break; }
    ctr_xor(c, a, ct, pt, ct_n, L);
    /* MAC ueber den Klartext */
    uint8_t t[16], s0[16];
    ccm_mac(c, nonce, nonce_n, aad, aad_n, pt, ct_n, tag_n, t);
    moo_krypto_aes_encrypt_block(c, a0, s0);
    uint8_t diff = 0;
    for (size_t i = 0; i < tag_n; i++)
        diff |= (uint8_t)((t[i] ^ s0[i]) ^ tag[i]);
    if (diff != 0) { memset(pt, 0, ct_n); return -1; }
    return 0;
}

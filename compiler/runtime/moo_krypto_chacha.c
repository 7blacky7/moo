/* ============================================================
 * moo_krypto_chacha.c — ChaCha20 + Poly1305 + AEAD (NETZK-3).
 * RFC 8439 (loest RFC 7539 ab).
 *
 * FREESTANDING wie moo_krypto.c / moo_krypto_aes.c: nur
 * <stdint.h>/<stddef.h> ueber moo_krypto.h, kein malloc, kein OS,
 * kein libc-Aufruf. Aller Zustand liegt beim Aufrufer, damit das
 * Modul unveraendert ins moOS wandern kann.
 *
 * EHRLICHKEIT / GRENZEN (bitte lesen, bevor das irgendwo scharf laeuft):
 *  - Referenz-Implementierung. ChaCha20 und Poly1305 arbeiten rein
 *    arithmetisch ohne Nachschlagetabellen und haben damit NICHT die
 *    Cache-Timing-Schwaeche von Tabellen-AES (siehe moo_krypto_aes.c).
 *  - Der Tag-Vergleich beim Entschluesseln ist konstant-zeitig
 *    (XOR-Akkumulation, kein frueher Abbruch, kein memcmp).
 *  - NICHT gehaertet gegen Power-/EM-Seitenkanaele.
 *  - Nonce-Wiederverwendung zerstoert die Sicherheit vollstaendig —
 *    wie bei jedem Stream-AEAD. Eindeutigkeit ist Pflicht des Aufrufers
 *    und kann hier technisch nicht geprueft werden.
 *  - Bei fehlgeschlagener Verifikation wird der Klartext NICHT
 *    herausgegeben (fail-closed, siehe _decrypt).
 * ============================================================ */
#include "moo_krypto.h"

/* ------------------------------------------------------------- Helfer -- */

static uint32_t kr_lade32le(const uint8_t* p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void kr_speichere32le(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)( v        & 0xFFu);
    p[1] = (uint8_t)((v >>  8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* Linksrotation. Wrap ist gewollt und auf unsigned definiert.
 * n liegt hier immer in {7,8,12,16} — nie 0 und nie 32, damit ist die
 * Gegenschiebung (32 - n) ebenfalls immer im gueltigen Bereich. */
static uint32_t kr_rotl32(uint32_t v, int n) {
    return (uint32_t)((v << n) | (v >> (32 - n)));
}

/* ==================================================== ChaCha20 (2.3) == */

#define KR_QR(a, b, c, d) do {                        \
    (a) += (b); (d) ^= (a); (d) = kr_rotl32((d), 16); \
    (c) += (d); (b) ^= (c); (b) = kr_rotl32((b), 12); \
    (a) += (b); (d) ^= (a); (d) = kr_rotl32((d),  8); \
    (c) += (d); (b) ^= (c); (b) = kr_rotl32((b),  7); \
} while (0)

void moo_krypto_chacha20_block(const uint8_t key[32], uint32_t zaehler,
                               const uint8_t nonce[12], uint8_t aus[64]) {
    uint32_t s[16], x[16];
    int i;

    /* "expand 32-byte k" als vier Little-Endian-Woerter (RFC 8439 2.3) */
    s[0] = 0x61707865u; s[1] = 0x3320646eu;
    s[2] = 0x79622d32u; s[3] = 0x6b206574u;
    for (i = 0; i < 8; i++) s[4 + i]  = kr_lade32le(key + 4 * i);
    s[12] = zaehler;
    for (i = 0; i < 3; i++) s[13 + i] = kr_lade32le(nonce + 4 * i);

    for (i = 0; i < 16; i++) x[i] = s[i];

    /* 20 Runden = 10x (4 Spalten-Runden + 4 Diagonal-Runden) */
    for (i = 0; i < 10; i++) {
        KR_QR(x[0], x[4], x[ 8], x[12]);
        KR_QR(x[1], x[5], x[ 9], x[13]);
        KR_QR(x[2], x[6], x[10], x[14]);
        KR_QR(x[3], x[7], x[11], x[15]);
        KR_QR(x[0], x[5], x[10], x[15]);
        KR_QR(x[1], x[6], x[11], x[12]);
        KR_QR(x[2], x[7], x[ 8], x[13]);
        KR_QR(x[3], x[4], x[ 9], x[14]);
    }

    /* Addition ist absichtlicher 32-Bit-Wrap (unsigned, definiert) */
    for (i = 0; i < 16; i++) kr_speichere32le(aus + 4 * i, x[i] + s[i]);
}

void moo_krypto_chacha20_xor(const uint8_t key[32], uint32_t zaehler,
                             const uint8_t nonce[12],
                             const uint8_t* ein, uint8_t* aus, size_t n) {
    uint8_t ks[64];
    size_t i = 0;

    while (i < n) {
        size_t rest = n - i;
        size_t m = (rest < 64) ? rest : 64;
        size_t j;

        moo_krypto_chacha20_block(key, zaehler, nonce, ks);
        for (j = 0; j < m; j++) aus[i + j] = (uint8_t)(ein[i + j] ^ ks[j]);

        i += m;
        /* 32-Bit-Wrap ist definiert (unsigned). RFC 8439 begrenzt eine
         * Nonce ohnehin auf 2^32 Bloecke = 256 GiB; darueber hinaus
         * duerfte der Aufrufer die Nonce nicht wiederverwenden. */
        zaehler++;
    }

    for (i = 0; i < 64; i++) ks[i] = 0;
}

/* =================================================== Poly1305 (2.5) == */
/* 130-Bit-Akkumulator in fuenf 26-Bit-Gliedern. Alles unsigned, alle
 * Zwischenprodukte in uint64_t — kein signed Overflow moeglich. */

void moo_krypto_poly1305_init(MooPoly1305Ctx* c, const uint8_t key[32]) {
    int i;

    /* r wird geklemmt ("clamp", RFC 8439 2.5): bestimmte Bits auf 0 */
    c->r[0] = (kr_lade32le(key +  0)     ) & 0x3ffffffu;
    c->r[1] = (kr_lade32le(key +  3) >> 2) & 0x3ffff03u;
    c->r[2] = (kr_lade32le(key +  6) >> 4) & 0x3ffc0ffu;
    c->r[3] = (kr_lade32le(key +  9) >> 6) & 0x3f03fffu;
    c->r[4] = (kr_lade32le(key + 12) >> 8) & 0x00fffffu;

    for (i = 0; i < 5; i++) c->h[i] = 0;
    for (i = 0; i < 4; i++) c->pad[i] = kr_lade32le(key + 16 + 4 * i);

    c->bufn   = 0;
    c->fertig = 0;
}

/* Verarbeitet ein Vielfaches von 16 Byte. Das implizite Hoch-Bit
 * entfaellt nur beim letzten, unvollstaendigen Block (der sein 1-Bit
 * bereits explizit im Puffer stehen hat). */
static void kr_poly1305_bloecke(MooPoly1305Ctx* c, const uint8_t* m, size_t n) {
    const uint32_t hoch_bit = c->fertig ? 0u : (1u << 24);
    const uint32_t r0 = c->r[0], r1 = c->r[1], r2 = c->r[2],
                   r3 = c->r[3], r4 = c->r[4];
    /* s_i = 5 * r_i — die Reduktion modulo 2^130-5 faltet die oberen
     * Glieder mit Faktor 5 zurueck. Passt in uint32_t, da r geklemmt. */
    const uint32_t s1 = r1 * 5u, s2 = r2 * 5u, s3 = r3 * 5u, s4 = r4 * 5u;

    uint32_t h0 = c->h[0], h1 = c->h[1], h2 = c->h[2],
             h3 = c->h[3], h4 = c->h[4];

    while (n >= 16) {
        uint64_t d0, d1, d2, d3, d4, uebertrag;

        /* Block als 130-Bit-Zahl addieren (Little Endian + 1-Bit oben) */
        h0 += (kr_lade32le(m +  0)     ) & 0x3ffffffu;
        h1 += (kr_lade32le(m +  3) >> 2) & 0x3ffffffu;
        h2 += (kr_lade32le(m +  6) >> 4) & 0x3ffffffu;
        h3 += (kr_lade32le(m +  9) >> 6) & 0x3ffffffu;
        h4 += (kr_lade32le(m + 12) >> 8) | hoch_bit;

        /* h = h * r mod 2^130-5 */
        d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3
           + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4
           + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0
           + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1
           + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2
           + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

        uebertrag = d0 >> 26; h0 = (uint32_t)d0 & 0x3ffffffu; d1 += uebertrag;
        uebertrag = d1 >> 26; h1 = (uint32_t)d1 & 0x3ffffffu; d2 += uebertrag;
        uebertrag = d2 >> 26; h2 = (uint32_t)d2 & 0x3ffffffu; d3 += uebertrag;
        uebertrag = d3 >> 26; h3 = (uint32_t)d3 & 0x3ffffffu; d4 += uebertrag;
        uebertrag = d4 >> 26; h4 = (uint32_t)d4 & 0x3ffffffu;
        h0 += (uint32_t)uebertrag * 5u;
        uebertrag = h0 >> 26;  h0 &= 0x3ffffffu; h1 += (uint32_t)uebertrag;

        m += 16;
        n -= 16;
    }

    c->h[0] = h0; c->h[1] = h1; c->h[2] = h2; c->h[3] = h3; c->h[4] = h4;
}

void moo_krypto_poly1305_update(MooPoly1305Ctx* c,
                                const uint8_t* daten, size_t n) {
    size_t i;

    if (c->bufn > 0) {
        size_t frei = 16 - c->bufn;
        size_t nimm = (n < frei) ? n : frei;
        for (i = 0; i < nimm; i++) c->buf[c->bufn + i] = daten[i];
        c->bufn += nimm;
        daten   += nimm;
        n       -= nimm;
        if (c->bufn < 16) return;
        kr_poly1305_bloecke(c, c->buf, 16);
        c->bufn = 0;
    }

    if (n >= 16) {
        size_t ganz = n & ~(size_t)15;
        kr_poly1305_bloecke(c, daten, ganz);
        daten += ganz;
        n     -= ganz;
    }

    for (i = 0; i < n; i++) c->buf[i] = daten[i];
    c->bufn = n;
}

void moo_krypto_poly1305_final(MooPoly1305Ctx* c, uint8_t tag[16]) {
    uint32_t h0, h1, h2, h3, h4;
    uint32_t g0, g1, g2, g3, g4;
    uint32_t uebertrag, maske;
    uint64_t f;
    size_t i;

    if (c->bufn > 0) {
        c->buf[c->bufn++] = 1;                       /* Padding-1-Bit */
        while (c->bufn < 16) c->buf[c->bufn++] = 0;
        c->fertig = 1;
        kr_poly1305_bloecke(c, c->buf, 16);
    }

    h0 = c->h[0]; h1 = c->h[1]; h2 = c->h[2]; h3 = c->h[3]; h4 = c->h[4];

    uebertrag = h1 >> 26; h1 &= 0x3ffffffu; h2 += uebertrag;
    uebertrag = h2 >> 26; h2 &= 0x3ffffffu; h3 += uebertrag;
    uebertrag = h3 >> 26; h3 &= 0x3ffffffu; h4 += uebertrag;
    uebertrag = h4 >> 26; h4 &= 0x3ffffffu; h0 += uebertrag * 5u;
    uebertrag = h0 >> 26; h0 &= 0x3ffffffu; h1 += uebertrag;

    /* g = h + 5 (entspricht h - p). Der Wrap in g4 ist gewollt und auf
     * unsigned definiert: ein gesetztes oberstes Bit signalisiert, dass
     * h < p war und h daher stehen bleiben muss. */
    g0 = h0 + 5u;        uebertrag = g0 >> 26; g0 &= 0x3ffffffu;
    g1 = h1 + uebertrag; uebertrag = g1 >> 26; g1 &= 0x3ffffffu;
    g2 = h2 + uebertrag; uebertrag = g2 >> 26; g2 &= 0x3ffffffu;
    g3 = h3 + uebertrag; uebertrag = g3 >> 26; g3 &= 0x3ffffffu;
    g4 = h4 + uebertrag - (1u << 26);

    /* Konstant-zeitige Auswahl ohne Verzweigung */
    maske = (g4 >> 31) - 1u;      /* h >= p -> 0xffffffff, sonst 0 */
    g0 &= maske; g1 &= maske; g2 &= maske; g3 &= maske; g4 &= maske;
    maske = ~maske;
    h0 = (h0 & maske) | g0;
    h1 = (h1 & maske) | g1;
    h2 = (h2 & maske) | g2;
    h3 = (h3 & maske) | g3;
    h4 = (h4 & maske) | g4;

    /* 5x26 Bit -> 4x32 Bit (h mod 2^128) */
    h0 = ( h0        | (h1 << 26));
    h1 = ((h1 >>  6) | (h2 << 20));
    h2 = ((h2 >> 12) | (h3 << 14));
    h3 = ((h3 >> 18) | (h4 <<  8));

    /* tag = (h + s) mod 2^128 — Uebertraege ueber uint64_t */
    f = (uint64_t)h0 + c->pad[0];                 h0 = (uint32_t)f;
    f = (uint64_t)h1 + c->pad[1] + (f >> 32);     h1 = (uint32_t)f;
    f = (uint64_t)h2 + c->pad[2] + (f >> 32);     h2 = (uint32_t)f;
    f = (uint64_t)h3 + c->pad[3] + (f >> 32);     h3 = (uint32_t)f;

    kr_speichere32le(tag +  0, h0);
    kr_speichere32le(tag +  4, h1);
    kr_speichere32le(tag +  8, h2);
    kr_speichere32le(tag + 12, h3);

    /* Schluesselmaterial im Kontext loeschen (best effort, freestanding:
     * kein memset_s verfuegbar, der Compiler koennte das wegoptimieren) */
    for (i = 0; i < 5; i++) { c->r[i] = 0; c->h[i] = 0; }
    for (i = 0; i < 4; i++) c->pad[i] = 0;
    for (i = 0; i < 16; i++) c->buf[i] = 0;
    c->bufn = 0;
}

void moo_krypto_poly1305(const uint8_t key[32], const uint8_t* daten,
                         size_t n, uint8_t tag[16]) {
    MooPoly1305Ctx c;
    moo_krypto_poly1305_init(&c, key);
    moo_krypto_poly1305_update(&c, daten, n);
    moo_krypto_poly1305_final(&c, tag);
}

/* ======================================== AEAD-Konstruktion (2.8) == */

/* Einmal-Schluessel fuer Poly1305 = erste 32 Byte des ChaCha20-Blocks
 * mit Zaehler 0 (RFC 8439 2.6). */
void moo_krypto_chacha20_poly1305_keygen(const uint8_t key[32],
                                         const uint8_t nonce[12],
                                         uint8_t otk[32]) {
    uint8_t blk[64];
    int i;
    moo_krypto_chacha20_block(key, 0, nonce, blk);
    for (i = 0; i < 32; i++) otk[i] = blk[i];
    for (i = 0; i < 64; i++) blk[i] = 0;
}

/* Nullpadding auf das naechste Vielfache von 16 */
static void kr_poly_pad16(MooPoly1305Ctx* c, size_t n) {
    static const uint8_t nullen[16] = { 0 };
    size_t rest = n % 16;
    if (rest != 0) moo_krypto_poly1305_update(c, nullen, 16 - rest);
}

static void kr_poly_laenge(MooPoly1305Ctx* c, size_t n) {
    uint8_t le[8];
    uint64_t v = (uint64_t)n;
    int i;
    for (i = 0; i < 8; i++) le[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
    moo_krypto_poly1305_update(c, le, 8);
}

/* mac_data = aad || pad16(aad) || ct || pad16(ct) || le64(|aad|) || le64(|ct|) */
static void kr_aead_tag(const uint8_t otk[32],
                        const uint8_t* aad, size_t aad_n,
                        const uint8_t* ct,  size_t ct_n,
                        uint8_t tag[16]) {
    MooPoly1305Ctx c;
    moo_krypto_poly1305_init(&c, otk);
    if (aad_n > 0) moo_krypto_poly1305_update(&c, aad, aad_n);
    kr_poly_pad16(&c, aad_n);
    if (ct_n > 0)  moo_krypto_poly1305_update(&c, ct, ct_n);
    kr_poly_pad16(&c, ct_n);
    kr_poly_laenge(&c, aad_n);
    kr_poly_laenge(&c, ct_n);
    moo_krypto_poly1305_final(&c, tag);
}

void moo_krypto_chacha20_poly1305_encrypt(const uint8_t key[32],
                                          const uint8_t nonce[12],
                                          const uint8_t* aad, size_t aad_n,
                                          const uint8_t* pt, size_t pt_n,
                                          uint8_t* ct, uint8_t tag[16]) {
    uint8_t otk[32];
    int i;

    moo_krypto_chacha20_poly1305_keygen(key, nonce, otk);
    /* Nutzdaten ab Zaehler 1 — Zaehler 0 gehoert dem Poly1305-Schluessel */
    moo_krypto_chacha20_xor(key, 1, nonce, pt, ct, pt_n);
    /* Stream-AEAD: |ct| == |pt| */
    kr_aead_tag(otk, aad, aad_n, ct, pt_n, tag);

    for (i = 0; i < 32; i++) otk[i] = 0;
}

int moo_krypto_chacha20_poly1305_decrypt(const uint8_t key[32],
                                         const uint8_t nonce[12],
                                         const uint8_t* aad, size_t aad_n,
                                         const uint8_t* ct, size_t ct_n,
                                         const uint8_t tag[16],
                                         uint8_t* pt) {
    uint8_t otk[32];
    uint8_t soll[16];
    uint8_t diff = 0;
    int i;

    moo_krypto_chacha20_poly1305_keygen(key, nonce, otk);
    kr_aead_tag(otk, aad, aad_n, ct, ct_n, soll);

    /* Konstant-zeitiger Vergleich: kein frueher Abbruch, kein memcmp */
    for (i = 0; i < 16; i++) diff = (uint8_t)(diff | (soll[i] ^ tag[i]));

    for (i = 0; i < 16; i++) soll[i] = 0;

    /* fail-closed: bei falschem Tag wird KEIN Klartext erzeugt.
     * Das ist die Kernregel von Encrypt-then-MAC — niemals unverifizierte
     * Daten an den Aufrufer geben. */
    if (diff != 0) {
        for (i = 0; i < 32; i++) otk[i] = 0;
        return -1;
    }

    moo_krypto_chacha20_xor(key, 1, nonce, ct, pt, ct_n);
    for (i = 0; i < 32; i++) otk[i] = 0;
    return 0;
}

/* ============================================================
 * moo_krypto_x25519.c — X25519 Schluesselaustausch (NETZK-3).
 * RFC 7748, Kurve Curve25519 ueber dem Koerper F(2^255-19).
 *
 * FREESTANDING wie der uebrige Krypto-Kern: nur <stdint.h>/<stddef.h>
 * ueber moo_krypto.h, kein malloc, kein OS, kein libc-Aufruf.
 *
 * FELDARITHMETIK — bewusste Entscheidung:
 * 16 Glieder zu je 16 Bit (Radix 2^16) in int64_t, nicht die schnellere
 * Radix-2^51-Variante mit __int128. Grund: __int128 gibt es nicht auf
 * jedem freestanding-Ziel (32-Bit, Bare-Metal-moOS). Diese Variante
 * braucht nur 64-Bit-Multiplikation und laeuft ueberall. Der Preis ist
 * Geschwindigkeit, nicht Korrektheit.
 *
 * UB-POLICY (Projektregel, Punkt 3): Der Uebertrag in kr_carry und die
 * Reduktion in kr_packe nutzen arithmetische Rechtsschiebungen auf
 * moeglicherweise negativem int64_t. Das ist implementation-defined
 * (NICHT undefined) und auf allen von moo genutzten Compilern
 * (gcc/clang/msvc) als arithmetische Schiebung definiert. Bewusst so
 * gewaehlt: die Glieder duerfen zwischen den Reduktionen negativ werden,
 * genau dafuer ist der signed Radix-2^16-Kern ausgelegt. Signed Overflow
 * tritt nicht auf: |Glied| bleibt nach jedem kr_mul unter 2^62, da 16
 * Produkte von je unter 2^17 mal unter 2^17 (< 2^38) plus Faltungsfaktor
 * 38 weit darunter bleiben.
 *
 * EHRLICHKEIT / GRENZEN:
 *  - Die Montgomery-Ladder laeuft datenunabhaengig: immer alle 255
 *    Bit-Schritte, bedingter Tausch nur ueber Bitmasken, keine
 *    geheimnisabhaengigen Verzweigungen oder Speicherzugriffe.
 *  - NICHT gehaertet gegen Power-/EM-Seitenkanaele.
 *  - Referenz-Implementierung, keine Vektor-/Assembler-Optimierung.
 * ============================================================ */
#include "moo_krypto.h"

/* Feldelement: 16 Glieder a 16 Bit. Glieder duerfen zwischenzeitlich
 * negativ und leicht ueberlaufend sein — kr_carry normalisiert. */
typedef int64_t MooGf[16];

/* Kurvenkonstante (A-2)/4 = 121665 */
static const MooGf kr_121665 = { 0xDB41, 1 };

static void kr_gf_kopiere(MooGf ziel, const MooGf quelle) {
    int i;
    for (i = 0; i < 16; i++) ziel[i] = quelle[i];
}

/* Uebertragsnormalisierung modulo 2^255-19.
 * Der Ueberlauf aus Glied 15 wird mit Faktor 38 (= 2 * 19) auf Glied 0
 * zurueckgefaltet, weil 2^256 kongruent 38 modulo 2^255-19 ist.
 * Der Offset +2^16 vor der Schiebung macht das Verfahren auch fuer
 * negative Glieder korrekt (Abrundung statt Trunkierung). */
static void kr_carry(MooGf o) {
    int i;
    int64_t c;
    for (i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        c = o[i] >> 16;          /* arithmetisch, siehe UB-POLICY im Kopf */
        o[i] -= c * 65536;
        if (i < 15) o[i + 1] += c - 1;
        else        o[0]     += 38 * (c - 1);
    }
}

/* Bedingter Tausch ohne Verzweigung. bit muss 0 oder 1 sein. */
static void kr_tausche(MooGf p, MooGf q, int bit) {
    int i;
    /* Maske: bit=1 -> alle Bits gesetzt, bit=0 -> 0 */
    int64_t maske = ~((int64_t)bit - 1);
    for (i = 0; i < 16; i++) {
        int64_t t = maske & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void kr_add(MooGf o, const MooGf a, const MooGf b) {
    int i;
    for (i = 0; i < 16; i++) o[i] = a[i] + b[i];
}

static void kr_sub(MooGf o, const MooGf a, const MooGf b) {
    int i;
    for (i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

static void kr_mul(MooGf o, const MooGf a, const MooGf b) {
    int64_t t[31];
    int i, j;
    for (i = 0; i < 31; i++) t[i] = 0;
    for (i = 0; i < 16; i++)
        for (j = 0; j < 16; j++)
            t[i + j] += a[i] * b[j];
    /* Faltung der oberen Haelfte: 2^256 kongruent 38 modulo 2^255-19 */
    for (i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for (i = 0; i < 16; i++) o[i] = t[i];
    kr_carry(o);
    kr_carry(o);
}

static void kr_quad(MooGf o, const MooGf a) {
    kr_mul(o, a, a);
}

/* Inverses ueber den kleinen Satz von Fermat: a^(p-2) = a^(2^255-21).
 * Die ausgelassenen Schritte 2 und 4 bilden genau diesen Exponenten. */
static void kr_invers(MooGf o, const MooGf i) {
    MooGf c;
    int a;
    kr_gf_kopiere(c, i);
    for (a = 253; a >= 0; a--) {
        kr_quad(c, c);
        if (a != 2 && a != 4) kr_mul(c, c, i);
    }
    kr_gf_kopiere(o, c);
}

static void kr_entpacke(MooGf o, const uint8_t n[32]) {
    int i;
    for (i = 0; i < 16; i++)
        o[i] = (int64_t)n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    /* das oberste Bit wird laut RFC 7748 Abschnitt 5 ignoriert */
    o[15] &= 0x7fff;
}

/* Voll reduzieren (zwei bedingte Subtraktionen von p) und ausgeben. */
static void kr_packe(uint8_t o[32], const MooGf n) {
    MooGf m, t;
    int i, j, b;

    kr_gf_kopiere(t, n);
    kr_carry(t); kr_carry(t); kr_carry(t);

    for (j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        kr_tausche(t, m, 1 - b);
    }

    for (i = 0; i < 16; i++) {
        o[2 * i]     = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)((t[i] >> 8) & 0xff);
    }
}

/* --------------------------------------------------------- oeffentlich -- */

int moo_krypto_x25519(uint8_t aus[32], const uint8_t skalar[32],
                      const uint8_t u_punkt[32]) {
    uint8_t z[32];
    MooGf x, a, b, c, d, e, f, iz, erg;
    int i;
    uint8_t oder = 0;

    /* Clamping nach RFC 7748 Abschnitt 5: drei unterste Bits loeschen
     * (Kofaktor 8), Bit 255 loeschen, Bit 254 setzen. */
    for (i = 0; i < 32; i++) z[i] = skalar[i];
    z[0]  = (uint8_t)(z[0] & 248u);
    z[31] = (uint8_t)((z[31] & 127u) | 64u);

    kr_entpacke(x, u_punkt);
    for (i = 0; i < 16; i++) {
        b[i] = x[i];
        a[i] = c[i] = d[i] = 0;
    }
    a[0] = d[0] = 1;

    /* Montgomery-Ladder, datenunabhaengig ueber alle 255 Bits */
    for (i = 254; i >= 0; i--) {
        int r = (int)((z[i >> 3] >> (i & 7)) & 1);
        kr_tausche(a, b, r);
        kr_tausche(c, d, r);
        kr_add(e, a, c);
        kr_sub(a, a, c);
        kr_add(c, b, d);
        kr_sub(b, b, d);
        kr_quad(d, e);
        kr_quad(f, a);
        kr_mul(a, c, a);
        kr_mul(c, b, e);
        kr_add(e, a, c);
        kr_sub(a, a, c);
        kr_quad(b, a);
        kr_sub(c, d, f);
        kr_mul(a, c, kr_121665);
        kr_add(a, a, d);
        kr_mul(c, c, a);
        kr_mul(a, d, f);
        kr_mul(d, b, x);
        kr_quad(b, e);
        kr_tausche(a, b, r);
        kr_tausche(c, d, r);
    }

    /* Ergebnis = X / Z */
    kr_invers(iz, c);
    kr_mul(erg, a, iz);
    kr_packe(aus, erg);

    /* NEGATIVGATE (RFC 7748 Abschnitt 6.1): Ein Punkt kleiner Ordnung
     * erzeugt ein Shared Secret aus lauter Nullen. Das ist KEIN
     * gueltiges Geheimnis — fail-closed mit -1. Der Puffer ist trotzdem
     * deterministisch beschrieben (eben mit Nullen), damit nie
     * uninitialisierter Speicher nach aussen gelangt. Die Pruefung ist
     * konstant-zeitig (ODER-Akkumulation, kein frueher Abbruch). */
    for (i = 0; i < 32; i++) oder = (uint8_t)(oder | aus[i]);

    /* geklemmten Skalar aus dem Stack raeumen (best effort) */
    for (i = 0; i < 32; i++) z[i] = 0;

    if (oder == 0) return -1;
    return 0;
}

int moo_krypto_x25519_basis(uint8_t aus[32], const uint8_t skalar[32]) {
    /* Basispunkt u = 9, Little Endian (RFC 7748 Abschnitt 4.1) */
    uint8_t basis[32];
    int i;
    basis[0] = 9;
    for (i = 1; i < 32; i++) basis[i] = 0;
    return moo_krypto_x25519(aus, skalar, basis);
}

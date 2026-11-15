/* P2-T58 Batch D (Plan f40b5980 §3.2): freistehende Rechenfunktionen fuer die
 * native Wertschicht - Float-Rest (x % y) und Float-Potenz (x ** y) ohne libm.
 * C11, nur <stdint.h>, kein MooValue, keine Allokation, kein errno: hosted,
 * bare und UEFI linkbar. Bitoperationen unsigned (ub-arithmetik-policy).
 *
 * moo_native_fmod: exakter Rest nach C99 F.9.7.1 (wie C fmod). Verfahren:
 * Mantissen als Ganzzahlen, Exponenten angleichen, schrittweise Schiebe-
 * Subtrahier-Reduktion (binaere Langdivision ohne Quotient); das Ergebnis ist
 * exakt darstellbar, es wird nie gerundet. */
#include "moo_native_math.h"

static uint64_t bits_f64(double x) {
    union { double d; uint64_t u; } v;
    v.d = x;
    return v.u;
}

static double f64_bits(uint64_t u) {
    union { double d; uint64_t u; } v;
    v.u = u;
    return v.d;
}

#define F64_MANT_BITS 52
#define F64_EXP_MASK UINT64_C(0x7ff)
#define F64_IMPLIZIT (UINT64_C(1) << F64_MANT_BITS)
#define F64_MANT_MASK (F64_IMPLIZIT - 1u)

/* Mantisse mit impliziter Eins und unverzerrter Exponent; Subnormale werden
 * normalisiert (Mantisse links geschoben, Exponent entsprechend kleiner). */
static uint64_t zerlege(uint64_t betrag, int32_t* exponent) {
    int32_t e = (int32_t)(betrag >> F64_MANT_BITS);
    uint64_t m = betrag & F64_MANT_MASK;
    if (e == 0) {
        /* subnormal: e = 1 - Verschiebung bis zur impliziten Stelle */
        e = 1;
        while ((m & F64_IMPLIZIT) == 0u) {
            m <<= 1;
            e--;
        }
    } else {
        m |= F64_IMPLIZIT;
    }
    *exponent = e;
    return m;
}

double moo_native_fmod(double x, double y) {
    uint64_t ux = bits_f64(x), uy = bits_f64(y);
    uint64_t vorzeichen = ux & (UINT64_C(1) << 63);
    uint64_t ax = ux & ~(UINT64_C(1) << 63), ay = uy & ~(UINT64_C(1) << 63);
    /* NaN-Faelle: y = 0, y NaN, x unendlich oder NaN (x * y / (x * y) liefert
       NaN ohne Bibliothek; bei x NaN bleibt NaN erhalten). */
    if (ay == 0u || ay > (F64_EXP_MASK << F64_MANT_BITS)
        || (ax >> F64_MANT_BITS) == F64_EXP_MASK) {
        double p = x * y;
        return p / p;
    }
    if (ax < ay) return x;          /* |x| < |y| (auch y = Inf, x = +-0) */
    if (ax == ay) return 0.0 * x;   /* exakt Vielfaches: +-0 mit Vorzeichen von x */
    int32_t ex, ey;
    uint64_t mx = zerlege(ax, &ex);
    uint64_t my = zerlege(ay, &ey);
    /* Reduktion: solange der Exponent von x groesser ist, mx mod my bilden und
       um eine Stelle nach links schieben (mx < 2*my bleibt invariant). */
    for (; ex > ey; ex--) {
        if (mx >= my) mx -= my;
        if (mx == 0u) return 0.0 * x;
        mx <<= 1;
    }
    if (mx >= my) mx -= my;
    if (mx == 0u) return 0.0 * x;
    /* normalisieren: implizite Stelle wieder an Position 52 bringen */
    while ((mx & F64_IMPLIZIT) == 0u) {
        mx <<= 1;
        ex--;
    }
    uint64_t ergebnis;
    if (ex > 0) {
        ergebnis = ((uint64_t)ex << F64_MANT_BITS) | (mx & F64_MANT_MASK);
    } else {
        /* subnormal: exakt, weil der Rest kleiner als |y| ist und |y| >= den
           kleinsten Abstand der Eingaben hat (keine Bits gehen verloren) */
        ergebnis = mx >> (uint32_t)(1 - ex);
    }
    return f64_bits(ergebnis | vorzeichen);
}

float moo_native_fmodf(float x, float y) {
    /* Der Rest zweier f32 ist als f32 darstellbar; die f64-Rechnung ist exakt. */
    return (float)moo_native_fmod((double)x, (double)y);
}

/* ---------------------------------------------------------------------------
 * moo_native_pow: IEEE-754/C99 pow (F.9.4.4). Sonderfaelle exakt nach Tabelle,
 * Kern x ** y = exp(y * ln|x|) in Doppel-Double-Arithmetik (Dekker/Knuth,
 * ~100 Bit): ln ueber atanh-Reihe auf [sqrt(1/2), sqrt(2)), exp ueber
 * Reduktion mit ln 2, Teilung durch 2^10, Taylor-Reihe und 10-faches Quadrieren.
 * Nur eine abschliessende Rundung nach double (im subnormalen Bereich zwei),
 * daher Abweichung <= 1 ULP. Keine FMA vorausgesetzt: die Produkte, deren
 * Rundung der Algorithmus braucht, laufen ueber volatile-Zwischenwerte, damit
 * eine FMA-Kontraktion (z. B. aarch64) sie nicht verschmilzt.
 * ------------------------------------------------------------------------- */
typedef struct { double hi, lo; } moo_dd;

static moo_dd dd_zwei_summe(double a, double b) {
    double s = a + b;
    double bb = s - a;
    double fehler = (a - (s - bb)) + (b - bb);
    moo_dd r = { s, fehler };
    return r;
}

static moo_dd dd_schnell_summe(double a, double b) {
    double s = a + b;
    moo_dd r = { s, b - (s - a) };
    return r;
}

/* Dekker-Teilung in zwei 26-Bit-Haelften (Werte hier immer < 2^970). */
static void dd_teile(double a, double* hi, double* lo) {
    volatile double t = 134217729.0 * a;   /* 2^27 + 1 */
    double h = t - (t - a);
    *hi = h;
    *lo = a - h;
}

static moo_dd dd_zwei_produkt(double a, double b) {
    volatile double p = a * b;
    double ah, al, bh, bl;
    dd_teile(a, &ah, &al);
    dd_teile(b, &bh, &bl);
    /* Teilprodukte der Haelften sind exakt; nur p ist gerundet. */
    double fehler = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
    moo_dd r = { p, fehler };
    return r;
}

static moo_dd dd_add(moo_dd a, moo_dd b) {
    moo_dd s = dd_zwei_summe(a.hi, b.hi);
    moo_dd t = dd_zwei_summe(a.lo, b.lo);
    s.lo += t.hi;
    s = dd_schnell_summe(s.hi, s.lo);
    s.lo += t.lo;
    return dd_schnell_summe(s.hi, s.lo);
}

static moo_dd dd_neg(moo_dd a) { moo_dd r = { -a.hi, -a.lo }; return r; }

static moo_dd dd_mul(moo_dd a, moo_dd b) {
    moo_dd p = dd_zwei_produkt(a.hi, b.hi);
    p.lo += a.hi * b.lo + a.lo * b.hi;
    return dd_schnell_summe(p.hi, p.lo);
}

static moo_dd dd_mul_d(moo_dd a, double b) {
    moo_dd p = dd_zwei_produkt(a.hi, b);
    p.lo += a.lo * b;
    return dd_schnell_summe(p.hi, p.lo);
}

static moo_dd dd_div(moo_dd a, moo_dd b) {
    double q1 = a.hi / b.hi;
    moo_dd r = dd_add(a, dd_neg(dd_mul_d(b, q1)));
    double q2 = r.hi / b.hi;
    r = dd_add(r, dd_neg(dd_mul_d(b, q2)));
    double q3 = r.hi / b.hi;
    moo_dd q = dd_schnell_summe(q1, q2);
    moo_dd q3d = { q3, 0.0 };
    return dd_add(q, q3d);
}

static moo_dd dd_aus(double a) { moo_dd r = { a, 0.0 }; return r; }

/* ln 2 als Doppel-Double. */
static const moo_dd DD_LN2 = { 0x1.62e42fefa39efp-1, 0x1.abc9e3b39803fp-56 };

/* 2^n fuer -1022 <= n <= 1023 ueber das Bitmuster (exakt). */
static double zwei_hoch(int32_t n) {
    return f64_bits((uint64_t)(n + 1023) << F64_MANT_BITS);
}

/* ln(a) fuer endliches a > 0 als Doppel-Double. */
static moo_dd dd_ln(double a) {
    int32_t e;
    uint64_t m = zerlege(bits_f64(a), &e);
    /* a = mant * 2^k mit mant in [1, 2); auf [sqrt(1/2), sqrt(2)) schieben */
    double mant = f64_bits((m & F64_MANT_MASK) | (UINT64_C(1023) << F64_MANT_BITS));
    int32_t k = e - 1023;
    if (mant > 1.4142135623730951) { mant *= 0.5; k += 1; }
    /* ln(mant) = 2 atanh(f), f = (mant-1)/(mant+1), |f| <= 0.1716 */
    moo_dd f = dd_div(dd_aus(mant - 1.0), dd_zwei_summe(mant, 1.0));
    moo_dd f2 = dd_mul(f, f);
    moo_dd reihe = dd_div(dd_aus(1.0), dd_aus(45.0));
    for (int32_t i = 21; i >= 0; i--) {
        reihe = dd_add(dd_mul(reihe, f2), dd_div(dd_aus(1.0), dd_aus((double)(2 * i + 1))));
    }
    moo_dd ln_mant = dd_mul_d(dd_mul(f, reihe), 2.0);
    return dd_add(dd_mul_d(DD_LN2, (double)k), ln_mant);
}

/* exp(p) fuer -746 < p.hi < 710, Ergebnis gerundet nach double. */
static double dd_exp(moo_dd p) {
    double n = p.hi * 1.4426950408889634;
    n = (double)(int64_t)(n + (n >= 0.0 ? 0.5 : -0.5));
    moo_dd r = dd_add(p, dd_neg(dd_mul_d(DD_LN2, n)));
    r.hi *= 0x1p-10;
    r.lo *= 0x1p-10;
    /* Taylor bis r^9 / 9! (|r| <= 3.4e-4: Restglied < 2^-110) */
    moo_dd summe = dd_div(dd_aus(1.0), dd_aus(362880.0));
    double fakultaet = 362880.0;
    for (int32_t k = 8; k >= 0; k--) {
        fakultaet /= (double)(k + 1);   /* k! exakt */
        summe = dd_add(dd_mul(summe, r), dd_div(dd_aus(1.0), dd_aus(fakultaet)));
    }
    for (int32_t i = 0; i < 10; i++) summe = dd_mul(summe, summe);
    double wert = summe.hi + summe.lo;
    int32_t ni = (int32_t)n;
    if (ni > 1023) return wert * zwei_hoch(ni - 1) * 2.0;
    if (ni >= -1021) return wert * zwei_hoch(ni);
    /* subnormaler Bereich: erst exakt in den Normalbereich, dann einmal runden */
    return (wert * zwei_hoch(ni + 600)) * zwei_hoch(-600);
}

/* 0 = keine ganze Zahl, 1 = ungerade ganze Zahl, 2 = gerade ganze Zahl. */
static int32_t ganzzahl_art(double y) {
    uint64_t u = bits_f64(y) & ~(UINT64_C(1) << 63);
    int32_t e = (int32_t)(u >> F64_MANT_BITS) - 1023;
    if (u == 0u) return 2;
    if (e < 0) return 0;
    if (e >= 53) return 2;
    uint64_t m = (u & F64_MANT_MASK) | F64_IMPLIZIT;
    uint32_t bruch = (uint32_t)(F64_MANT_BITS - e);
    if (m & ((UINT64_C(1) << bruch) - 1u)) return 0;
    return ((m >> bruch) & 1u) ? 1 : 2;
}

double moo_native_pow(double x, double y) {
    uint64_t ux = bits_f64(x), uy = bits_f64(y);
    uint64_t ax = ux & ~(UINT64_C(1) << 63), ay = uy & ~(UINT64_C(1) << 63);
    uint64_t unendlich = F64_EXP_MASK << F64_MANT_BITS;
    if (ay == 0u) return 1.0;                       /* x ** +-0 = 1, auch NaN */
    if (ux == bits_f64(1.0)) return 1.0;            /* 1 ** y = 1, auch NaN */
    if (ax > unendlich || ay > unendlich) return x + y;   /* NaN */
    int32_t art = ganzzahl_art(y);
    int negativ_x = (ux >> 63) != 0u;
    int negativ_y = (uy >> 63) != 0u;
    if (ay == unendlich) {                          /* y = +-Inf */
        if (ax == bits_f64(1.0)) return 1.0;        /* -1 ** +-Inf */
        int gross = ax > bits_f64(1.0);
        return (gross != negativ_y) ? f64_bits(unendlich) : 0.0;
    }
    if (ax == 0u) {                                  /* x = +-0 */
        if (negativ_y) return (art == 1) ? 1.0 / x : f64_bits(unendlich);
        return (art == 1) ? x : 0.0;
    }
    if (ax == unendlich) {                           /* x = +-Inf */
        if (!negativ_x) return negativ_y ? 0.0 : x;
        if (negativ_y) return (art == 1) ? -0.0 : 0.0;
        return (art == 1) ? x : f64_bits(unendlich);
    }
    if (negativ_x && art == 0) {                     /* x < 0, y nicht ganz */
        double z = x - x;
        return z / z;
    }
    double betrag = f64_bits(ax);
    double ergebnis;
    if (ay > bits_f64(0x1p64)) {
        /* |y| > 2^64 (gerade ganze Zahl): |x| = 1 ergibt 1, sonst ist
           |y ln x| > 2^11, also Ueber- bzw. Unterlauf */
        if (betrag == 1.0) ergebnis = 1.0;
        else ergebnis = ((betrag > 1.0) != negativ_y) ? f64_bits(unendlich) : 0.0;
    } else {
        moo_dd p = dd_mul_d(dd_ln(betrag), y);
        if (p.hi > 709.8) ergebnis = f64_bits(unendlich);
        else if (p.hi < -745.2) ergebnis = 0.0;
        else ergebnis = dd_exp(p);
    }
    return (negativ_x && art == 1) ? -ergebnis : ergebnis;
}

float moo_native_powf(float x, float y) {
    /* f64-Kern mit ~100 Bit Zwischengenauigkeit, dann eine Rundung nach f32:
       <= 1 ULP; Sonderfaelle sind fuer f32-Werte identisch. */
    return (float)moo_native_pow((double)x, (double)y);
}

/* ---------------------------------------------------------------------------
 * P2-T89: moo_native_sin/cos/tan (+ f32) ohne libm.
 * Argumentreduktion |x| = (q + f) * pi/2, |f| <= 1/2, exakt im Festkomma: die
 * Mantisse (53 Bit) wird mit dem passenden Fenster von 2/pi (24-Bit-Woerter,
 * ganze Vielfache von 4 entfallen) multipliziert; 12 Woerter liefern >= 263
 * Nachkommabits, genug fuer jede Ausloeschung bei double. Kern auf
 * [-pi/4, pi/4] als Taylor-Reihe in Doppel-Double (Horner ueber r^2 mit
 * kleinen exakten Teilern), eine abschliessende Rundung nach double:
 * <= 1 ULP. f32 ueber den f64-Kern mit einer Rundung nach float.
 * ------------------------------------------------------------------------- */

/* Nachkommastellen von 2/pi in 24-Bit-Woertern (2/pi = 0.A2F9836E4E44...h). */
static const uint32_t ZWEI_DURCH_PI[66] = {
    0xA2F983, 0x6E4E44, 0x1529FC, 0x2757D1, 0xF534DD, 0xC0DB62,
    0x95993C, 0x439041, 0xFE5163, 0xABDEBB, 0xC561B7, 0x246E3A,
    0x424DD2, 0xE00649, 0x2EEA09, 0xD1921C, 0xFE1DEB, 0x1CB129,
    0xA73EE8, 0x8235F5, 0x2EBB44, 0x84E99C, 0x7026B4, 0x5F7E41,
    0x3991D6, 0x398353, 0x39F49C, 0x845F8B, 0xBDF928, 0x3B1FF8,
    0x97FFDE, 0x05980F, 0xEF2F11, 0x8B5A0A, 0x6D1F6D, 0x367ECF,
    0x27CB09, 0xB74F46, 0x3F669E, 0x5FEA2D, 0x7527BA, 0xC7EBE5,
    0xF17B3D, 0x0739F7, 0x8A5292, 0xEA6BFB, 0x5FB11F, 0x8D5D08,
    0x560330, 0x46FC7B, 0x6BABF0, 0xCFBC20, 0x9AF436, 0x1DA9E3,
    0x91615E, 0xE61B08, 0x659985, 0x5F14A0, 0x68408D, 0xFFD880,
    0x4D7327, 0x310606, 0x1556CA, 0x73A8C9, 0x60E27B, 0xC08C6B,
};

/* pi/2 als Doppel-Double. */
static const moo_dd DD_PI_HALBE = { 0x1.921fb54442d18p0, 0x1.1a62633145c07p-54 };

#define TRIG_WOERTER 12
#define TRIG_MASKE UINT64_C(0xFFFFFF)

/* Reduktion fuer endliches betrag >= pi/4: liefert r = f * pi/2 (|f| <= 1/2)
 * und q mod 4 mit betrag = (q + f) * pi/2 (modulo 2 pi exakt). */
static int32_t trig_reduziere(double betrag, moo_dd* r) {
    int32_t e;
    uint64_t m = zerlege(bits_f64(betrag), &e);
    int32_t exponent = e - 1075;                 /* betrag = m * 2^exponent */
    /* Wort i traegt m * w_i * 2^(exponent - 24(i+1)); ab Verschiebung >= 2 ist
       das ein Vielfaches von 4 und entfaellt. */
    int32_t erstes = exponent >= 2 ? (exponent - 2) / 24 : 0;
    /* Produkt P = m * T (T = Woerter erstes..erstes+11); Binaerpunkt bei Bit
       punkt = 24 (erstes + 12) - exponent. Vorschieben um s, damit der Punkt
       auf eine Wortgrenze faellt. */
    int32_t punkt = 24 * (erstes + TRIG_WOERTER) - exponent;
    int32_t s = (24 - punkt % 24) % 24;
    int32_t grenze = (punkt + s) / 24;           /* erstes Ganzzahlwort */
    uint64_t mw[4];
    for (int32_t k = 0; k < 4; k++) {
        int32_t schieb = 24 * k - s;
        /* m < 2^53: ab Verschiebung 64 bleibt nichts uebrig (Schieben um >= 64
           waere undefiniert); nach links hoechstens 23 Stellen. */
        uint64_t teil = schieb >= 64 ? 0u : (schieb >= 0 ? (m >> schieb) : (m << -schieb));
        mw[k] = teil & TRIG_MASKE;
    }
    uint64_t w[4 + TRIG_WOERTER];
    for (int32_t i = 0; i < 4 + TRIG_WOERTER; i++) w[i] = 0;
    for (int32_t a = 0; a < 4; a++)
        for (int32_t c = 0; c < TRIG_WOERTER; c++)
            w[a + c] += mw[a] * (uint64_t)ZWEI_DURCH_PI[erstes + TRIG_WOERTER - 1 - c];
    for (int32_t i = 0; i + 1 < 4 + TRIG_WOERTER; i++) {
        w[i + 1] += w[i] >> 24;
        w[i] &= TRIG_MASKE;
    }
    int32_t q = (int32_t)(w[grenze] & 3u);
    int negativ = 0;
    if (w[grenze - 1] >= (UINT64_C(1) << 23)) {  /* f >= 1/2: f - 1 */
        q += 1;
        negativ = 1;
        uint64_t uebertrag = 1;
        for (int32_t i = 0; i < grenze; i++) {
            uint64_t v = ((~w[i]) & TRIG_MASKE) + uebertrag;
            w[i] = v & TRIG_MASKE;
            uebertrag = v >> 24;
        }
    }
    /* Nachkommateil als Doppel-Double: positive Summanden von oben, exakt
       skaliert (2^(24(i - grenze)) >= 2^-360, normal). */
    moo_dd f = dd_aus(0.0);
    for (int32_t i = grenze - 1; i >= 0; i--) {
        if (w[i] == 0u) continue;
        f = dd_add(f, dd_aus((double)w[i] * zwei_hoch(24 * (i - grenze))));
    }
    moo_dd ergebnis = dd_mul(f, DD_PI_HALBE);
    *r = negativ ? dd_neg(ergebnis) : ergebnis;
    return q & 3;
}

/* sin(r) = r (1 - r^2/(2*3) (1 - r^2/(4*5) (...))), |r| <= pi/4. */
static moo_dd dd_sin_kern(moo_dd r) {
    moo_dd r2 = dd_mul(r, r);
    moo_dd t = dd_aus(1.0);
    for (int32_t k = 15; k >= 1; k--) {
        double teiler = (double)((2 * k) * (2 * k + 1));
        t = dd_add(dd_aus(1.0), dd_neg(dd_div(dd_mul(r2, t), dd_aus(teiler))));
    }
    return dd_mul(r, t);
}

/* cos(r) = 1 - r^2/(1*2) (1 - r^2/(3*4) (...)), |r| <= pi/4. */
static moo_dd dd_cos_kern(moo_dd r) {
    moo_dd r2 = dd_mul(r, r);
    moo_dd t = dd_aus(1.0);
    for (int32_t k = 15; k >= 1; k--) {
        double teiler = (double)((2 * k - 1) * (2 * k));
        t = dd_add(dd_aus(1.0), dd_neg(dd_div(dd_mul(r2, t), dd_aus(teiler))));
    }
    return t;
}

enum { TRIG_SIN = 0, TRIG_COS = 1, TRIG_TAN = 2 };

static double moo_native_trig(double x, int32_t art) {
    uint64_t ux = bits_f64(x);
    uint64_t ax = ux & ~(UINT64_C(1) << 63);
    if (ax >= (F64_EXP_MASK << F64_MANT_BITS)) return x - x;   /* +-Inf, NaN -> NaN */
    if (ax == 0u) return art == TRIG_COS ? 1.0 : x;             /* +-0 */
    double betrag = f64_bits(ax);
    moo_dd r;
    int32_t q;
    if (betrag < 0x1.921fb54442d18p-1) {                        /* < pi/4 */
        r = dd_aus(betrag);
        q = 0;
    } else {
        q = trig_reduziere(betrag, &r);
    }
    moo_dd v;
    if (art == TRIG_TAN) {
        moo_dd s = dd_sin_kern(r), c = dd_cos_kern(r);
        v = (q & 1) ? dd_neg(dd_div(c, s)) : dd_div(s, c);
    } else {
        /* cos(x) = sin(x + pi/2): Quadrant um eins weiter */
        int32_t quadrant = (q + (art == TRIG_COS ? 1 : 0)) & 3;
        v = (quadrant & 1) ? dd_cos_kern(r) : dd_sin_kern(r);
        if (quadrant & 2) v = dd_neg(v);
    }
    double ergebnis = v.hi + v.lo;
    /* sin und tan sind ungerade, cos ist gerade */
    return (art != TRIG_COS && (ux >> 63)) ? -ergebnis : ergebnis;
}

double moo_native_sin(double x) { return moo_native_trig(x, TRIG_SIN); }
double moo_native_cos(double x) { return moo_native_trig(x, TRIG_COS); }
double moo_native_tan(double x) { return moo_native_trig(x, TRIG_TAN); }
float moo_native_sinf(float x) { return (float)moo_native_trig((double)x, TRIG_SIN); }
float moo_native_cosf(float x) { return (float)moo_native_trig((double)x, TRIG_COS); }
float moo_native_tanf(float x) { return (float)moo_native_trig((double)x, TRIG_TAN); }

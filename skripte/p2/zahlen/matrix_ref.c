/* P2-T9/T58 Batch B3 (Plan 4500717b): C-Orakel fuer die native Wertschicht.
 * Liest Fallzeilen "<typ> <op> <a> [<b>]" von stdin (test.sh nimmt sie aus der
 * Ausgabe des von matrix_int.sh erzeugten Moo-Programms, Teil vor " = ") und
 * gibt je Fall "<fall> = <ergebnis>" im Format des Moo-Programms aus; test.sh
 * vergleicht beide Ausgaben byteweise. Das Orakel rechnet unabhaengig mit
 * <stdint.h>; die Fallliste existiert nur einmal (matrix_int.sh).
 * 64 Bit: Operanden "hi:lo" (u32-Haelften des Bitmusters), Ergebnis "hi lo".
 * f32/f64 (B2, matrix_float.sh): IEEE ohne Fast-Math (Uebersetzung mit
 * -ffp-contract=off), Ausgabe wie moo_to_string (ganzzahlig %lld, sonst %g).
 * P2-T58 D2c: rem/pow/remkehr/fnan gegen fmod(f)/pow(f) aus libm - libm nur
 * hier im Orakel; Moo rechnet ueber compiler/runtime/moo_native_math.c.
 * UB-frei nach ub-arithmetik-policy: Wrap ueber uint64_t, kein Cast nach
 * intN_t ausserhalb des Bereichs, kein signed >> auf negativen Werten, keine
 * int-Promotion mit Ueberlauf; Division durch 0 und MIN/-1 sind kein Fall. */
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Kuerzt v auf N Bit. */
static uint64_t maske(int bits, uint64_t v) {
    return bits == 64 ? v : v & ((UINT64_C(1) << bits) - 1u);
}
/* N-Bit-Bitmuster als Zweierkomplement (ohne implementation-defined Cast). */
static int64_t als_signed(int bits, uint64_t v) {
    uint64_t m = maske(bits, v);
    uint64_t vorzeichen = UINT64_C(1) << (bits - 1);
    uint64_t x = (m ^ vorzeichen) - vorzeichen;
    return x <= (uint64_t)INT64_MAX ? (int64_t)x : -(int64_t)(~x) - 1;
}

static int fehler(const char* zeile) {
    fprintf(stderr, "matrix_ref: ungueltiger Fall: %s\n", zeile);
    return 2;
}

/* Dezimal (signed/unsigned) oder "hi:lo" als Bitmuster modulo 2^64. */
static uint64_t operand(const char* text, int vorz) {
    const char* doppelpunkt = strchr(text, ':');
    if (doppelpunkt) {
        uint64_t hi = strtoull(text, NULL, 10), lo = strtoull(doppelpunkt + 1, NULL, 10);
        return (hi << 32) | (lo & UINT64_C(0xFFFFFFFF));
    }
    return vorz ? (uint64_t)strtoll(text, NULL, 10) : strtoull(text, NULL, 10);
}

/* Zahl wie moo_to_string (moo_print.c): ganzzahlig darstellbar -> %lld, sonst %g. */
static void zeige_zahl(const char* zeile, double x) {
    if (isfinite(x) && x >= -9.2e18 && x <= 9.2e18 && x == (double)(int64_t)x) {
        printf("%s = %lld\n", zeile, (long long)(int64_t)x);
    } else {
        printf("%s = %g\n", zeile, x);
    }
}

/* B2: f32/f64 nach IEEE ohne Fast-Math. f32 rechnet in float (SSE,
 * FLT_EVAL_METHOD 0); Operanden sind die f64-Werte des Moo-Programms. */
static int fall_float(const char* zeile, int bits, const char* op, const char* at, const char* bt) {
    double a = strtod(at, NULL), b = strtod(bt, NULL);
    float fa = (float)a, fb = (float)b;
    int vergleich = -1;
    double r = 0.0;
    if (bits == 32) {
        if (!strcmp(op, "add")) r = fa + fb;
        else if (!strcmp(op, "sub")) r = fa - fb;
        else if (!strcmp(op, "mul")) r = fa * fb;
        else if (!strcmp(op, "div")) r = fa / fb;
        else if (!strcmp(op, "neg")) r = -fa;
        else if (!strcmp(op, "kette")) { float p = fa * fb; r = p + fb; }
        else if (!strcmp(op, "negkehr")) r = 1.0f / -fa;
        else if (!strcmp(op, "negeq")) vergleich = -fa == fa;
        else if (!strcmp(op, "rund")) { float s = fa + fb; vergleich = (double)s != (double)fa + (double)fb; }
        else if (!strcmp(op, "rem")) r = fmodf(fa, fb);
        else if (!strcmp(op, "pow")) r = powf(fa, fb);
        else if (!strcmp(op, "remkehr")) r = 1.0f / fmodf(fa, fb);
        else if (!strcmp(op, "fnan")) vergleich = isnan(fmodf(fa, fb)) || isnan(powf(fa, fb));
        else if (!strcmp(op, "eq")) vergleich = fa == fb;
        else if (!strcmp(op, "ne")) vergleich = fa != fb;
        else if (!strcmp(op, "lt")) vergleich = fa < fb;
        else if (!strcmp(op, "le")) vergleich = fa <= fb;
        else if (!strcmp(op, "gt")) vergleich = fa > fb;
        else if (!strcmp(op, "ge")) vergleich = fa >= fb;
        else return fehler(zeile);
    } else {
        if (!strcmp(op, "add")) r = a + b;
        else if (!strcmp(op, "sub")) r = a - b;
        else if (!strcmp(op, "mul")) r = a * b;
        else if (!strcmp(op, "div")) r = a / b;
        else if (!strcmp(op, "neg")) r = -a;
        else if (!strcmp(op, "kette")) { double p = a * b; r = p + b; }
        else if (!strcmp(op, "negkehr")) r = 1.0 / -a;
        else if (!strcmp(op, "negeq")) vergleich = -a == a;
        else if (!strcmp(op, "rem")) r = fmod(a, b);
        else if (!strcmp(op, "pow")) r = pow(a, b);
        else if (!strcmp(op, "remkehr")) r = 1.0 / fmod(a, b);
        else if (!strcmp(op, "fnan")) vergleich = isnan(fmod(a, b)) || isnan(pow(a, b));
        else if (!strcmp(op, "eq")) vergleich = a == b;
        else if (!strcmp(op, "ne")) vergleich = a != b;
        else if (!strcmp(op, "lt")) vergleich = a < b;
        else if (!strcmp(op, "le")) vergleich = a <= b;
        else if (!strcmp(op, "gt")) vergleich = a > b;
        else if (!strcmp(op, "ge")) vergleich = a >= b;
        else return fehler(zeile);
    }
    if (vergleich >= 0) printf("%s = %s\n", zeile, vergleich ? "wahr" : "falsch");
    else if (isnan(r)) return fehler(zeile);  /* NaN-Ergebnisse sind kein Fall */
    else zeige_zahl(zeile, r);
    return 0;
}

static int fall(const char* zeile) {
    char typ[8], op[8];
    char a_text[32], b_text[32] = "";
    int n = sscanf(zeile, "%7s %7s %31s %31s", typ, op, a_text, b_text);
    if (n >= 3 && typ[0] == 'f' && (!strcmp(typ, "f32") || !strcmp(typ, "f64")))
        return fall_float(zeile, atoi(typ + 1), op, a_text, n == 4 ? b_text : "0");
    if (n < 3 || (typ[0] != 'i' && typ[0] != 'u')) return fehler(zeile);
    int vorz = typ[0] == 'i';
    int bits = atoi(typ + 1);
    if (bits != 8 && bits != 16 && bits != 32 && bits != 64) return fehler(zeile);
    /* Operanden als Bitmuster modulo 2^64 (signed sign-extendiert). */
    uint64_t a = operand(a_text, vorz);
    uint64_t b = n == 4 ? operand(b_text, vorz) : 0;
    int64_t sa = als_signed(bits, a), sb = als_signed(bits, b);
    uint64_t ua = maske(bits, a), ub = maske(bits, b);
    int teilbar = ub != 0 && !(vorz && sb == -1 && sa == als_signed(bits, UINT64_C(1) << (bits - 1)));
    int vergleich = -1;
    uint64_t r = 0;
    /* lokal (A1-Nachweis): Moo rechnet a + b ueber Locals anderer Typen. */
    if (!strcmp(op, "add") || !strcmp(op, "lokal")) r = a + b;
    else if (!strcmp(op, "sub")) r = a - b;
    else if (!strcmp(op, "mul")) r = a * b;
    else if (!strcmp(op, "div")) { if (!teilbar) return fehler(zeile); r = vorz ? (uint64_t)(sa / sb) : ua / ub; }
    else if (!strcmp(op, "rem")) { if (!teilbar) return fehler(zeile); r = vorz ? (uint64_t)(sa % sb) : ua % ub; }
    else if (!strcmp(op, "and")) r = a & b;
    else if (!strcmp(op, "or")) r = a | b;
    else if (!strcmp(op, "xor")) r = a ^ b;
    else if (!strcmp(op, "shl")) { if (ub >= (uint64_t)bits) return fehler(zeile); r = ua << ub; }
    else if (!strcmp(op, "shr")) {
        if (ub >= (uint64_t)bits) return fehler(zeile);
        /* arithmetisch fuer signed: ~(~x >> n) bei negativem x */
        r = vorz && sa < 0 ? ~(~(uint64_t)sa >> ub) : (vorz ? (uint64_t)sa : ua) >> ub;
    }
    else if (!strcmp(op, "pow")) {
        /* P2-T58: Quadrieren und Multiplizieren modulo 2^64, danach Maske = Wrap
           modulo 2^N; negativer Exponent (signed) ist der Fehlervertrag: Moo
           faengt ihn und gibt 'fehler' aus (Review 06d10060 #6). */
        if (vorz && sb < 0) { printf("%s = fehler\n", zeile); return 0; }
        uint64_t basis = a, exponent = ub;
        r = 1;
        while (exponent) {
            if (exponent & 1u) r *= basis;
            basis *= basis;
            exponent >>= 1;
        }
    }
    else if (!strcmp(op, "not")) r = ~a;
    else if (!strcmp(op, "neg")) r = UINT64_C(0) - a;
    else if (!strcmp(op, "eq")) vergleich = vorz ? sa == sb : ua == ub;
    else if (!strcmp(op, "ne")) vergleich = vorz ? sa != sb : ua != ub;
    else if (!strcmp(op, "lt")) vergleich = vorz ? sa < sb : ua < ub;
    else if (!strcmp(op, "le")) vergleich = vorz ? sa <= sb : ua <= ub;
    else if (!strcmp(op, "gt")) vergleich = vorz ? sa > sb : ua > ub;
    else if (!strcmp(op, "ge")) vergleich = vorz ? sa >= sb : ua >= ub;
    else return fehler(zeile);
    if (vergleich >= 0) {
        printf("%s = %s\n", zeile, vergleich ? "wahr" : "falsch");
    } else if (bits == 64) {
        printf("%s = %" PRIu32 " %" PRIu32 "\n", zeile, (uint32_t)(r >> 32), (uint32_t)r);
    } else if (vorz) {
        printf("%s = %" PRId64 "\n", zeile, als_signed(bits, r));
    } else {
        printf("%s = %" PRIu64 "\n", zeile, maske(bits, r));
    }
    return 0;
}

int main(void) {
    char zeile[128];
    int faelle = 0;
    while (fgets(zeile, sizeof zeile, stdin)) {
        zeile[strcspn(zeile, "\r\n")] = '\0';
        if (!zeile[0]) continue;
        if (fall(zeile)) return 2;
        faelle++;
    }
    return faelle ? 0 : 2;
}

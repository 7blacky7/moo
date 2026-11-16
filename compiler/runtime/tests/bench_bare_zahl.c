/* P7-T19: Mikro-Bench des Bare-Zahlpfads (moo_bare.c) vor/nach P7-T17.
 *
 * Der Codegen ruft die Bare-Runtime als externe Funktionen auf (kein LTO).
 * Gemessen wird die typische Zaehlschleife
 *     solange i < n: s = s + i * 2; i = i + 1
 * = je Runde moo_lt, moo_is_truthy, moo_mul, 2x moo_add.
 *
 * "vor"  = Referenz: die Funktionen exakt aus moo_bare.c files-Version 207403
 *          (Stand vor T17: as_num = reines memcpy), noinline wie externe Calls.
 * "nach" = aktuelle moo_bare.c, separat uebersetzt und dazugelinkt.
 * Bau/Lauf: skripte/p7/bare_rechenleistung.sh. Ausgabe: ns je Runde, Minimum
 * aus 7 Laeufen, plus Pruefsumme (muss in beiden Varianten gleich sein). */
#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct { uint64_t tag; uint64_t data; } MooValue;

MooValue moo_number(double n);
MooValue moo_add(MooValue a, MooValue b);
MooValue moo_mul(MooValue a, MooValue b);
MooValue moo_lt(MooValue a, MooValue b);
bool moo_is_truthy(MooValue v);

/* --- Referenz vor T17 (Version 207403) --- */
static inline double ref_as_num(MooValue v) { double d; __builtin_memcpy(&d, &v.data, sizeof d); return d; }
static inline MooValue ref_number(double n) { MooValue v; v.tag = 0; __builtin_memcpy(&v.data, &n, sizeof n); return v; }
static inline MooValue ref_bool(bool b) { MooValue v; v.tag = 2; v.data = (uint64_t)b; return v; }
__attribute__((noinline)) MooValue ref_add(MooValue a, MooValue b) { return ref_number(ref_as_num(a) + ref_as_num(b)); }
__attribute__((noinline)) MooValue ref_mul(MooValue a, MooValue b) { return ref_number(ref_as_num(a) * ref_as_num(b)); }
__attribute__((noinline)) MooValue ref_lt(MooValue a, MooValue b) { return ref_bool(ref_as_num(a) < ref_as_num(b)); }
__attribute__((noinline)) bool ref_truthy(MooValue v) {
    if (v.tag == 0) return ref_as_num(v) != 0.0;
    if (v.tag == 2) return (bool)v.data;
    return false;
}

typedef MooValue (*Bin)(MooValue, MooValue);
typedef bool (*Wahr)(MooValue);

static double jetzt_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static double lauf(Bin add, Bin mul, Bin lt, Wahr wahr, long n, double* summe) {
    MooValue i = ref_number(0), s = ref_number(0), zwei = ref_number(2), eins = ref_number(1);
    MooValue grenze = ref_number((double)n);
    double t0 = jetzt_ns();
    while (wahr(lt(i, grenze))) {
        s = add(s, mul(i, zwei));
        i = add(i, eins);
    }
    double t1 = jetzt_ns();
    *summe = ref_as_num(s);
    return (t1 - t0) / (double)n;
}

int main(int argc, char** argv) {
    long n = argc > 1 ? atol(argv[1]) : 50000000L;
    double best_vor = 1e30, best_nach = 1e30, s_vor = 0, s_nach = 0;
    /* volatile Zeiger: verhindert, dass der Compiler die Referenz inlinet */
    Bin volatile r_add = ref_add, r_mul = ref_mul, r_lt = ref_lt;
    Wahr volatile r_w = ref_truthy;
    Bin volatile n_add = moo_add, n_mul = moo_mul, n_lt = moo_lt;
    Wahr volatile n_w = moo_is_truthy;
    for (int k = 0; k < 7; ++k) {
        double a = lauf(r_add, r_mul, r_lt, r_w, n, &s_vor);
        double b = lauf(n_add, n_mul, n_lt, n_w, n, &s_nach);
        if (a < best_vor) best_vor = a;
        if (b < best_nach) best_nach = b;
    }
    printf("BENCH runden=%ld vor_ns=%.3f nach_ns=%.3f delta_pct=%.2f summe_gleich=%s\n",
           n, best_vor, best_nach, (best_nach / best_vor - 1.0) * 100.0,
           s_vor == s_nach ? "ja" : "NEIN");
    return s_vor == s_nach ? 0 : 1;
}

/**
 * moo_bare.c — Minimale Bare-Metal Runtime fuer Embedded (ESP32, STM32, RP2040).
 * KEIN malloc, KEIN printf, KEIN stdlib.
 * Nur Zahlen-Arithmetik + volatile Memory-I/O.
 *
 * Wird mit --no-stdlib kompiliert und ersetzt die volle moo_runtime.
 */

#include <stdint.h>
#include <stdbool.h>

// === MooValue (gleiche 16-Byte Struktur wie volle Runtime) ===
typedef enum {
    MOO_NUMBER = 0,
    MOO_BOOL   = 2,
    MOO_NONE   = 3,
} BareTag;

typedef struct {
    uint64_t tag;
    uint64_t data;
} MooValue;

// Zugriff
static inline double bare_as_double(MooValue v) {
    double d;
    __builtin_memcpy(&d, &v.data, sizeof(double));
    return d;
}
static inline void bare_set_double(MooValue* v, double d) {
    __builtin_memcpy(&v->data, &d, sizeof(double));
}

// === Konstruktoren ===
MooValue moo_number(double n) {
    MooValue v;
    v.tag = MOO_NUMBER;
    bare_set_double(&v, n);
    return v;
}

MooValue moo_bool(bool b) {
    MooValue v;
    v.tag = MOO_BOOL;
    v.data = (uint64_t)b;
    return v;
}

MooValue moo_none(void) {
    MooValue v;
    v.tag = MOO_NONE;
    v.data = 0;
    return v;
}

// === Arithmetik (direkt, kein malloc) ===
static inline double as_num(MooValue v) { return bare_as_double(v); }

MooValue moo_add(MooValue a, MooValue b) { return moo_number(as_num(a) + as_num(b)); }
MooValue moo_sub(MooValue a, MooValue b) { return moo_number(as_num(a) - as_num(b)); }
MooValue moo_mul(MooValue a, MooValue b) { return moo_number(as_num(a) * as_num(b)); }
MooValue moo_div(MooValue a, MooValue b) { return moo_number(as_num(a) / as_num(b)); }
MooValue moo_mod(MooValue a, MooValue b) {
    double bv = as_num(b);
    double av = as_num(a);
    return moo_number(av - (double)(int64_t)(av / bv) * bv);
}
MooValue moo_neg(MooValue v) { return moo_number(-as_num(v)); }

// === Vergleiche ===
MooValue moo_eq(MooValue a, MooValue b) { return moo_bool(as_num(a) == as_num(b)); }
MooValue moo_neq(MooValue a, MooValue b) { return moo_bool(as_num(a) != as_num(b)); }
MooValue moo_lt(MooValue a, MooValue b) { return moo_bool(as_num(a) < as_num(b)); }
MooValue moo_gt(MooValue a, MooValue b) { return moo_bool(as_num(a) > as_num(b)); }
MooValue moo_lte(MooValue a, MooValue b) { return moo_bool(as_num(a) <= as_num(b)); }
MooValue moo_gte(MooValue a, MooValue b) { return moo_bool(as_num(a) >= as_num(b)); }

// === Logik ===
bool moo_is_truthy(MooValue v) {
    if (v.tag == MOO_NUMBER) return as_num(v) != 0.0;
    if (v.tag == MOO_BOOL) return (bool)v.data;
    return false;
}
MooValue moo_and(MooValue a, MooValue b) { return moo_bool(moo_is_truthy(a) && moo_is_truthy(b)); }
MooValue moo_or(MooValue a, MooValue b) { return moo_bool(moo_is_truthy(a) || moo_is_truthy(b)); }
MooValue moo_not(MooValue v) { return moo_bool(!moo_is_truthy(v)); }

// === Bitwise ===
MooValue moo_bitand(MooValue a, MooValue b) { return moo_number((double)((int64_t)as_num(a) & (int64_t)as_num(b))); }
MooValue moo_bitor(MooValue a, MooValue b) { return moo_number((double)((int64_t)as_num(a) | (int64_t)as_num(b))); }
MooValue moo_bitxor(MooValue a, MooValue b) { return moo_number((double)((int64_t)as_num(a) ^ (int64_t)as_num(b))); }
MooValue moo_bitnot(MooValue v) { return moo_number((double)(~(int64_t)as_num(v))); }
MooValue moo_lshift(MooValue a, MooValue b) { return moo_number((double)((int64_t)as_num(a) << (int64_t)as_num(b))); }
MooValue moo_rshift(MooValue a, MooValue b) { return moo_number((double)((int64_t)as_num(a) >> (int64_t)as_num(b))); }

// === Volatile Memory-Mapped I/O ===
MooValue moo_mem_read(MooValue addr, MooValue size) {
    uintptr_t a = (uintptr_t)as_num(addr);
    int s = (int)as_num(size);
    if (s == 1) return moo_number((double)(*(volatile uint8_t*)a));
    if (s == 2) return moo_number((double)(*(volatile uint16_t*)a));
    if (s == 4) return moo_number((double)(*(volatile uint32_t*)a));
    return moo_none();
}

void moo_mem_write(MooValue addr, MooValue value, MooValue size) {
    uintptr_t a = (uintptr_t)as_num(addr);
    uint64_t v = (uint64_t)as_num(value);
    int s = (int)as_num(size);
    if (s == 1) *(volatile uint8_t*)a = (uint8_t)v;
    else if (s == 2) *(volatile uint16_t*)a = (uint16_t)v;
    else if (s == 4) *(volatile uint32_t*)a = (uint32_t)v;
}

// === Retain/Release (no-ops im Bare-Metal Modus) ===
void moo_retain(MooValue v) { (void)v; }
void moo_release(MooValue v) { (void)v; }

// === Stubs fuer Funktionen die der Codegen referenziert ===
// Diese tun nichts im Bare-Metal Modus
MooValue moo_pow(MooValue a, MooValue b) {
    // Einfache Integer-Power ohne libm
    double base = as_num(a);
    int exp = (int)as_num(b);
    if (exp < 0) return moo_number(0.0);
    double result = 1.0;
    for (int i = 0; i < exp; i++) result *= base;
    return result == result ? moo_number(result) : moo_none();
}

bool moo_is_none(MooValue v) { return v.tag == MOO_NONE; }
void moo_throw(MooValue v) { (void)v; /* Kein Error-Handling im Bare-Metal */ }
void moo_print(MooValue v) { (void)v; /* Kein printf im Bare-Metal */ }

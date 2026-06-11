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
// P007-U3: Shift-UB analog zu moo_ops.c #7 vermeiden. Bare-Metal hat KEIN
// Error-Handling (moo_throw ist hier ein No-Op, kein moo_error/snprintf),
// daher kann der ungueltige Count nicht geworfen werden — wir maskieren ihn
// stattdessen auf 0..63 (`& 63`, definiert, kein UB) und schieben den Wert als
// uint64_t (lshift, definierter Wrap) bzw. signed int64_t (rshift, bewusst
// arithmetisch, wie auf den Zielplattformen). Damit ist der Embedded-Pfad
// UB-frei; eine Voll-Validierung mit Fehlerwurf bleibt dem Hosted-Pfad (ops.c).
MooValue moo_lshift(MooValue a, MooValue b) {
    uint64_t ua = (uint64_t)(int64_t)as_num(a);
    unsigned cnt = (unsigned)((int64_t)as_num(b) & 63);
    return moo_number((double)(int64_t)(ua << cnt));
}
MooValue moo_rshift(MooValue a, MooValue b) {
    int64_t sa = (int64_t)as_num(a);
    unsigned cnt = (unsigned)((int64_t)as_num(b) & 63);
    return moo_number((double)(sa >> cnt));
}

// === Volatile Memory-Mapped I/O ===
MooValue moo_mem_read(MooValue addr, MooValue size) {
    uintptr_t a = (uintptr_t)as_num(addr);
    int s = (int)as_num(size);
    if (s == 1) return moo_number((double)(*(volatile uint8_t*)a));
    if (s == 2) return moo_number((double)(*(volatile uint16_t*)a));
    if (s == 4) return moo_number((double)(*(volatile uint32_t*)a));
    if (s == 8) return moo_number((double)(*(volatile uint64_t*)a));
    return moo_none();
}

void moo_mem_write(MooValue addr, MooValue value, MooValue size) {
    uintptr_t a = (uintptr_t)as_num(addr);
    uint64_t v = (uint64_t)as_num(value);
    int s = (int)as_num(size);
    if (s == 1) *(volatile uint8_t*)a = (uint8_t)v;
    else if (s == 2) *(volatile uint16_t*)a = (uint16_t)v;
    else if (s == 4) *(volatile uint32_t*)a = (uint32_t)v;
    else if (s == 8) *(volatile uint64_t*)a = (uint64_t)v;
}

// === CPU / Hardware Builtins ===
void moo_cpu_halt(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("hlt" : : : "memory");
#elif defined(__arm__) || defined(__aarch64__) || defined(__riscv)
    __asm__ volatile("wfi" : : : "memory");
#else
    while(1);
#endif
}

void moo_cpu_cli(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("cli" : : : "memory");
#endif
}

void moo_cpu_sti(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("sti" : : : "memory");
#endif
}

MooValue moo_io_inb(MooValue port) {
    uint16_t p = (uint16_t)as_num(port);
    uint8_t value = 0;
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(p));
#endif
    return moo_number((double)value);
}

void moo_io_outb(MooValue port, MooValue data) {
    uint16_t p = (uint16_t)as_num(port);
    uint8_t d = (uint8_t)as_num(data);
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("outb %0, %1" : : "a"(d), "Nd"(p));
#endif
}

// === C-seitige Port-Helfer (ohne MooValue-Boxing) ===
// Genutzt von moo_bare_console.c / _idt.c / _boot.c (Plan-010, kern_*).
uint8_t kern_inb(uint16_t port) {
    uint8_t value = 0;
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
#endif
    return value;
}

void kern_outb(uint16_t port, uint8_t value) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
#endif
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

// moo_throw/moo_print sind WEAK: ein Kernel-Build mit moo_bare_console.c
// liefert eine STARKE moo_print (seriell + VGA). Ohne Console bleiben diese
// No-Op-Defaults aktiv (reiner Embedded-Pfad ohne Ausgabe).
__attribute__((weak)) void moo_throw(MooValue v) { (void)v; /* Kein Error-Handling im Bare-Metal */ }
__attribute__((weak)) void moo_print(MooValue v) { (void)v; /* Kein printf im Bare-Metal */ }

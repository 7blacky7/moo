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
// PORTABLER Geraete-Zugriff fuer ALLE Targets: x86-MMIO ebenso wie
// ARM/RISC-V-Geraeteregister (PL011-UART, GIC, CLINT, ...). Auf ARM und
// RISC-V ist MMIO der EINZIGE Geraete-Pfad — Port-I/O (unten) bleibt
// strikt x86-only. Semantik/UB-Policy:
//   - Zugriff strikt volatile in nativer Breite 1/2/4/8 Byte.
//   - Casts via uintptr_t/uint64_t (unsigned, definiert, kein UB).
//   - Adress-/Wert-Transport laeuft ueber MooValue-double: exakt bis
//     2^53 — reale MMIO-Fenster liegen weit darunter; fuer volle
//     64-bit-WERTE (z.B. 64-bit-Counter-Register) gilt dieselbe
//     2^53-Grenze. Wo das nicht reicht: zwei 32-bit-Zugriffe.
//   - Ungueltige Breite: read -> moo_none, write -> no-op (kein Trap).
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

// C-seitige MMIO-Helfer (ohne MooValue-Boxing) — P012-B3. Fuer die
// kern_*-Module der portablen Targets (z.B. PL011-Konsole auf
// qemu-virt-aarch64, P012-D1). Muster analog kern_inb/kern_outb.
uint8_t  kern_mmio_read8(uintptr_t addr)                  { return *(volatile uint8_t*)addr; }
uint16_t kern_mmio_read16(uintptr_t addr)                 { return *(volatile uint16_t*)addr; }
uint32_t kern_mmio_read32(uintptr_t addr)                 { return *(volatile uint32_t*)addr; }
uint64_t kern_mmio_read64(uintptr_t addr)                 { return *(volatile uint64_t*)addr; }
void     kern_mmio_write8(uintptr_t addr, uint8_t v)      { *(volatile uint8_t*)addr = v; }
void     kern_mmio_write16(uintptr_t addr, uint16_t v)    { *(volatile uint16_t*)addr = v; }
void     kern_mmio_write32(uintptr_t addr, uint32_t v)    { *(volatile uint32_t*)addr = v; }
void     kern_mmio_write64(uintptr_t addr, uint64_t v)    { *(volatile uint64_t*)addr = v; }

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

// === Port-I/O — STRIKT x86/i386-ONLY (P012-B3) ===
// in/out-Instruktionen existieren NUR auf x86. Auf allen anderen Targets
// (ARM/AArch64/RISC-V) sind diese Funktionen EXPLIZITE no-op-Stubs:
// in* liefert 0, out* tut nichts. Das ist BEWUSST nur fuer object-compile
// gedacht (Programm baut, Port-Aufrufe sind dort wirkungslos) — KEINE
// ARM-Register-Constraint-Nachbauten, KEIN Port->MMIO-Mapping. Portable
// Geraete-Pfade nutzen das MMIO-API oben (speicher_lesen/schreiben bzw.
// kern_mmio_*).
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

// === P011-B1: 16/32-bit Port-I/O (Muster wie inb/outb) ===
// MOO_BARE_PORT_STUB: Host-Test-Harness ersetzt echte Port-asm durch
// Loopback-Register (echte in/out ist im Userspace privilegiert -> #GP).
#ifdef MOO_BARE_PORT_STUB
static uint32_t kern_port_stub[65536];
#endif

uint16_t kern_inw(uint16_t port) {
#ifdef MOO_BARE_PORT_STUB
    return (uint16_t)kern_port_stub[port];
#else
    uint16_t value = 0;
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
#endif
    return value;
#endif
}

void kern_outw(uint16_t port, uint16_t value) {
#ifdef MOO_BARE_PORT_STUB
    kern_port_stub[port] = value;
#else
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
#endif
#endif
}

uint32_t kern_inl(uint16_t port) {
#ifdef MOO_BARE_PORT_STUB
    return kern_port_stub[port];
#else
    uint32_t value = 0;
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
#endif
    return value;
#endif
}

void kern_outl(uint16_t port, uint32_t value) {
#ifdef MOO_BARE_PORT_STUB
    kern_port_stub[port] = value;
#else
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
#endif
#endif
}

// MooValue-Wrapper (P011-B1) — Truncation via Cast (unsigned, kein UB)
MooValue moo_io_inw(MooValue port) {
    return moo_number((double)kern_inw((uint16_t)as_num(port)));
}
void moo_io_outw(MooValue port, MooValue data) {
    kern_outw((uint16_t)as_num(port), (uint16_t)as_num(data));
}
MooValue moo_io_inl(MooValue port) {
    return moo_number((double)kern_inl((uint16_t)as_num(port)));
}
void moo_io_outl(MooValue port, MooValue data) {
    kern_outl((uint16_t)as_num(port), (uint32_t)as_num(data));
}

// === P011-B2: CPU-Steuer-Builtins (rdmsr/wrmsr, CRx, lgdt/lidt) ===
// Alle privilegiert — nur im Kernel-Kontext ausfuehrbar; der Host-Harness
// kompiliert diese Datei lediglich mit (Ausfuehrung im Userspace -> #GP).
#if defined(__x86_64__)
static void kern_rdmsr_c(uint32_t msr, uint32_t* lo, uint32_t* hi) {
    uint32_t a = 0, d = 0;
    __asm__ volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(msr));
    *lo = a; *hi = d;
}
static void kern_wrmsr_c(uint32_t msr, uint32_t lo, uint32_t hi) {
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}
static uint64_t kern_cr_lese_c(int n) {
    uint64_t v = 0;
    switch (n) {
        case 0: __asm__ volatile("mov %%cr0, %0" : "=r"(v)); break;
        case 2: __asm__ volatile("mov %%cr2, %0" : "=r"(v)); break;
        case 3: __asm__ volatile("mov %%cr3, %0" : "=r"(v)); break;
        case 4: __asm__ volatile("mov %%cr4, %0" : "=r"(v)); break;
        default: break;
    }
    return v;
}
static void kern_cr_setze_c(int n, uint64_t v) {
    switch (n) {
        case 0: __asm__ volatile("mov %0, %%cr0" : : "r"(v) : "memory"); break;
        case 2: __asm__ volatile("mov %0, %%cr2" : : "r"(v) : "memory"); break;
        case 3: __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory"); break;
        case 4: __asm__ volatile("mov %0, %%cr4" : : "r"(v) : "memory"); break;
        default: break;
    }
}
#endif

// MSR-Lesen als lo/hi-Split: EDX:EAX bewusst getrennt (double traegt nur
// 53 Mantissen-Bits — keine 64-bit-Komposition in moo-Zahlen).
MooValue kern_rdmsr_lo(MooValue msr) {
#if defined(__x86_64__)
    uint32_t lo = 0, hi = 0;
    kern_rdmsr_c((uint32_t)as_num(msr), &lo, &hi);
    return moo_number((double)lo);
#else
    (void)msr; return moo_number(0.0);
#endif
}

MooValue kern_rdmsr_hi(MooValue msr) {
#if defined(__x86_64__)
    uint32_t lo = 0, hi = 0;
    kern_rdmsr_c((uint32_t)as_num(msr), &lo, &hi);
    return moo_number((double)hi);
#else
    (void)msr; return moo_number(0.0);
#endif
}

void kern_wrmsr(MooValue msr, MooValue lo, MooValue hi) {
#if defined(__x86_64__)
    kern_wrmsr_c((uint32_t)as_num(msr), (uint32_t)as_num(lo), (uint32_t)as_num(hi));
#else
    (void)msr; (void)lo; (void)hi;
#endif
}

// CR-Werte > 2^53 sind theoretisch moeglich (PoC-dokumentiert akzeptiert;
// praktisch: CR0/CR4-Flagbits + CR3 < 2^48).
MooValue kern_cr_lese(MooValue n) {
#if defined(__x86_64__)
    return moo_number((double)kern_cr_lese_c((int)as_num(n)));
#else
    (void)n; return moo_number(0.0);
#endif
}

void kern_cr_setze(MooValue n, MooValue wert) {
#if defined(__x86_64__)
    kern_cr_setze_c((int)as_num(n), (uint64_t)as_num(wert));
#else
    (void)n; (void)wert;
#endif
}

void kern_lgdt(MooValue basis, MooValue limit) {
#if defined(__x86_64__)
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } d;
    d.limit = (uint16_t)as_num(limit);
    d.base  = (uint64_t)as_num(basis);
    __asm__ volatile("lgdt %0" : : "m"(d));
#else
    (void)basis; (void)limit;
#endif
}

void kern_lidt(MooValue basis, MooValue limit) {
#if defined(__x86_64__)
    struct __attribute__((packed)) { uint16_t limit; uint64_t base; } d;
    d.limit = (uint16_t)as_num(limit);
    d.base  = (uint64_t)as_num(basis);
    __asm__ volatile("lidt %0" : : "m"(d));
#else
    (void)basis; (void)limit;
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

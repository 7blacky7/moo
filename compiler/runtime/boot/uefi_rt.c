/* P011-F1: Minimale moo-Runtime-Stubs fuer UEFI/COFF (aus E1-PoC).
 *
 * Diese Stubs decken genau die Symbole ab, die der moo-Codegen im
 * bare-UEFI-Pfad referenziert (Port-I/O-Builtins + MooValue-Boxing).
 * MooValue ist {i64 tag; i64 data} = 16 Byte, double-Bits liegen in data.
 *
 * Mit clang --target=x86_64-unknown-windows -ffreestanding kompilieren,
 * damit die MS-x64-ABI konsistent zum moo-COFF-Objekt ist.
 */
#include <stdint.h>
typedef struct { int64_t tag; int64_t data; } MooValue;

static double as_num(MooValue v) { union { int64_t i; double d; } u; u.i = v.data; return u.d; }

MooValue moo_number(double n) { union { int64_t i; double d; } u; u.d = n; MooValue v = {0, u.i}; return v; }
MooValue moo_none(void) { MooValue v = {3, 0}; return v; }
void moo_retain(MooValue v) { (void)v; }
void moo_release(MooValue v) { (void)v; }

MooValue moo_io_inw(MooValue port) {
    uint16_t p = (uint16_t)as_num(port), val = 0;
    __asm__ volatile("inw %1,%0" : "=a"(val) : "Nd"(p));
    return moo_number((double)val);
}
void moo_io_outw(MooValue port, MooValue data) {
    uint16_t p = (uint16_t)as_num(port), d = (uint16_t)as_num(data);
    __asm__ volatile("outw %0,%1" :: "a"(d), "Nd"(p));
}
MooValue moo_io_inl(MooValue port) {
    uint16_t p = (uint16_t)as_num(port); uint32_t val = 0;
    __asm__ volatile("inl %1,%0" : "=a"(val) : "Nd"(p));
    return moo_number((double)val);
}
void moo_io_outl(MooValue port, MooValue data) {
    uint16_t p = (uint16_t)as_num(port); uint32_t d = (uint32_t)as_num(data);
    __asm__ volatile("outl %0,%1" :: "a"(d), "Nd"(p));
}

/* MSVC-Float-Marker: lld-link verlangt _fltused sobald Floats vorkommen. */
int _fltused = 0;

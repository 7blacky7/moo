/* test_bare_mmio_asan.c — P012-B3: Portables MMIO-API auf dem Host.
 *
 * Testet moo_mem_read/moo_mem_write (MooValue-API, volatile 1/2/4/8)
 * und die kern_mmio_*-C-Helfer gegen eine malloc-Arena — KEINE echten
 * Hardware-Adressen. ASan ueberwacht die Arena-Raender, UBSan die
 * uintptr_t/uint64_t-Casts (UB-Policy: unsigned, definiert).
 * malloc-Adressen liegen auf x86_64 < 2^48 -> exakt in double
 * darstellbar (MooValue-Transportgrenze 2^53 dokumentiert in moo_bare.c).
 *
 * Link: test_bare_mmio_asan.c moo_bare.c (Port-Funktionen werden nur
 * gelinkt, nie aufgerufen — echte in/out-asm wuerde im Userspace #GP).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "moo_bare_kern.h"

static int g_checks = 0;
static int g_fails  = 0;

#define CHECK(cond, msg) do {                                              \
    g_checks++;                                                            \
    if (!(cond)) {                                                         \
        g_fails++;                                                         \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));    \
    }                                                                      \
} while (0)

/* MooValue-API aus moo_bare.c (dort definiert, Header deklariert nur
 * die kern_*-Sicht — mem_read/write sind moo_*-Symbole). */
MooValue moo_mem_read(MooValue addr, MooValue size);
void     moo_mem_write(MooValue addr, MooValue value, MooValue size);

static MooValue num(double d) { return moo_number(d); }
static double as_d(MooValue v) {
    double d;
    __builtin_memcpy(&d, &v.data, sizeof(double));
    return d;
}

int main(void) {
    /* 64-Byte-Arena, 8-aligned via malloc. */
    uint8_t* arena = (uint8_t*)malloc(64);
    CHECK(arena != NULL, "malloc-Arena");
    memset(arena, 0, 64);
    double base = (double)(uintptr_t)arena;

    /* (1) MooValue-API: Roundtrip je Breite an disjunkten Offsets. */
    moo_mem_write(num(base + 0),  num(0xAB),               num(1));
    moo_mem_write(num(base + 8),  num(0xBEEF),             num(2));
    moo_mem_write(num(base + 16), num(3735928559.0),       num(4)); /* 0xDEADBEEF */
    moo_mem_write(num(base + 24), num(1250999896491.0),   num(8)); /* 0x123456789AB < 2^53, exakt */
    CHECK(as_d(moo_mem_read(num(base + 0),  num(1))) == 171.0, "read8 == 0xAB");
    CHECK(as_d(moo_mem_read(num(base + 8),  num(2))) == 48879.0, "read16 == 0xBEEF");
    CHECK(as_d(moo_mem_read(num(base + 16), num(4))) == 3735928559.0, "read32 == 0xDEADBEEF");
    CHECK(as_d(moo_mem_read(num(base + 24), num(8))) == 1250999896491.0, "read64 == 0x123456789AB");

    /* (2) kern_mmio_*-Helfer: Roundtrip + Konsistenz mit MooValue-API. */
    kern_mmio_write8((uintptr_t)(arena + 32), 0x5A);
    kern_mmio_write16((uintptr_t)(arena + 34), 0xC0DE);
    kern_mmio_write32((uintptr_t)(arena + 36), 0xCAFEBABEu);
    kern_mmio_write64((uintptr_t)(arena + 40), 0x1122334455667788ull);
    CHECK(kern_mmio_read8((uintptr_t)(arena + 32))  == 0x5A, "kern read8");
    CHECK(kern_mmio_read16((uintptr_t)(arena + 34)) == 0xC0DE, "kern read16");
    CHECK(kern_mmio_read32((uintptr_t)(arena + 36)) == 0xCAFEBABEu, "kern read32");
    CHECK(kern_mmio_read64((uintptr_t)(arena + 40)) == 0x1122334455667788ull, "kern read64");
    CHECK(as_d(moo_mem_read(num(base + 32), num(1))) == (double)0x5A, "API liest kern-Write (8)");
    CHECK(as_d(moo_mem_read(num(base + 36), num(4))) == (double)0xCAFEBABEu, "API liest kern-Write (32)");

    /* (3) Ungueltige Breite: read -> none, write -> no-op. */
    MooValue bad = moo_mem_read(num(base), num(3));
    CHECK(bad.tag == MOO_NONE, "read size=3 -> none");
    uint8_t before = arena[48];
    moo_mem_write(num(base + 48), num(0xFF), num(3));
    CHECK(arena[48] == before, "write size=3 -> no-op");

    free(arena);
    printf("test_bare_mmio: %d checks, %d fails\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}

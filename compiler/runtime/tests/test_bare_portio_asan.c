// P011-B1 Harness: 16/32-bit Port-I/O Wrapper-Logik (Truncation, Roundtrip).
// Echte in/out-asm ist im Userspace privilegiert (#GP) — deshalb wird
// moo_bare.c mit -DMOO_BARE_PORT_STUB gebaut: Loopback-Register statt asm.
// Build (run_sanitize.sh): cc -fsanitize=... -DMOO_BARE_PORT_STUB \
//   test_bare_portio_asan.c ../moo_bare.c ../moo_bare_console.c
#include <stdio.h>
#include "../moo_bare_kern.h"

static int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else { printf("ok:   %s\n", msg); } \
} while (0)

int main(void) {
    // 16-bit Roundtrip
    kern_outw(0x3F8, 0xBEEF);
    CHECK(kern_inw(0x3F8) == 0xBEEF, "kern_outw/inw Roundtrip 0xBEEF");
    // 16-bit Truncation im MooValue-Wrapper: 65537 -> 1
    moo_io_outw(moo_number(0x10), moo_number(65537.0));
    MooValue r16 = moo_io_inw(moo_number(0x10));
    CHECK(kern_as_double(r16) == 1.0, "moo_io_outw truncated 65537 -> 1");
    // 32-bit Roundtrip exakt (double traegt 32 bit verlustfrei)
    kern_outl(0xCF8, 0xDEADBEEFu);
    CHECK(kern_inl(0xCF8) == 0xDEADBEEFu, "kern_outl/inl Roundtrip 0xDEADBEEF");
    moo_io_outl(moo_number(0x20), moo_number(4294967295.0));
    MooValue r32 = moo_io_inl(moo_number(0x20));
    CHECK(kern_as_double(r32) == 4294967295.0, "moo_io_outl/inl Roundtrip 2^32-1");
    // Port-Truncation: Port 65536+8 -> Port 8 (uint16_t-Cast)
    kern_outw(8, 0x1234);
    MooValue rp = moo_io_inw(moo_number(65544.0));
    CHECK(kern_as_double(rp) == (double)0x1234, "Port-Nummer wrapt auf uint16");
    printf(fails == 0 ? "ALLE PORTIO-TESTS GRUEN\n" : "%d FEHLER\n", fails);
    return fails == 0 ? 0 : 1;
}

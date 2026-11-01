// P7-T4: C++-Probe fuer die nativen Hardwarefunktionen aus volatile_mmio.moos
// (gegen normalen Speicher; Barrieren laufen als echte mfence/lfence/sfence).
#include <cstdint>
#include <cstdio>
extern "C" std::uint32_t hw_lesen(const volatile std::uint32_t*) asm("__moo_native.hw_reg_lesen");
extern "C" std::uint32_t hw_lesen_i(const volatile std::uint32_t*, std::uint32_t) asm("__moo_native.hw_reg_lesen_index");
extern "C" void hw_schreiben(volatile std::uint32_t*, std::uint32_t) asm("__moo_native.hw_reg_schreiben");
extern "C" void hw_klingel(volatile std::uint32_t*, volatile std::uint16_t*, std::uint16_t) asm("__moo_native.hw_klingel");
extern "C" std::uint64_t hw_quittieren(const volatile std::uint64_t*) asm("__moo_native.hw_quittieren");
int main() {
    alignas(8) volatile std::uint32_t regs[8] = {0x11111111u, 0x22222222u, 0x33333333u, 0, 0, 0, 0, 0x88888888u};
    alignas(8) volatile std::uint16_t ring[4] = {0, 0, 0, 0};
    alignas(8) volatile std::uint64_t paar[2] = {0x0123456789abcdefull, 7};
    if (hw_lesen(regs) != 0x11111111u) return 1;
    for (std::uint32_t i = 0; i < 8; ++i) if (hw_lesen_i(regs, i) != regs[i]) return 2;
    hw_schreiben(regs, 0xdeadbeefu);
    if (regs[0] != 0xdeadbeefu) return 3;
    hw_klingel(regs, ring, 0x4242u);
    if (ring[3] != 0x4242u || regs[6] != 0x10u || ring[0] != 0) return 4;
    if (hw_quittieren(paar) != 0x0123456789abcdefull) return 5;
    std::puts("PASS volatile_lesen/volatile_schreiben/Barrieren");
}

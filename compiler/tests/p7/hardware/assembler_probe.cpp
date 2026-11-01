// P7-T4 5A: C++-Probe fuer assembler_x86.moos (ausgefuehrt, unprivilegierte Befehle).
#include <cpuid.h>
#include <cstdint>
#include <cstdio>
using u64 = std::uint64_t;
extern "C" u64 asm_addiere(u64, u64) asm("__moo_native.asm_addiere");
extern "C" std::uint32_t asm_cpuid_max(std::uint32_t) asm("__moo_native.asm_cpuid_max");
extern "C" u64 asm_lies(const u64*) asm("__moo_native.asm_lies");
extern "C" u64 asm_hoch32(u64) asm("__moo_native.asm_hoch32");
extern "C" void asm_pause() asm("__moo_native.asm_pause");
int main() {
    if (asm_addiere(40, 2) != 42 || asm_addiere(~u64(0), 1) != 0) return 1;
    unsigned a = 0, b = 0, c = 0, d = 0;
    __get_cpuid(0, &a, &b, &c, &d);
    if (asm_cpuid_max(0) != a || a == 0) return 2;
    const u64 wert = 0x1122334455667788ull;
    if (asm_lies(&wert) != wert) return 3;
    if (asm_hoch32(0x1234u) != 0x123400000000ull) return 4;
    asm_pause();
    std::puts("PASS assembler: Ausgang, Eingaenge, gebundener Operand, Clobber, $$");
}

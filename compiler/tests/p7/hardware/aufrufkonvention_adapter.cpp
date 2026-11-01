// P7-T4 7A: echte Aufrufer-/Aufgerufenen-Probe auf x86_64 Linux (SysV-Default).
// Gegenseite jeweils mit __attribute__((ms_abi)) bzw. sysv_abi: stimmt die von
// Moo erzeugte Konvention nicht, landen Argumente in falschen Registern/Slots.
#include <cstdint>
#include <cstdio>
using u64 = std::uint64_t;
#define MS __attribute__((ms_abi))
#define SYSV __attribute__((sysv_abi))

using ms_fuenf_t = u64 (MS *)(u64, u64, u64, u64, u64);
using ms_dienst_t = u64 (MS *)(std::uint8_t*, std::uint16_t*);
using sysv_fuenf_t = u64 (SYSV *)(u64, u64, u64, u64, u64);

extern "C" u64 rufe_win64(ms_fuenf_t, u64, u64, u64, u64, u64) asm("__moo_native.cc_rufe_win64");
extern "C" u64 rufe_uefi(ms_dienst_t, std::uint8_t*, std::uint16_t*) asm("__moo_native.cc_rufe_uefi");
extern "C" MS u64 win64_eintritt(sysv_fuenf_t, u64, u64, u64, u64) asm("__moo_native.cc_win64_eintritt");
extern "C" MS std::uint32_t reg_lesen(const volatile std::uint32_t*) asm("__moo_native.cc_reg_lesen");
extern "C" std::int32_t vier(std::int32_t) asm("__moo_native.cc_vier");
extern "C" u64 extern_rufen(u64, u64, u64, u64, u64) asm("__moo_native.cc_extern_rufen");

static MS u64 ms_fuenf(u64 a, u64 b, u64 c, u64 d, u64 e) { return a + 2 * b + 3 * c + 4 * d + 5 * e; }
static MS u64 ms_dienst(std::uint8_t* k, std::uint16_t* t) { return (u64(k[0]) << 16) | t[1]; }
static SYSV u64 sysv_fuenf(u64 a, u64 b, u64 c, u64 d, u64 e) { return a + 10 * b + 100 * c + 1000 * d + 10000 * e; }
// Ziel des extern-C-Aufrufs aus aufrufkonvention_extern.moos.
extern "C" MS u64 cc_fremd(u64 a, u64 b, u64 c, u64 d, u64 e) { return a * 10000 + b * 1000 + c * 100 + d * 10 + e; }

int main() {
    if (rufe_win64(ms_fuenf, 1, 2, 3, 4, 5) != 55) return 1;
    std::uint8_t kontext[1] = {0x7a};
    std::uint16_t text[2] = {0x41, 0x1234};
    if (rufe_uefi(ms_dienst, kontext, text) != 0x7a1234u) return 2;
    if (win64_eintritt(sysv_fuenf, 1, 2, 3, 4) != 44321) return 3;
    alignas(4) volatile std::uint32_t reg = 0xcafef00du;
    if (reg_lesen(&reg) != 0xcafef00du) return 4;
    if (vier(5) != 20) return 5;
    if (extern_rufen(1, 2, 3, 4, 5) != 12345) return 6;
    std::puts("PASS aufrufkonvention win64/sysv64/uefi (indirekt, direkt, extern C)");
}

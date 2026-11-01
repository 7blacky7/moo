// P7-T4 3A: nackte Funktionen ausfuehren (SysV und win64 per Hand im Asm).
#include <cstdint>
#include <cstdio>
using u64 = std::uint64_t;
extern "C" u64 nackt_identitaet(u64) asm("__moo_native.nackt_identitaet");
extern "C" __attribute__((ms_abi)) u64 nackt_win64(u64) asm("__moo_native.nackt_win64");
int main() {
    if (nackt_identitaet(0xfeedfacecafebeefull) != 0xfeedfacecafebeefull) return 1;
    if (nackt_win64(41) != 42) return 2;
    std::puts("PASS @nackt: Aufruf ohne Prolog/Epilog (SysV und win64)");
}

#include <cstdint>
#include <cstdio>
extern "C" std::uint16_t lesen(const std::uint16_t *)
    asm("__moo_native.p2_modul_zeiger_u16");
int main() {
    const std::uint16_t a = 0x1234, b = 0xfedc;
    if (lesen(&a) != a || lesen(&b) != b) return 1;
    std::puts("PASS importierter ptr[u16]-Callee");
}

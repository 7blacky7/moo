#include <cstdint>
#include <cstdio>
extern "C" std::uint16_t lies(const std::uint16_t*, std::uint32_t) asm("__moo_native.p2_zeiger_variabel");
int main() {
    const std::uint16_t data[] = { 0x1234, 0xabcd, 0xfedc };
    for (std::uint32_t i = 0; i < 3; ++i) if (lies(data, i) != data[i]) return 1;
    std::puts("PASS variabler nativer Zeigerindex");
}

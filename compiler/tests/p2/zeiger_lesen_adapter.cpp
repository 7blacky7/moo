#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" std::uint16_t lies(const std::uint16_t*) asm("__moo_native.p2_zeiger_lesen");
extern "C" std::uint16_t lies_zwei(const std::uint16_t*) asm("__moo_native.p2_zeiger_lesen_zwei");
int main() {
    const std::uint16_t first = 0x1234, second = 0xfedc;
    if (lies(&first) != first || lies(&second) != second) return 1;
    const std::uint16_t values[] = { first, second };
    if (lies_zwei(values) != second) return 1;
    // x86_64 permits the unaligned address; memcpy supplies native byte order.
    alignas(2) unsigned char bytes[4] = {};
    std::memcpy(bytes + 1, &second, sizeof(second));
    if (lies(reinterpret_cast<const std::uint16_t*>(bytes + 1)) != second) return 1;
    std::puts("PASS nativer u16-Zeigerlesezugriff");
}

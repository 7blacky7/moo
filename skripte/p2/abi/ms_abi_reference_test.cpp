#include <cstdint>
#include <cstdio>
using P2MsTen = std::uint64_t (__attribute__((ms_abi)) *)(std::uint8_t*, std::uint8_t*, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" std::uint64_t p2_ms_zehn_aufrufen(P2MsTen, std::uint8_t*, std::uint8_t*, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" std::uint64_t __attribute__((ms_abi)) p2_ms_zehn(std::uint8_t* a, std::uint8_t* b, std::uint32_t op, std::uint64_t x1, std::uint64_t x2, std::uint64_t x3, std::uint64_t x4, std::uint64_t x5, std::uint64_t x6, std::uint64_t x7) {
    return *a + *b + op + x1 + x2 + x3 + x4 + x5 + x6 + x7;
}
int main() {
    std::uint8_t a = 2, b = 3;
    if (p2_ms_zehn_aufrufen(&p2_ms_zehn, &a, &b, 4, 1, 2, 3, 4, 5, 6, 7) != 37) return 1;
    if (p2_ms_zehn_aufrufen(&p2_ms_zehn, &a, &b, 4, UINT64_C(0x8000000000000000), 2, 3, 4, 5, 6, 7) != UINT64_C(0x8000000000000024)) return 2;
    std::puts("Linux x86_64 indirect ms_abi ten arguments PASS");
}

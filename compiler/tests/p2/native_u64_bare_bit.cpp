#include <cstdint>
#include <cstdio>
extern "C" std::uint64_t wort_und(std::uint64_t, std::uint64_t) asm("__moo_native.wort_und");
extern "C" std::uint64_t wort_oder(std::uint64_t, std::uint64_t) asm("__moo_native.wort_oder");
extern "C" std::uint64_t wort_xor(std::uint64_t, std::uint64_t) asm("__moo_native.wort_xor");
int main() {
    constexpr std::uint64_t a = UINT64_C(0xf0f0000000000001);
    constexpr std::uint64_t b = UINT64_C(0x0ff0000000000003);
    if (wort_und(a, b) != (a & b) || wort_oder(a, b) != (a | b) ||
        wort_xor(a, b) != (a ^ b)) return 1;
    std::puts("PASS native u64-Bitmasken");
    return 0;
}

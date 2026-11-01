#include <cstdint>
#include <cstdio>
#include <limits>
extern "C" std::uint64_t wort_add(std::uint64_t, std::uint64_t) asm("__moo_native.wort_add");
extern "C" std::uint64_t wort_sub(std::uint64_t, std::uint64_t) asm("__moo_native.wort_sub");
extern "C" std::uint64_t wort_mul(std::uint64_t, std::uint64_t) asm("__moo_native.wort_mul");
extern "C" std::uint64_t wort_max() asm("__moo_native.wort_max");
extern "C" std::uint64_t wort_genau() asm("__moo_native.wort_genau");
int main() {
    const auto max = std::numeric_limits<std::uint64_t>::max();
    if (wort_max() != max || wort_genau() != UINT64_C(9007199254740993) ||
        wort_add(max, 1) != 0 || wort_add(UINT64_C(9007199254740993), 2) != UINT64_C(9007199254740995) ||
        wort_sub(0, 1) != max || wort_mul(UINT64_C(0x8000000000000001), 2) != 2) return 1;
    std::puts("PASS u64-Bare-Arithmetik und Literale");
    return 0;
}

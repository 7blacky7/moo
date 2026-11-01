#include <cstdint>
#include <cstdio>
#include <limits>
extern "C" std::uint64_t rufe_wort(std::uint64_t) asm("__moo_native.rufe_wort");
int main() {
    if (rufe_wort(UINT64_C(9007199254740993)) != UINT64_C(9007199254740994) ||
        rufe_wort(std::numeric_limits<std::uint64_t>::max()) != 0) return 1;
    std::puts("PASS direkter nativer u64-Aufruf");
    return 0;
}

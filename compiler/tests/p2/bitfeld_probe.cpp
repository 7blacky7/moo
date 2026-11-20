// P10-T5: unabhaengige Referenz zur bisherigen x86-Wortrechnung.
#include <cstdint>
#include <cstdio>
extern "C" std::uint64_t tss_low(std::uint64_t) asm("__moo_native.x86_tss_unterwort");
extern "C" std::uint64_t tss_high(std::uint64_t) asm("__moo_native.x86_tss_oberwort");
extern "C" std::uint64_t idt_low(std::uint64_t, std::uint64_t) asm("__moo_native.x86_idt_unterwort");
extern "C" std::uint64_t idt_high(std::uint64_t) asm("__moo_native.x86_idt_oberwort");
int main() {
    const std::uint64_t adr[] = {0, 1, 0xffff, 0x12345678, 0x123456789abcdef0ULL,
        0xffffffff80000000ULL, UINT64_MAX};
    const std::uint64_t ist[] = {0, 1, 7, 8, 255, UINT64_MAX};
    unsigned checks = 0;
    for (auto a : adr) {
        const auto t = 103ULL | ((a & 0xffffffULL) << 16) | (0x89ULL << 40)
            | (((a >> 24) & 0xffULL) << 56);
        if (tss_low(a) != t || tss_high(a) != (a >> 32)) return 1;
        checks += 2;
        for (auto i : ist) {
            const auto v = (a & 0xffffULL) | (8ULL << 16) | ((i & 7ULL) << 32)
                | (0x8eULL << 40) | (((a >> 16) & 0xffffULL) << 48);
            if (idt_low(a, i) != v || idt_high(a) != (a >> 32)) return 2;
            checks += 2;
        }
    }
    std::printf("PASS Bitfeld-Deskriptoren: %u Wortvergleiche\n", checks);
}

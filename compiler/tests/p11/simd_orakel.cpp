// Unabhängiges GF-Orakel: polynomiale Multiplikation und lange Division.
// Keine gemeinsame Basiswechsel-/GFNI-Implementierung mit dem Compiler.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
extern "C" [[noreturn]] void moo_native_abbruch(...) { std::abort(); }
using Probe = void (*)(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_16_gfni128(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_32_gfni128(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_16_skalar(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_16_ssse3(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_16_avx2(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_16_auto(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_32_skalar(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_32_ssse3(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_32_avx2(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_32_auto(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
static std::uint8_t gf(unsigned a, unsigned b) {
    unsigned p=0;
    for(unsigned bit=0;bit<8;++bit) if(b&(1u<<bit)) p^=a<<bit;
    for(int degree=14;degree>=8;--degree) if(p&(1u<<degree)) p^=0x11du<<(degree-8);
    return static_cast<std::uint8_t>(p);
}
static void check(const char* name, unsigned n, Probe probe) {
    std::array<std::uint8_t,32> a{},b{};
    std::array<std::uint8_t,64> result{};
    for(unsigned x=0;x<256;++x) for(unsigned y=0;y<256;++y) {
        for(unsigned i=0;i<n;++i) {
            a[i]=static_cast<std::uint8_t>(x+i*17);
            b[i]=static_cast<std::uint8_t>(y+i*29);
        }
        probe(a.data(),b.data(),result.data());
        for(unsigned i=0;i<n;++i) {
            auto product=gf(a[i],b[i]);
            auto shuffled=(b[i]&128)?0:a[(i&~15u)+(b[i]&15)];
            if(result[i]!=product || result[n+i]!=shuffled) {
                std::fprintf(stderr,"FAIL %s a=%u b=%u lane=%u gf=%u/%u shuffle=%u/%u\n",
                    name,x,y,i,result[i],product,result[n+i],shuffled);
                std::exit(1);
            }
        }
    }
    std::printf("PASS %s: alle 65536 Bytepaare in jeder der %u Bahnen; Shuffle alle Masken\n",name,n);
}
int main() {
    __builtin_cpu_init();
    std::printf("CPU SSSE3=%d AVX2=%d GFNI=%d\n", !!__builtin_cpu_supports("ssse3"),
        !!__builtin_cpu_supports("avx2"), !!__builtin_cpu_supports("gfni"));
    if (std::getenv("MOO_SIMD_REQUIRE_GFNI") && !__builtin_cpu_supports("gfni")) {
        std::fprintf(stderr,"FAIL GFNI-Hardware für diese Abnahme erforderlich\n");
        return 2;
    }
    check("16_gfni128",16,simd_orakel_16_gfni128);
    check("32_gfni128",32,simd_orakel_32_gfni128);
    check("16_skalar",16,simd_orakel_16_skalar);
    check("16_ssse3",16,simd_orakel_16_ssse3);
    check("16_avx2",16,simd_orakel_16_avx2);
    check("16_auto",16,simd_orakel_16_auto);
    check("32_skalar",32,simd_orakel_32_skalar);
    check("32_ssse3",32,simd_orakel_32_ssse3);
    check("32_avx2",32,simd_orakel_32_avx2);
    check("32_auto",32,simd_orakel_32_auto);
}

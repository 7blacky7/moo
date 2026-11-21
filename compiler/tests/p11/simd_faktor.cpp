// Unabhaengige GF-Polynomdivision, alle Faktorpaare je Bahn/Profil.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
using Probe = void (*)(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" void faktor_16_skalar(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" void faktor_32_skalar(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" void faktor_16_ssse3(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" void faktor_32_ssse3(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" void faktor_16_avx2(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" void faktor_32_avx2(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" void faktor_16_gfni128(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" void faktor_32_gfni128(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" void faktor_16_gfni256(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" void faktor_32_gfni256(const std::uint8_t*,std::uint8_t,std::uint8_t*);
extern "C" [[noreturn]] void moo_native_abbruch(...) { std::abort(); }
static std::uint8_t gf(unsigned a,unsigned b) {
    unsigned x=0;
    for(unsigned i=0;i<8;++i) if(b&(1u<<i)) x^=a<<i;
    for(int i=14;i>=8;--i) if(x&(1u<<i)) x^=0x11du<<(i-8);
    return static_cast<std::uint8_t>(x);
}
static void check(const char *name,unsigned n,Probe f) {
    std::array<std::uint8_t,32> a{},out{};
    for(unsigned factor=0;factor<256;++factor) for(unsigned v=0;v<256;++v) {
        for(unsigned i=0;i<n;++i) a[i]=static_cast<std::uint8_t>(v+i*17);
        f(a.data(),static_cast<std::uint8_t>(factor),out.data());
        for(unsigned i=0;i<n;++i) if(out[i]!=gf(a[i],factor)) {
            std::fprintf(stderr,"FAIL %s factor=%u a=%u lane=%u got=%u expected=%u\n",name,factor,v,i,out[i],gf(a[i],factor));
            std::exit(1);
        }
    }
    std::printf("GF Faktor OK %s: 65536 Paare je Bahn\n",name);
}
int main() {
    __builtin_cpu_init();
    std::printf("CPU SSSE3=%d AVX2=%d GFNI=%d\n",!!__builtin_cpu_supports("ssse3"),!!__builtin_cpu_supports("avx2"),!!__builtin_cpu_supports("gfni"));
    check("16_skalar",16,faktor_16_skalar);
    check("32_skalar",32,faktor_32_skalar);
    check("16_ssse3",16,faktor_16_ssse3);
    check("32_ssse3",32,faktor_32_ssse3);
    check("16_avx2",16,faktor_16_avx2);
    check("32_avx2",32,faktor_32_avx2);
    check("16_gfni128",16,faktor_16_gfni128);
    check("32_gfni128",32,faktor_32_gfni128);
    check("16_gfni256",16,faktor_16_gfni256);
    check("32_gfni256",32,faktor_32_gfni256);
}

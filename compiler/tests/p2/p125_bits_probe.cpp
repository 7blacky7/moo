#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
using U = std::uint64_t;
extern "C" {
std::uint8_t rol8(std::uint8_t, U) asm("__moo_native.p125_rol8");
std::uint8_t ror8(std::uint8_t, U) asm("__moo_native.p125_ror8");
std::uint8_t hi8(std::uint8_t, std::uint8_t) asm("__moo_native.p125_hi8");
bool carry8(std::uint8_t, std::uint8_t) asm("__moo_native.p125_carry8");
bool eq8(std::uint8_t, std::uint8_t) asm("__moo_native.p125_eq8");
std::uint8_t sel8(bool, std::uint8_t, std::uint8_t) asm("__moo_native.p125_sel8");
std::uint16_t rol16(std::uint16_t, U) asm("__moo_native.p125_rol16");
std::uint16_t ror16(std::uint16_t, U) asm("__moo_native.p125_ror16");
std::uint16_t hi16(std::uint16_t, std::uint16_t) asm("__moo_native.p125_hi16");
bool carry16(std::uint16_t, std::uint16_t) asm("__moo_native.p125_carry16");
bool eq16(std::uint16_t, std::uint16_t) asm("__moo_native.p125_eq16");
std::uint16_t sel16(bool, std::uint16_t, std::uint16_t) asm("__moo_native.p125_sel16");
std::uint32_t rol32(std::uint32_t, U) asm("__moo_native.p125_rol32");
std::uint32_t ror32(std::uint32_t, U) asm("__moo_native.p125_ror32");
std::uint32_t hi32(std::uint32_t, std::uint32_t) asm("__moo_native.p125_hi32");
bool carry32(std::uint32_t, std::uint32_t) asm("__moo_native.p125_carry32");
bool eq32(std::uint32_t, std::uint32_t) asm("__moo_native.p125_eq32");
std::uint32_t sel32(bool, std::uint32_t, std::uint32_t) asm("__moo_native.p125_sel32");
std::uint64_t rol64(std::uint64_t, U) asm("__moo_native.p125_rol64");
std::uint64_t ror64(std::uint64_t, U) asm("__moo_native.p125_ror64");
std::uint64_t hi64(std::uint64_t, std::uint64_t) asm("__moo_native.p125_hi64");
bool carry64(std::uint64_t, std::uint64_t) asm("__moo_native.p125_carry64");
bool eq64(std::uint64_t, std::uint64_t) asm("__moo_native.p125_eq64");
std::uint64_t sel64(bool, std::uint64_t, std::uint64_t) asm("__moo_native.p125_sel64");
U negative(U) asm("__moo_native.p125_neg");
U system_rotate(U, U) asm("__moo_native.p125_sys");
U inline_select(U, U, U) asm("__moo_native.p125_inline");
void hardware(std::uint32_t *, std::uint32_t, std::uint32_t, U) asm("__moo_native.p125_hw");
void hardware_select(std::uint32_t *, bool, std::uint32_t, std::uint32_t) asm("__moo_native.p125_hw_sel");
}
static U checks = 0;
static void check(bool ok, const char *name) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %s (Probe %llu)\n", name, (unsigned long long)checks); std::exit(1); }
}
// Unabhaengiges portables 4x32-Schulprodukt, auch mit MSVC ohne __int128.
static U high64(U a, U b) {
    const U a0 = std::uint32_t(a), a1 = a >> 32;
    const U b0 = std::uint32_t(b), b1 = b >> 32;
    const U lo = a0 * b0;
    const U t = a1 * b0 + (lo >> 32);
    const U middle = (t & 0xffffffffULL) + a0 * b1;
    return a1 * b1 + (t >> 32) + (middle >> 32);
}
static U left(U a, U count, unsigned bits, U mask) {
    const unsigned n = unsigned(count % bits);
    return n ? ((a << n) | (a >> (bits - n))) & mask : a;
}
static U right(U a, U count, unsigned bits, U mask) {
    const unsigned n = unsigned(count % bits);
    return n ? ((a >> n) | (a << (bits - n))) & mask : a;
}
template<class T> static void probe(T (*rol)(T,U), T (*ror)(T,U), T (*hi)(T,T),
                                    bool (*carry)(T,T), bool (*equal)(T,T), T (*select)(bool,T,T)) {
    constexpr unsigned bits = sizeof(T) * 8;
    const U mask = std::numeric_limits<T>::max();
    auto one = [&](U av, U bv, U n) {
        const T a = T(av), b = T(bv);
        check(rol(a,n) == T(left(a,n,bits,mask)), "Linksrotation");
        check(ror(a,n) == T(right(a,n,bits,mask)), "Rechtsrotation");
        U upper;
        if constexpr (sizeof(T) == 8) upper = high64(a,b);
        else upper = (U(a) * U(b)) >> bits;
        check(hi(a,b) == T(upper), "hohes Produktwort");
        check(carry(a,b) == (U(a) > mask - U(b)), "unsigned Carry");
        check(equal(a,b) == (a == b), "Konstantzeit-Gleichheit");
        check(select(false,a,b) == b && select(true,a,b) == a, "Konstantzeit-Auswahl");
    };
    const U values[] = {0,1,2,3,mask,mask-1,mask>>1,(mask>>1)+1};
    const U counts[] = {0,1,bits-1,bits,bits+1,2*bits-1,~U(0)};
    for (U a: values) for (U b: values) for (U n: counts) one(a,b,n);
    if constexpr (sizeof(T) == 1) {
        for (unsigned a=0; a<256; ++a) for (unsigned b=0; b<256; ++b) one(a,b,a+b);
    }
    U state = 0x3141592653589793ULL;
    // Deterministische Probenfolge: uint64_t-Wrap ist absichtlich definiert.
    for (unsigned i=0; i<4096; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const U a=state;
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        one(a,state,state>>17);
    }
}
int main() {
    probe(rol8,ror8,hi8,carry8,eq8,sel8);
    probe(rol16,ror16,hi16,carry16,eq16,sel16);
    probe(rol32,ror32,hi32,carry32,eq32,sel32);
    probe(rol64,ror64,hi64,carry64,eq64,sel64);
    check(negative(0x123456789abcdef0ULL) == right(0x123456789abcdef0ULL,1,64,~U(0)), "negativer Rotationszaehler modulo Breite");
    for (U n: {U(0),U(1),U(63),U(64),U(65),~U(0)}) {
        check(system_rotate(0x123456789abcdef0ULL,n) == rol64(0x123456789abcdef0ULL,n), "System/Pure");
        std::uint32_t out=0;
        hardware(&out,0xffffffffU,2,n);
        check(out == 1, "Hardware Carry/MulHigh");
        hardware(&out,0x12345678U,1,n);
        check(out == ror32(0x12345678U,n), "Hardware Rotation");
        hardware_select(&out,false,7,9); check(out == 9, "Hardware Auswahl falsch");
        hardware_select(&out,true,7,9); check(out == 7, "Hardware Auswahl wahr");
    }
    check(inline_select(41,17,0) == 42 && inline_select(41,17,1) == 51, "inlinierte geheime Auswahl");
    std::printf("PASS P125 Bits: %llu C++-Orakelpruefungen\n", (unsigned long long)checks);
}

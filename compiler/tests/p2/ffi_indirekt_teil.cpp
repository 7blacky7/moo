#include <cstdint>
#include <cstdio>
using Fn = std::int32_t (*)(std::int32_t);
extern "C" std::int32_t p2_indirekt(Fn, std::int32_t) asm("__moo_native.p2_indirekt");
using Fn10 = std::uint64_t (*)(std::uint8_t*, std::uint8_t*, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" std::uint64_t p2_indirekt_zehn(Fn10, std::uint8_t*, std::uint8_t*, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t) asm("__moo_native.p2_indirekt_zehn");
extern "C" std::int8_t p2_indirekt_i8(std::int8_t (*)(std::int8_t), std::int8_t) asm("__moo_native.p2_indirekt_i8");
extern "C" std::uint8_t p2_indirekt_u8(std::uint8_t (*)(std::uint8_t), std::uint8_t) asm("__moo_native.p2_indirekt_u8");
extern "C" std::int8_t p2_i8(std::int8_t x) { return x; }
extern "C" std::uint8_t p2_u8(std::uint8_t x) { return x; }
extern "C" std::int16_t p2_indirekt_i16(std::int16_t (*)(std::int16_t), std::int16_t) asm("__moo_native.p2_indirekt_i16");
extern "C" std::uint16_t p2_indirekt_u16(std::uint16_t (*)(std::uint16_t), std::uint16_t) asm("__moo_native.p2_indirekt_u16");
extern "C" std::int16_t p2_i16(std::int16_t x) { return x; }
extern "C" std::uint16_t p2_u16(std::uint16_t x) { return x; }
extern "C" std::int32_t twice(std::int32_t x) { return x * 2; }
extern "C" std::uint64_t sum(std::uint8_t* a, std::uint8_t* b, std::uint32_t op, std::uint64_t x1, std::uint64_t x2, std::uint64_t x3, std::uint64_t x4, std::uint64_t x5, std::uint64_t x6, std::uint64_t x7) { return *a + *b + op + x1 + x2 + x3 + x4 + x5 + x6 + x7; }
int main() {
    std::uint8_t a = 2, b = 3;
    if (p2_indirekt(twice, -13) != -26) return 1;
    if (p2_indirekt_i8(p2_i8, -100) != -100) return 3;
    if (p2_indirekt_u8(p2_u8, 250) != 250) return 4;
    if (p2_indirekt_i16(p2_i16, -30000) != -30000) return 5;
    if (p2_indirekt_u16(p2_u16, 60000) != 60000) return 6;
    if (p2_indirekt_zehn(sum, &a, &b, 4, 1, 2, 3, 4, 5, 6, 7) != 37) return 2;
    std::puts("PASS indirekter Teilaufruf");
}

#include <cstdint>
#include <cstdio>
extern "C" std::int8_t p2_ref_indirect_i8(std::int8_t (*)(std::int8_t), std::int8_t);
extern "C" std::uint8_t p2_ref_indirect_u8(std::uint8_t (*)(std::uint8_t), std::uint8_t);
extern "C" std::uint64_t p2_ref_indirect_ptr(std::uint64_t (*)(std::uint8_t*, std::uint16_t*), std::uint8_t*, std::uint16_t*);
extern "C" void p2_ref_indirect_void(void (*)(std::int32_t), std::int32_t);
using P2Zehn = std::uint64_t (*)(std::uint8_t*, std::uint8_t*, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" std::uint64_t p2_ref_indirect_zehn(P2Zehn, std::uint8_t*, std::uint8_t*, std::uint32_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" std::uint64_t ref_zehn(std::uint8_t* a, std::uint8_t* b, std::uint32_t op, std::uint64_t x1, std::uint64_t x2, std::uint64_t x3, std::uint64_t x4, std::uint64_t x5, std::uint64_t x6, std::uint64_t x7) { return *a + *b + op + x1 + x2 + x3 + x4 + x5 + x6 + x7; }
extern "C" std::int8_t ref_i8(std::int8_t x) { return x; }
extern "C" std::uint8_t ref_u8(std::uint8_t x) { return x; }
extern "C" std::uint64_t ref_ptr(std::uint8_t* context, std::uint16_t* text) { return *context + text[0]; }
static std::int32_t observed = 0;
extern "C" void ref_void(std::int32_t x) { observed = x; }
int main() {
    std::uint8_t context = 5;
    std::uint16_t text[] = {77, 0};
    if (p2_ref_indirect_i8(&ref_i8, -100) != -100) return 1;
    if (p2_ref_indirect_u8(&ref_u8, 250) != 250) return 2;
    if (p2_ref_indirect_ptr(&ref_ptr, &context, text) != 82) return 3;
    p2_ref_indirect_void(&ref_void, 37);
    if (observed != 37) return 4;
    std::uint8_t a = 2, b = 3;
    if (p2_ref_indirect_zehn(&ref_zehn, &a, &b, 4, 1, 2, 3, 4, 5, 6, 7) != 37) return 5;
    std::puts("C/C++ indirect reference PASS");
}

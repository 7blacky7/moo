#include <stdint.h>
int8_t p2_ref_indirect_i8(int8_t (*f)(int8_t), int8_t value) { return f(value); }
uint8_t p2_ref_indirect_u8(uint8_t (*f)(uint8_t), uint8_t value) { return f(value); }
uint64_t p2_ref_indirect_ptr(uint64_t (*f)(uint8_t*, uint16_t*), uint8_t* context, uint16_t* text) { return f(context, text); }
void p2_ref_indirect_void(void (*f)(int32_t), int32_t value) { f(value); }

typedef uint64_t (*P2Zehn)(uint8_t*, uint8_t*, uint32_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t p2_ref_indirect_zehn(P2Zehn f, uint8_t* a, uint8_t* b, uint32_t op, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5, uint64_t x6, uint64_t x7) {
    return f(a, b, op, x1, x2, x3, x4, x5, x6, x7);
}


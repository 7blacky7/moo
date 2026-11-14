#include <stdint.h>
typedef uint64_t (__attribute__((ms_abi)) *P2MsTen)(uint8_t*, uint8_t*, uint32_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t p2_ms_zehn_aufrufen(P2MsTen f, uint8_t* a, uint8_t* b, uint32_t op, uint64_t x1, uint64_t x2, uint64_t x3, uint64_t x4, uint64_t x5, uint64_t x6, uint64_t x7) {
    return f(a, b, op, x1, x2, x3, x4, x5, x6, x7);
}

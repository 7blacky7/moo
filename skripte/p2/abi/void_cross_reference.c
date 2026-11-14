#include <stdint.h>
extern void p2_void_mixed(int32_t, float, double, int8_t, uint8_t);
void p2_void_reference(int32_t a, float b, double c, int8_t d, uint8_t e) {
    p2_void_mixed(a, b, c, d, e);
}

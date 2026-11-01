#include <stdint.h>
double p2_mixed_value(int32_t a, float b, double c, int8_t d, uint8_t e) {
    return (double)a + (double)b + c + (double)d + (double)e;
}
float p2_mixed_f32(int32_t a, float b, double c, int8_t d, uint8_t e) {
    return (float)(p2_mixed_value(a, b, c, d, e));
}
int32_t p2_mixed_i32(int32_t a, float b, double c, int8_t d, uint8_t e) {
    return (int32_t)(p2_mixed_value(a, b, c, d, e));
}
int8_t p2_mixed_i8(int32_t a, float b, double c, int8_t d, uint8_t e) {
    return (int8_t)(p2_mixed_value(a, b, c, d, e) - 147);
}
uint8_t p2_mixed_u8(int32_t a, float b, double c, int8_t d, uint8_t e) {
    return (uint8_t)(p2_mixed_value(a, b, c, d, e) + 236);
}

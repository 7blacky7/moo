#include <stdint.h>
static int32_t current;
void p2_void_record(int32_t value) { current = value; }
int32_t p2_void_read(void) { return current; }
void p2_void_mixed(int32_t a, float b, double c, int8_t d, uint8_t e) {
    current = (int32_t)((double)a + (double)b + c + (double)d + (double)e);
}

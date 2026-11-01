#include <stdint.h>

int32_t p2_ffi_schmal(int8_t signiert, uint8_t unsigniert) {
    return (int32_t)signiert * 1000 + (int32_t)unsigniert;
}

#include "ffi_reference.h"
#include <stdarg.h>

#define DEFINE_ECHO(type, name) type moo_p2_echo_##name(type value) { return value; }
DEFINE_ECHO(int8_t, i8)
DEFINE_ECHO(uint8_t, u8)
DEFINE_ECHO(int16_t, i16)
DEFINE_ECHO(uint16_t, u16)
DEFINE_ECHO(int32_t, i32)
DEFINE_ECHO(uint32_t, u32)
DEFINE_ECHO(int64_t, i64)
DEFINE_ECHO(uint64_t, u64)
DEFINE_ECHO(float, f32)
DEFINE_ECHO(double, f64)

bool moo_p2_bool_not(bool value) { return !value; }
void *moo_p2_echo_pointer(void *value) { return value; }
void moo_p2_store_i32(int32_t *out, int32_t value) { if (out) *out = value; }
double moo_p2_mixed(int32_t a, double b, uint64_t c, float d, const int32_t *p) {
    return (double)a + b + (double)c + (double)d + (p ? (double)*p : 0.0);
}
MooP2Aggregate moo_p2_aggregate_by_value(MooP2Aggregate value) { return value; }
int32_t moo_p2_variadic(int32_t count, ...) {
    va_list args;
    int32_t sum = 0;
    va_start(args, count);
    for (int32_t i = 0; i < count; ++i) sum += va_arg(args, int);
    va_end(args);
    return sum;
}

#ifndef MOO_P2_FFI_REFERENCE_H
#define MOO_P2_FFI_REFERENCE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int8_t moo_p2_echo_i8(int8_t value);
uint8_t moo_p2_echo_u8(uint8_t value);
int16_t moo_p2_echo_i16(int16_t value);
uint16_t moo_p2_echo_u16(uint16_t value);
int32_t moo_p2_echo_i32(int32_t value);
uint32_t moo_p2_echo_u32(uint32_t value);
int64_t moo_p2_echo_i64(int64_t value);
uint64_t moo_p2_echo_u64(uint64_t value);
float moo_p2_echo_f32(float value);
double moo_p2_echo_f64(double value);
bool moo_p2_bool_not(bool value);
void *moo_p2_echo_pointer(void *value);
void moo_p2_store_i32(int32_t *out, int32_t value);
double moo_p2_mixed(int32_t a, double b, uint64_t c, float d, const int32_t *p);

/* Valid C signatures: future Moo declarations MUST reject these in slice 1. */
typedef struct { int32_t x; double y; } MooP2Aggregate;
MooP2Aggregate moo_p2_aggregate_by_value(MooP2Aggregate value);
int32_t moo_p2_variadic(int32_t count, ...);

#ifdef __cplusplus
}
#endif
#endif

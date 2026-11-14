#include "ffi_reference.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>

static int failures;
#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); ++failures; } } while (0)

int main(void) {
    CHECK(moo_p2_echo_i8(INT8_MIN) == INT8_MIN);
    CHECK(moo_p2_echo_i8(INT8_MAX) == INT8_MAX);
    CHECK(moo_p2_echo_u8(UINT8_MAX) == UINT8_MAX);
    CHECK(moo_p2_echo_i16(INT16_MIN) == INT16_MIN);
    CHECK(moo_p2_echo_i16(INT16_MAX) == INT16_MAX);
    CHECK(moo_p2_echo_u16(UINT16_MAX) == UINT16_MAX);
    CHECK(moo_p2_echo_i32(INT32_MIN) == INT32_MIN);
    CHECK(moo_p2_echo_i32(INT32_MAX) == INT32_MAX);
    CHECK(moo_p2_echo_u32(UINT32_MAX) == UINT32_MAX);
    CHECK(moo_p2_echo_i64(INT64_MIN) == INT64_MIN);
    CHECK(moo_p2_echo_i64(INT64_MAX) == INT64_MAX);
    CHECK(moo_p2_echo_u64(UINT64_MAX) == UINT64_MAX);
    CHECK(moo_p2_echo_u64(UINT64_C(9007199254740993)) == UINT64_C(9007199254740993));
    CHECK(moo_p2_echo_f32(-1.25f) == -1.25f);
    CHECK(moo_p2_echo_f64(0x1.0000000000001p0) == 0x1.0000000000001p0);
    CHECK(moo_p2_bool_not(false));
    CHECK(!moo_p2_bool_not(true));
    int32_t value = 41;
    CHECK(moo_p2_echo_pointer(&value) == &value);
    CHECK(moo_p2_echo_pointer(NULL) == NULL);
    moo_p2_store_i32(&value, -1234567);
    CHECK(value == -1234567);
    moo_p2_store_i32(NULL, 0);
    value = 3;
    CHECK(moo_p2_mixed(-7, 0.5, UINT64_C(9), 1.25f, &value) == 6.75);
    MooP2Aggregate in = { -17, 2.5 };
    MooP2Aggregate out = moo_p2_aggregate_by_value(in);
    CHECK(out.x == in.x && out.y == in.y);
    CHECK(moo_p2_variadic(3, 1, 2, 3) == 6);
    if (failures) { fprintf(stderr, "%d ABI checks failed\n", failures); return 1; }
    puts("P2 C ABI reference: PASS");
    return 0;
}

#include "struct_by_value.h"
#include <stdio.h>

#define CHECK(N) do { \
    MooP2Struct##N value = { .words = {0} }; \
    for (unsigned i = 0; i < N; ++i) value.words[i] = 100 + i; \
    MooP2Struct##N actual = moo_p2_struct##N(value); \
    for (unsigned i = 0; i < N; ++i) \
        if (actual.words[i] != 101 + 2 * i) return N; \
} while (0)

int main(void) {
    CHECK(1);
    CHECK(2);
    CHECK(3);
    CHECK(4);
    CHECK(6);
    MooP2Mixed mixed = moo_p2_struct_mixed((MooP2Mixed){ 1.25, 11 });
    if (mixed.real != 1.75 || mixed.integer != 14) return 7;
    MooP2Float4 floats = moo_p2_float4((MooP2Float4){1.0f, 2.0f, 3.0f, 4.0f});
    if (floats.a != 2.0f || floats.b != 4.0f || floats.c != 6.0f || floats.d != 8.0f) return 8;
    MooP2Packed packed = moo_p2_packed((MooP2Packed){4, 0x12345678});
    if (sizeof packed != 5 || packed.tag != 5 || packed.value != 0x1234567a) return 9;
    MooP2Struct2 pressure = moo_p2_pressure(1, 2, 3, 4, 5, 6, (MooP2Struct2){{10, 20}});
    if (pressure.words[0] != 31 || pressure.words[1] != 27) return 10;
    puts("C struct-by-value sizes, mixed, float4, packed and register pressure PASS");
    return 0;
}

#include "struct_by_value.h"

#define DEFINE_STRUCT(N) \
    _Static_assert(sizeof(MooP2Struct##N) == 4 * N, "unexpected C layout"); \
    MooP2Struct##N moo_p2_struct##N(MooP2Struct##N value) { \
        for (unsigned i = 0; i < N; ++i) value.words[i] += i + 1; \
        return value; \
    }
DEFINE_STRUCT(1)
DEFINE_STRUCT(2)
DEFINE_STRUCT(3)
DEFINE_STRUCT(4)
DEFINE_STRUCT(6)

MooP2Mixed moo_p2_struct_mixed(MooP2Mixed value) {
    value.real += 0.5;
    value.integer += 3;
    return value;
}


MooP2Float4 moo_p2_float4(MooP2Float4 value) {
    value.a += 1.0f; value.b += 2.0f; value.c += 3.0f; value.d += 4.0f;
    return value;
}

MooP2Packed moo_p2_packed(MooP2Packed value) {
    value.tag += 1;
    value.value += 2;
    return value;
}

MooP2Struct2 moo_p2_pressure(uint64_t a, uint64_t b, uint64_t c,
                             uint64_t d, uint64_t e, uint64_t f, MooP2Struct2 value) {
    value.words[0] += (uint32_t)(a + b + c + d + e + f);
    value.words[1] += 7;
    return value;
}

/* Dieselben C-Callees dienen als unabhaengige Linux-/Windows-ABI-Referenz. */
MooP2Struct2 moo_p2_pair8(MooP2Struct2 first, MooP2Struct2 second) {
    first.words[0] += second.words[0] * 3u;
    first.words[1] += second.words[1] * 5u;
    return first;
}

MooP2Struct3 moo_p2_pair12(MooP2Struct3 first, MooP2Struct3 second) {
    for (unsigned i = 0; i < 3; ++i)
        first.words[i] += second.words[i] * (i + 1u);
    return first;
}

MooP2Mixed moo_p2_pair_mixed(MooP2Mixed first, MooP2Mixed second) {
    first.real += second.real;
    first.integer += second.integer;
    return first;
}

MooP2Float4 moo_p2_pair_float4(MooP2Float4 first, MooP2Float4 second) {
    first.a += second.a; first.b += second.b;
    first.c += second.c; first.d += second.d;
    return first;
}

MooP2Packed moo_p2_pair_packed(MooP2Packed first, MooP2Packed second) {
    first.tag += second.tag;
    first.value += second.value;
    return first;
}

/* Mehrregister-Aggregate bei SysV Integer-/SSE-Registerdruck.
 * Getrennte Definitionen werden mit Clang in Zielform geprueft.
 */
MooP2Struct3 moo_p2_pressure12_5(uint64_t a, uint64_t b, uint64_t c,
    uint64_t d, uint64_t e, MooP2Struct3 value) {
    uint64_t scalars[] = {a, b, c, d, e};
    for (unsigned i = 0; i < 5; ++i) {
        value.words[0] += (uint32_t)scalars[i];
        value.words[1] += (uint32_t)(scalars[i] * (i + 1u));
    }
    value.words[2] += 71u;
    return value;
}

MooP2Struct3 moo_p2_pressure12_6(uint64_t a, uint64_t b, uint64_t c,
    uint64_t d, uint64_t e, uint64_t f, MooP2Struct3 value) {
    uint64_t scalars[] = {a, b, c, d, e, f};
    for (unsigned i = 0; i < 6; ++i) {
        value.words[0] += (uint32_t)scalars[i];
        value.words[1] += (uint32_t)(scalars[i] * (i + 1u));
    }
    value.words[2] += 97u;
    return value;
}

MooP2Struct4 moo_p2_pressure16_5(uint64_t a, uint64_t b, uint64_t c,
    uint64_t d, uint64_t e, MooP2Struct4 value) {
    value.words[0] += (uint32_t)(a + b + c + d + e);
    value.words[1] += 17u;
    value.words[2] += 29u;
    value.words[3] += 41u;
    return value;
}

MooP2Mixed moo_p2_pressure_mixed6(uint64_t a, uint64_t b, uint64_t c,
    uint64_t d, uint64_t e, uint64_t f, MooP2Mixed value) {
    value.real += 0.5;
    value.integer += (uint32_t)(a + b + c + d + e + f);
    return value;
}

MooP2Struct3 moo_p2_pair12_pressure3(uint64_t a, uint64_t b, uint64_t c,
    MooP2Struct3 first, MooP2Struct3 second) {
    uint32_t extra = (uint32_t)(a + b + c);
    for (unsigned i = 0; i < 3; ++i)
        first.words[i] += second.words[i] * (i + 1u) + extra;
    return first;
}

MooP2Float4 moo_p2_pressure_float4_6(uint64_t a, uint64_t b, uint64_t c,
    uint64_t d, uint64_t e, uint64_t f, MooP2Float4 value) {
    value.a += (float)(a + b + c + d + e + f);
    value.b += 0.5f;
    value.c += 2.0f;
    value.d += 3.0f;
    return value;
}




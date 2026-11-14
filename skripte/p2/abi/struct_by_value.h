#ifndef MOO_P2_STRUCT_BY_VALUE_H
#define MOO_P2_STRUCT_BY_VALUE_H
#include <stdint.h>

#define DECLARE_STRUCT(N) \
    typedef struct { uint32_t words[N]; } MooP2Struct##N; \
    MooP2Struct##N moo_p2_struct##N(MooP2Struct##N value);

DECLARE_STRUCT(1)
DECLARE_STRUCT(2)
DECLARE_STRUCT(3)
DECLARE_STRUCT(4)
DECLARE_STRUCT(6)
typedef struct { double real; uint32_t integer; } MooP2Mixed;
MooP2Mixed moo_p2_struct_mixed(MooP2Mixed value);

typedef struct { float a, b, c, d; } MooP2Float4;
typedef struct __attribute__((packed)) { uint8_t tag; uint32_t value; } MooP2Packed;
MooP2Float4 moo_p2_float4(MooP2Float4 value);
MooP2Packed moo_p2_packed(MooP2Packed value);
MooP2Struct2 moo_p2_pressure(uint64_t a, uint64_t b, uint64_t c,
                             uint64_t d, uint64_t e, uint64_t f, MooP2Struct2 value);
/* P2-T56: mehrere gleichartige C-Struct-Wertparameter und Registerdruck. */
MooP2Struct2 moo_p2_pair8(MooP2Struct2 first, MooP2Struct2 second);
MooP2Struct3 moo_p2_pair12(MooP2Struct3 first, MooP2Struct3 second);
MooP2Mixed moo_p2_pair_mixed(MooP2Mixed first, MooP2Mixed second);
MooP2Float4 moo_p2_pair_float4(MooP2Float4 first, MooP2Float4 second);
MooP2Packed moo_p2_pair_packed(MooP2Packed first, MooP2Packed second);

/* Clang-SysV-Referenzen fuer aggregate rollback bei Registerdruck.
 * SysV: 6 Integerregister, SSE-Klassen separat, Aggregate ganz stacken.
 */
MooP2Struct3 moo_p2_pressure12_5(uint64_t a, uint64_t b, uint64_t c,
    uint64_t d, uint64_t e, MooP2Struct3 value);
MooP2Struct3 moo_p2_pressure12_6(uint64_t a, uint64_t b, uint64_t c,
    uint64_t d, uint64_t e, uint64_t f, MooP2Struct3 value);
MooP2Struct4 moo_p2_pressure16_5(uint64_t a, uint64_t b, uint64_t c,
    uint64_t d, uint64_t e, MooP2Struct4 value);
MooP2Mixed moo_p2_pressure_mixed6(uint64_t a, uint64_t b, uint64_t c,
    uint64_t d, uint64_t e, uint64_t f, MooP2Mixed value);
MooP2Struct3 moo_p2_pair12_pressure3(uint64_t a, uint64_t b, uint64_t c,
    MooP2Struct3 first, MooP2Struct3 second);
MooP2Float4 moo_p2_pressure_float4_6(uint64_t a, uint64_t b, uint64_t c,
    uint64_t d, uint64_t e, uint64_t f, MooP2Float4 value);
#endif

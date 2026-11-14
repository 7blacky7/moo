/* Echte Moo->C->Moo Roundtrips: gemeinsame C-Gegenprobe fuer
 * Linux-SysV und Windows x64. Alle Aufrufer stehen in Moo, nicht in C. */
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include "struct_by_value.h"

extern uint32_t sys_c_complex_mixed(uint64_t addr);
extern uint32_t sys_c_complex_float4(uint64_t addr);
extern uint32_t sys_c_complex_packed(uint64_t addr);
extern uint32_t sys_c_pair8(uint64_t addr);
extern uint32_t sys_c_pair12(uint64_t addr);
extern uint32_t sys_c_pair_mixed(uint64_t addr);
extern uint32_t sys_c_pair_float4(uint64_t addr);
extern uint32_t sys_c_pair_packed(uint64_t addr);
extern uint32_t sys_c_pressure(uint64_t addr);

extern uint32_t sys_c_pressure12_5(uint64_t addr);
extern uint32_t sys_c_pressure12_6(uint64_t addr);
extern uint32_t sys_c_pressure16_5(uint64_t addr);
extern uint32_t sys_c_pressure_mixed6(uint64_t addr);
extern uint32_t sys_c_pair12_pressure3(uint64_t addr);
extern uint32_t sys_c_pressure_float4_6(uint64_t addr);



_Static_assert(sizeof(MooP2Mixed) == 16, "Mixed size");
_Static_assert(_Alignof(MooP2Mixed) == 8, "Mixed align");
_Static_assert(offsetof(MooP2Mixed, integer) == 8, "Mixed offset");
_Static_assert(sizeof(MooP2Float4) == 16, "Float4 size");
_Static_assert(sizeof(MooP2Packed) == 5, "Packed size");
_Static_assert(offsetof(MooP2Packed, value) == 1, "Packed offset");

static unsigned tests = 0, errors = 0;

/** Meldet die genaue gescheiterte Struktur-ABI-Gegenprobe. */
static void check(const char *name, int correct) {
    tests++;
    if (!correct) {
        errors++;
        fprintf(stderr, "FAIL %s\n", name);
    }
}

int main(void) {
    MooP2Mixed mixed = {0};
    MooP2Float4 floats = {0};
    MooP2Packed packed = {0};
    MooP2Struct2 pair8 = {{0, 0}};
    MooP2Struct3 pair12 = {{0, 0, 0}};
    MooP2Mixed mixed_pair = {0};
    MooP2Float4 float_pair = {0};
    MooP2Packed packed_pair = {0};
    MooP2Struct2 pressure = {{0, 0}};
    MooP2Struct3 pressure12_5 = {{0, 0, 0}};
    MooP2Struct3 pressure12_6 = {{0, 0, 0}};
    MooP2Struct4 pressure16_5 = {{0, 0, 0, 0}};
    MooP2Mixed pressure_mixed6 = {0};
    MooP2Struct3 pair12_pressure3 = {{0, 0, 0}};
    MooP2Float4 pressure_float4_6 = {0};



    uint32_t m = sys_c_complex_mixed((uint64_t)(uintptr_t)&mixed);
    check("mixed-return", m == 14);
    check("mixed-double", mixed.real == 1.75);
    check("mixed-integer", mixed.integer == 14);

    uint32_t f = sys_c_complex_float4((uint64_t)(uintptr_t)&floats);
    check("float4-return", f == 4);
    check("float4-a", floats.a == 2.0f);
    check("float4-b", floats.b == 4.0f);
    check("float4-c", floats.c == 6.0f);
    check("float4-d", floats.d == 8.0f);

    uint32_t p = sys_c_complex_packed((uint64_t)(uintptr_t)&packed);
    check("packed-return", p == 5);
    check("packed-tag", packed.tag == 5);
    check("packed-value", packed.value == 0x1234567aU);

    /* Zwei Struct-Parameter, C-Resultat und je alle Felder vergleichen. */
    check("pair8-result", sys_c_pair8((uint64_t)(uintptr_t)&pair8) == 62);
    check("pair8-word0", pair8.words[0] == 20);
    check("pair8-word1", pair8.words[1] == 42);

    check("pair12-result", sys_c_pair12((uint64_t)(uintptr_t)&pair12) == 146);
    check("pair12-word0", pair12.words[0] == 11);
    check("pair12-word1", pair12.words[1] == 42);
    check("pair12-word2", pair12.words[2] == 93);

    check("pair-mixed-result", sys_c_pair_mixed((uint64_t)(uintptr_t)&mixed_pair) == 18);
    check("pair-mixed-real", mixed_pair.real == 1.75);
    check("pair-mixed-int", mixed_pair.integer == 18);

    check("pair-float-result", sys_c_pair_float4((uint64_t)(uintptr_t)&float_pair) == 7);
    check("pair-float-a", float_pair.a == 1.5f);
    check("pair-float-b", float_pair.b == 3.0f);
    check("pair-float-c", float_pair.c == 4.5f);
    check("pair-float-d", float_pair.d == 6.0f);

    check("pair-packed-result", sys_c_pair_packed((uint64_t)(uintptr_t)&packed_pair) == 7);
    check("pair-packed-tag", packed_pair.tag == 7);
    check("pair-packed-value", packed_pair.value == 0x12345681U);

    /* Sechs volle Integerargumente + ein Struct8 -> Stackdruck auf SysV. */
    check("pressure-result", sys_c_pressure((uint64_t)(uintptr_t)&pressure) == 58);
    check("pressure-word0", pressure.words[0] == 31);
    check("pressure-word1", pressure.words[1] == 27);


    /* 12B-Struktur nach 5 bzw. 6 u64: GANZES Aggregat im Stack. */
    check("pressure12-5-return",
        sys_c_pressure12_5((uint64_t)(uintptr_t)&pressure12_5) == 201);
    check("pressure12-5-word0", pressure12_5.words[0] == 25);
    check("pressure12-5-word1", pressure12_5.words[1] == 75);
    check("pressure12-5-word2", pressure12_5.words[2] == 101);

    check("pressure12-6-return",
        sys_c_pressure12_6((uint64_t)(uintptr_t)&pressure12_6) == 269);
    check("pressure12-6-word0", pressure12_6.words[0] == 31);
    check("pressure12-6-word1", pressure12_6.words[1] == 111);
    check("pressure12-6-word2", pressure12_6.words[2] == 127);

    /* Gleiche ABI-Regel fuer 16 Byte / 2 INTEGER-Eightbytes. */
    check("pressure16-5-return",
        sys_c_pressure16_5((uint64_t)(uintptr_t)&pressure16_5) == 112);
    check("pressure16-5-word0", pressure16_5.words[0] == 16);
    check("pressure16-5-word1", pressure16_5.words[1] == 19);
    check("pressure16-5-word2", pressure16_5.words[2] == 32);
    check("pressure16-5-word3", pressure16_5.words[3] == 45);

    /* f64+u32 braucht SSE UND GP: keine halb-register Uebergabe. */
    check("pressure-mixed6-return",
        sys_c_pressure_mixed6((uint64_t)(uintptr_t)&pressure_mixed6) == 32);
    check("pressure-mixed6-f64", pressure_mixed6.real == 1.75);
    check("pressure-mixed6-u32", pressure_mixed6.integer == 32);

    /* Bei zwei 12B-Structs passt nur der erste noch in die GP-Register. */
    check("pressure-pair12-3-return",
        sys_c_pair12_pressure3((uint64_t)(uintptr_t)&pair12_pressure3) == 191);
    check("pressure-pair12-3-word0", pair12_pressure3.words[0] == 26);
    check("pressure-pair12-3-word1", pair12_pressure3.words[1] == 57);
    check("pressure-pair12-3-word2", pair12_pressure3.words[2] == 108);

    /* Die SSE-Register bleiben trotz 6 vorangestellten u64 frei. */
    check("pressure-float4-6-return",
        sys_c_pressure_float4_6((uint64_t)(uintptr_t)&pressure_float4_6) == 6);
    check("pressure-float4-6-a", pressure_float4_6.a == 22.0f);
    check("pressure-float4-6-b", pressure_float4_6.b == 2.5f);
    check("pressure-float4-6-c", pressure_float4_6.c == 5.0f);
    check("pressure-float4-6-d", pressure_float4_6.d == 7.0f);

    if (errors) {
        fprintf(stderr, "MOO_COMPLEX_STRUCT_FAIL: %u/%u\n", errors, tests);
        return 1;
    }
    printf("MOO_COMPLEX_STRUCT_OK: %u/%u\n", tests, tests);
    return 0;
}

/* P2-T13: C reference for x86_64 Linux SysV. Not an implementation of Moo. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#if !defined(__linux__) || !defined(__x86_64__) || (!defined(__GNUC__) && !defined(__clang__))
#error "This reference requires x86_64 Linux and GCC or Clang"
#endif

typedef struct { uint8_t a; uint32_t b; uint16_t c; } Normal;
typedef struct __attribute__((packed)) { uint8_t a; uint32_t b; uint16_t c; } Packed;
typedef struct { uint16_t a; uint8_t b; } Inner;
typedef struct { uint8_t a; Inner inner; uint32_t tail; } Nested;
typedef struct { uint8_t a; uint32_t values[3]; uint8_t tail; } ArrayFields;
typedef struct { Packed p; uint32_t tail; } NestedPacked;
typedef struct __attribute__((packed)) { uint8_t a; Inner inner; uint8_t tail; } PackedNested;
typedef struct { Packed values[2]; uint8_t tail; } PackedArray;
typedef struct __attribute__((aligned(16))) { uint32_t value; } Aligned;
typedef struct { uint8_t a; Aligned inner; uint8_t tail; } NestedAligned;

#define CHECK_LAYOUT(T, S, A) \
    _Static_assert(sizeof(T) == (S), #T " size"); \
    _Static_assert(_Alignof(T) == (A), #T " alignment")
#define CHECK_OFFSET(T, F, N) _Static_assert(offsetof(T, F) == (N), #T "." #F " offset")

CHECK_LAYOUT(Normal, 12, 4); CHECK_OFFSET(Normal, a, 0); CHECK_OFFSET(Normal, b, 4); CHECK_OFFSET(Normal, c, 8);
CHECK_LAYOUT(Packed, 7, 1); CHECK_OFFSET(Packed, a, 0); CHECK_OFFSET(Packed, b, 1); CHECK_OFFSET(Packed, c, 5);
CHECK_LAYOUT(Inner, 4, 2); CHECK_OFFSET(Inner, a, 0); CHECK_OFFSET(Inner, b, 2);
CHECK_LAYOUT(Nested, 12, 4); CHECK_OFFSET(Nested, a, 0); CHECK_OFFSET(Nested, inner, 2); CHECK_OFFSET(Nested, tail, 8);
CHECK_LAYOUT(ArrayFields, 20, 4); CHECK_OFFSET(ArrayFields, a, 0); CHECK_OFFSET(ArrayFields, values, 4); CHECK_OFFSET(ArrayFields, tail, 16);
CHECK_LAYOUT(NestedPacked, 12, 4); CHECK_OFFSET(NestedPacked, p, 0); CHECK_OFFSET(NestedPacked, tail, 8);
CHECK_LAYOUT(PackedNested, 6, 1); CHECK_OFFSET(PackedNested, a, 0); CHECK_OFFSET(PackedNested, inner, 1); CHECK_OFFSET(PackedNested, tail, 5);
CHECK_LAYOUT(PackedArray, 15, 1); CHECK_OFFSET(PackedArray, values, 0); CHECK_OFFSET(PackedArray, tail, 14);
CHECK_LAYOUT(Aligned, 16, 16); CHECK_OFFSET(Aligned, value, 0);
CHECK_LAYOUT(NestedAligned, 48, 16); CHECK_OFFSET(NestedAligned, a, 0); CHECK_OFFSET(NestedAligned, inner, 16); CHECK_OFFSET(NestedAligned, tail, 32);
_Static_assert(sizeof(((ArrayFields *)0)->values) / sizeof(uint32_t) == 3, "array length");
_Static_assert(sizeof(Packed[2]) == 14, "packed array stride");

#define L(T) printf(#T " size=%zu align=%zu\n", sizeof(T), _Alignof(T))
#define F(T, M) printf(#T "." #M " offset=%zu\n", offsetof(T, M))
int main(void) {
    L(Normal); F(Normal, a); F(Normal, b); F(Normal, c);
    L(Packed); F(Packed, a); F(Packed, b); F(Packed, c);
    L(Inner); F(Inner, a); F(Inner, b);
    L(Nested); F(Nested, a); F(Nested, inner); F(Nested, tail);
    L(ArrayFields); F(ArrayFields, a); F(ArrayFields, values); F(ArrayFields, tail);
    L(NestedPacked); F(NestedPacked, p); F(NestedPacked, tail);
    L(PackedNested); F(PackedNested, a); F(PackedNested, inner); F(PackedNested, tail);
    L(PackedArray); F(PackedArray, values); F(PackedArray, tail);
    L(Aligned); F(Aligned, value);
    L(NestedAligned); F(NestedAligned, a); F(NestedAligned, inner); F(NestedAligned, tail);
    return 0;
}

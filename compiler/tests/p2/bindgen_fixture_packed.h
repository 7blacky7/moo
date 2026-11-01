#ifndef P2_BINDGEN_PACKED_FIXTURE_H
#define P2_BINDGEN_PACKED_FIXTURE_H
#include <stdint.h>
typedef struct __attribute__((packed)) P2BindgenPacked {
    uint8_t tag;
    uint32_t value;
    uint16_t array[3];
} P2BindgenPacked;
typedef struct __attribute__((packed, aligned(8))) P2BindgenBoth {
    uint8_t tag;
    uint32_t value;
} P2BindgenBoth;

typedef struct P2BindgenAliasAttr {
    uint8_t tag;
    uint32_t value;
} P2BindgenAliasAttr __attribute__((aligned(16)));

typedef P2BindgenAliasAttr OtherAligned;
typedef OtherAligned P2BindgenChain;

typedef struct P2BindgenUsesAlias {
    uint8_t tag;
    P2BindgenAliasAttr value;
} P2BindgenUsesAlias;

typedef struct P2BindgenUsesChain {
    uint8_t tag;
    P2BindgenChain value;
} P2BindgenUsesChain;

typedef struct P2BindgenFieldAttr {
    uint8_t tag;
    uint32_t value __attribute__((aligned(16)));
} P2BindgenFieldAttr;
struct __attribute__((packed)) P2BindgenInherited;
struct P2BindgenInherited {
    uint8_t tag;
    uint32_t value;
};
typedef uint32_t OtherAlignedU32 __attribute__((aligned(16)));
uint32_t p2_bindgen_aligned_arg(OtherAlignedU32 value);
#pragma pack(push, 1)
typedef struct P2BindgenPragma {
    uint8_t tag;
    uint32_t value;
} P2BindgenPragma;
#pragma pack(pop)
#endif

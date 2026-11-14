#ifndef P2_BINDGEN_LAYOUT_PROBE_H
#define P2_BINDGEN_LAYOUT_PROBE_H
#include <stdint.h>
typedef struct P2LayoutProbe {
    int32_t first;
    int32_t second;
} P2LayoutProbe;

/* Fixed-width fields exercise padding, array stride, and nested alignment. */
typedef struct P2LayoutPadded {
    uint8_t tag;
    uint64_t address;
    uint16_t flags;
} P2LayoutPadded;

typedef struct P2LayoutNested {
    uint8_t head;
    P2LayoutPadded entry;
    uint32_t values[2][3];
    uint8_t tail;
} P2LayoutNested;
#endif

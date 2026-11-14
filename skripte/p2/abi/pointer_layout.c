#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t status;
    uint16_t* daten;
    uint64_t (*ausgabe)(uint8_t*, uint16_t*);
} P2PointerTabelle;
size_t p2_pointer_layout_size(void) { return sizeof(P2PointerTabelle); }
size_t p2_pointer_layout_align(void) { return _Alignof(P2PointerTabelle); }
size_t p2_pointer_layout_status(void) { return offsetof(P2PointerTabelle, status); }
size_t p2_pointer_layout_daten(void) { return offsetof(P2PointerTabelle, daten); }
size_t p2_pointer_layout_ausgabe(void) { return offsetof(P2PointerTabelle, ausgabe); }

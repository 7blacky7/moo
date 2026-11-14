#include <stdint.h>
typedef struct { uint64_t tag; uint64_t data; } MooP2Value;
extern MooP2Value moo_try_take_error(void);
extern void moo_throw(MooP2Value error);
void moo_p2_error_roundtrip(void) {
    MooP2Value error = moo_try_take_error();
    moo_throw(error);
}

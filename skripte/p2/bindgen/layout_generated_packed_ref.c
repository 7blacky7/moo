#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "bindgen_fixture_packed.h"

_Static_assert(sizeof(P2BindgenPacked) == 11, "packed size");
_Static_assert(_Alignof(P2BindgenPacked) == 1, "packed alignment");
_Static_assert(offsetof(P2BindgenPacked, tag) == 0, "tag offset");
_Static_assert(offsetof(P2BindgenPacked, value) == 1, "value offset");
_Static_assert(offsetof(P2BindgenPacked, array) == 5, "array offset");

int main(void) {
    return printf("%zu %zu %zu %zu %zu\n", sizeof(P2BindgenPacked),
        _Alignof(P2BindgenPacked), offsetof(P2BindgenPacked, tag),
        offsetof(P2BindgenPacked, value), offsetof(P2BindgenPacked, array)) < 0 ? 1 : 0;
}

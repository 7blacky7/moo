#include "layout_probe.h"
#include <stddef.h>
#include <stdio.h>
_Static_assert(sizeof(P2LayoutProbe) == 8, "P2LayoutProbe size");
_Static_assert(_Alignof(P2LayoutProbe) == 4, "P2LayoutProbe alignment");
_Static_assert(offsetof(P2LayoutProbe, first) == 0, "first offset");
_Static_assert(offsetof(P2LayoutProbe, second) == 4, "second offset");
/* These expectations are for the documented Linux x86_64 LP64 target. */
_Static_assert(sizeof(P2LayoutPadded) == 24, "padded size");
_Static_assert(_Alignof(P2LayoutPadded) == 8, "padded alignment");
_Static_assert(offsetof(P2LayoutPadded, tag) == 0, "tag offset");
_Static_assert(offsetof(P2LayoutPadded, address) == 8, "address offset");
_Static_assert(offsetof(P2LayoutPadded, flags) == 16, "flags offset");
_Static_assert(sizeof(P2LayoutNested) == 64, "nested size");
_Static_assert(_Alignof(P2LayoutNested) == 8, "nested alignment");
_Static_assert(offsetof(P2LayoutNested, head) == 0, "head offset");
_Static_assert(offsetof(P2LayoutNested, entry) == 8, "entry offset");
_Static_assert(offsetof(P2LayoutNested, values) == 32, "values offset");
_Static_assert(offsetof(P2LayoutNested, tail) == 56, "tail offset");
int main(void) {
    printf("%zu %zu %zu %zu\n", sizeof(P2LayoutProbe), _Alignof(P2LayoutProbe),
           offsetof(P2LayoutProbe, first), offsetof(P2LayoutProbe, second));
    printf("%zu %zu %zu %zu %zu\n", sizeof(P2LayoutPadded), _Alignof(P2LayoutPadded),
           offsetof(P2LayoutPadded, tag), offsetof(P2LayoutPadded, address), offsetof(P2LayoutPadded, flags));
    printf("%zu %zu %zu %zu %zu %zu\n", sizeof(P2LayoutNested), _Alignof(P2LayoutNested),
           offsetof(P2LayoutNested, head), offsetof(P2LayoutNested, entry),
           offsetof(P2LayoutNested, values), offsetof(P2LayoutNested, tail));
    return 0;
}

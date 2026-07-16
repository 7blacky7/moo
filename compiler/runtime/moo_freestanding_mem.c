/*
 * Freestanding bulk-memory intrinsics for native Moo targets without libc.
 *
 * Hosted builds intentionally do not compile this file.  Freestanding targets
 * link it when the C compiler lowers large aggregate copies/initializers to
 * memcpy/memset calls.  Volatile byte accesses prevent either implementation
 * from being folded back into a recursive compiler-generated libcall.
 */
#include <stddef.h>

void *memcpy(void *restrict destination, const void *restrict source,
             size_t count) {
    volatile unsigned char *to = (volatile unsigned char *)destination;
    const volatile unsigned char *from =
        (const volatile unsigned char *)source;
    size_t index;
    for (index = 0u; index < count; ++index) to[index] = from[index];
    return destination;
}

void *memset(void *destination, int value, size_t count) {
    volatile unsigned char *bytes =
        (volatile unsigned char *)destination;
    const unsigned char byte = (unsigned char)value;
    size_t index;
    for (index = 0u; index < count; ++index) bytes[index] = byte;
    return destination;
}

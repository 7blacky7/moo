#include "moo_defer_stack.h"
#include <stddef.h>
#if defined(MOO_BARE) && !defined(MOO_BARE_THREADS)
#include "moo_bare_kern.h"
_Static_assert(sizeof(size_t) <= sizeof(uint64_t), "bare allocator size width");
static void *moo_defer_alloc(size_t size) { return kern_alloc_c((uint64_t)size); }
static void moo_defer_free(void *ptr) { kern_frei_c(ptr); }
#else
#include <stdlib.h>
static void *moo_defer_alloc(size_t size) { return malloc(size); }
static void moo_defer_free(void *ptr) { free(ptr); }
#endif

struct MooDeferStack {
    uint32_t *items;
    size_t count;
    size_t capacity;
};

int moo_defer_stack_new(MooDeferStack **out) {
    if (!out) return MOO_DEFER_INVALID;
    *out = NULL;
    MooDeferStack *stack = moo_defer_alloc(sizeof(*stack));
    if (!stack) return MOO_DEFER_ALLOC;
    stack->items = NULL;
    stack->count = 0;
    stack->capacity = 0;
    *out = stack;
    return MOO_DEFER_OK;
}

int moo_defer_stack_push(MooDeferStack *stack, uint32_t action_id) {
    if (!stack) return MOO_DEFER_INVALID;
    if (stack->count == stack->capacity) {
        size_t limit = SIZE_MAX / sizeof(*stack->items);
        if (stack->capacity >= limit) return MOO_DEFER_OVERFLOW;
        size_t capacity = stack->capacity ? stack->capacity : 8;
        if (stack->capacity) {
            capacity = stack->capacity > limit / 2 ? limit : stack->capacity * 2;
        }
        if (capacity > limit) capacity = limit;
#if defined(MOO_BARE) && !defined(MOO_BARE_THREADS)
        uint32_t *items = moo_defer_alloc(capacity * sizeof(*items));
        if (!items) return MOO_DEFER_ALLOC;
        /* Volatile element copies avoid requiring libc memcpy in freestanding builds. */
        volatile uint32_t *dst = items;
        const volatile uint32_t *src = stack->items;
        for (size_t i = 0; i < stack->count; ++i) dst[i] = src[i];
        moo_defer_free(stack->items);
#else
        uint32_t *items = realloc(stack->items, capacity * sizeof(*items));
        if (!items) return MOO_DEFER_ALLOC;
#endif
        stack->items = items;
        stack->capacity = capacity;
    }
    stack->items[stack->count++] = action_id;
    return MOO_DEFER_OK;
}

int moo_defer_stack_pop(MooDeferStack *stack, uint32_t *out) {
    if (!stack || !out) return MOO_DEFER_INVALID;
    if (!stack->count) return MOO_DEFER_EMPTY;
    *out = stack->items[--stack->count];
    return MOO_DEFER_OK;
}

void moo_defer_stack_free(MooDeferStack *stack) {
    if (!stack) return;
    moo_defer_free(stack->items);
    moo_defer_free(stack);
}

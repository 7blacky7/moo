#include "../../runtime/moo_defer_stack.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

/* GNU ld wrapping is confined to this test binary. */
static int fail_malloc, fail_realloc;
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
void *__wrap_malloc(size_t size) {
    return fail_malloc ? NULL : __real_malloc(size);
}
void *__wrap_realloc(void *ptr, size_t size) {
    return fail_realloc ? NULL : __real_realloc(ptr, size);
}

int main(void) {
    MooDeferStack *a = NULL, *b = NULL;
    uint32_t id = 123;
    assert(moo_defer_stack_new(NULL) == MOO_DEFER_INVALID);
    fail_malloc = 1;
    assert(moo_defer_stack_new(&a) == MOO_DEFER_ALLOC && a == NULL);
    fail_malloc = 0;
    assert(moo_defer_stack_new(&a) == MOO_DEFER_OK);
    assert(moo_defer_stack_new(&b) == MOO_DEFER_OK);
    assert(moo_defer_stack_pop(a, &id) == MOO_DEFER_EMPTY && id == 123);
    assert(moo_defer_stack_pop(a, NULL) == MOO_DEFER_INVALID);
    assert(moo_defer_stack_push(NULL, 0) == MOO_DEFER_INVALID);
    fail_realloc = 1;
    assert(moo_defer_stack_push(a, 17) == MOO_DEFER_ALLOC);
    assert(moo_defer_stack_pop(a, &id) == MOO_DEFER_EMPTY && id == 123);
    fail_realloc = 0;
    for (uint32_t i = 0; i < 8; ++i) assert(moo_defer_stack_push(a, i) == MOO_DEFER_OK);
    fail_realloc = 1;
    assert(moo_defer_stack_push(a, 99) == MOO_DEFER_ALLOC);
    fail_realloc = 0;
    for (uint32_t i = 8; i > 0; --i) {
        assert(moo_defer_stack_pop(a, &id) == MOO_DEFER_OK && id == i - 1);
    }
    assert(moo_defer_stack_push(b, 0) == MOO_DEFER_OK);
    for (uint32_t i = 0; i < 10000; ++i)
        assert(moo_defer_stack_push(a, i % 5) == MOO_DEFER_OK);
    for (uint32_t i = 10000; i > 0; --i)
        assert(moo_defer_stack_pop(a, &id) == MOO_DEFER_OK && id == (i - 1) % 5);
    assert(moo_defer_stack_pop(a, &id) == MOO_DEFER_EMPTY);
    assert(moo_defer_stack_pop(b, &id) == MOO_DEFER_OK && id == 0);
    moo_defer_stack_free(a);
    moo_defer_stack_free(b);
    moo_defer_stack_free(NULL);
    return 0;
}

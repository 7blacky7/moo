#include "../../runtime/moo_defer_stack.h"
/* Hosted harness has int main; implementation TUs retain -DMOO_BARE. */
#undef MOO_BARE
#include "../../runtime/moo_bare_kern.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

_Alignas(16) static unsigned char arena[4096];
void kern_panic(const char *msg) { (void)msg; abort(); }

int main(void) {
    MooDeferStack *stack = (MooDeferStack *)(uintptr_t)1;
    assert(moo_defer_stack_new(&stack) == MOO_DEFER_ALLOC && stack == NULL);
    kern_heap_init_c((uintptr_t)arena, (uintptr_t)(arena + sizeof(arena)));
    assert(moo_defer_stack_new(&stack) == MOO_DEFER_OK);
    uint32_t count = 0;
    int status = MOO_DEFER_OK;
    while (count < 4096 && (status = moo_defer_stack_push(stack, count)) == MOO_DEFER_OK)
        ++count;
    assert(status == MOO_DEFER_ALLOC && count >= 8 && count < 4096);
    for (uint32_t i = count; i > 0; --i) {
        uint32_t value = UINT32_MAX;
        assert(moo_defer_stack_pop(stack, &value) == MOO_DEFER_OK && value == i - 1);
    }
    uint32_t value = 999;
    assert(moo_defer_stack_pop(stack, &value) == MOO_DEFER_EMPTY && value == 999);
    moo_defer_stack_free(stack);
    /* The kernel freelist can serve subsequent independent activations. */
    for (int i = 0; i < 32; ++i) {
        assert(moo_defer_stack_new(&stack) == MOO_DEFER_OK);
        assert(moo_defer_stack_push(stack, 0) == MOO_DEFER_OK);
        assert(moo_defer_stack_pop(stack, &value) == MOO_DEFER_OK && value == 0);
        moo_defer_stack_free(stack);
    }
    return 0;
}

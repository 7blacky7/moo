#include "../../runtime/moo_defer_stack.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

int main() {
    const char *mode = std::getenv("P2_DEFER_INJECT");
    if (!mode) return 2;
    const int expected = std::strcmp(mode, "alloc") == 0 ? MOO_DEFER_ALLOC : MOO_DEFER_OVERFLOW;
    MooDeferStack *stack = nullptr;
    uint32_t id = 0;
    if (moo_defer_stack_new(&stack) != MOO_DEFER_OK ||
        moo_defer_stack_push(stack, 1) != MOO_DEFER_OK ||
        moo_defer_stack_push(stack, 2) != MOO_DEFER_OK ||
        moo_defer_stack_push(stack, 3) != expected ||
        moo_defer_stack_pop(stack, &id) != MOO_DEFER_OK || id != 2 ||
        moo_defer_stack_pop(stack, &id) != MOO_DEFER_OK || id != 1 ||
        moo_defer_stack_pop(stack, &id) != MOO_DEFER_EMPTY) {
        std::fprintf(stderr, "Adapter-Stackvertrag fehlgeschlagen\n");
        moo_defer_stack_free(stack);
        return 1;
    }
    moo_defer_stack_free(stack);
    std::puts("PASS Adapter erhält alte IDs in LIFO-Reihenfolge");
    return 0;
}

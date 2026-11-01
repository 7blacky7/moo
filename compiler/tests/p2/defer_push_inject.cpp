#include "../../runtime/moo_defer_stack.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int __real_moo_defer_stack_push(MooDeferStack *, uint32_t);
extern "C" int __wrap_moo_defer_stack_push(MooDeferStack *stack, uint32_t id) {
    static unsigned calls = 0;
    const char *mode = std::getenv("P2_DEFER_INJECT");
    ++calls;
    if (mode && calls == 3 && (std::strcmp(mode, "alloc") == 0 || std::strcmp(mode, "overflow") == 0)) {
        std::fprintf(stderr, "PUSH_INJECTED:%s\n", mode);
        return std::strcmp(mode, "alloc") == 0 ? MOO_DEFER_ALLOC : MOO_DEFER_OVERFLOW;
    }
    return __real_moo_defer_stack_push(stack, id);
}

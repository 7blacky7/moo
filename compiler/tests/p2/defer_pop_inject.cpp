#include "../../runtime/moo_defer_stack.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

static unsigned pop_count = 0;
static unsigned free_count = 0;
static unsigned null_count = 0;
static unsigned other_count = 0;
static MooDeferStack *injected_stack = nullptr;
extern "C" int __real_moo_defer_stack_pop(MooDeferStack *, uint32_t *);
extern "C" void __real_moo_defer_stack_free(MooDeferStack *);
static void report() { std::fprintf(stderr, "POP_COUNT:%u FREE_COUNT:%u NULL_COUNT:%u OTHER_COUNT:%u\n", pop_count, free_count, null_count, other_count); }
extern "C" int __wrap_moo_defer_stack_pop(MooDeferStack *stack, uint32_t *out) {
    const char *mode = std::getenv("P2_POP_INJECT");
    if (++pop_count == 1) {
        injected_stack = stack;
        std::atexit(report);
        if (mode && std::strcmp(mode, "unknown") == 0) {
            *out = UINT32_MAX;
            std::fprintf(stderr, "INJECT:unknown\n");
            return MOO_DEFER_OK;
        }
        if (mode && std::strcmp(mode, "invalid") == 0) {
            std::fprintf(stderr, "INJECT:invalid\n");
            return MOO_DEFER_INVALID;
        }
    }
    if (mode && std::strcmp(mode, "invalid") == 0) std::_Exit(82);
    return __real_moo_defer_stack_pop(stack, out);
}
extern "C" void __wrap_moo_defer_stack_free(MooDeferStack *stack) {
    if (!stack) ++null_count;
    else if (stack != injected_stack) ++other_count;
    if (stack == injected_stack) {
        ++free_count;
        const char *mode = std::getenv("P2_POP_INJECT");
        if (mode && std::strcmp(mode, "invalid") == 0) std::_Exit(83);
    }
    __real_moo_defer_stack_free(stack);
}

#ifndef MOO_DEFER_STACK_H
#define MOO_DEFER_STACK_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MooDeferStack MooDeferStack;
enum {
    MOO_DEFER_OK = 0,
    MOO_DEFER_EMPTY = 1,
    MOO_DEFER_INVALID = 2,
    MOO_DEFER_ALLOC = 3,
    MOO_DEFER_OVERFLOW = 4,
    MOO_DEFER_UNKNOWN_ACTION = 5 /* dispatcher only, never returned by stack */
};
/* One stack per activation. No call changes the Moo error flag.
 * new clears *out on failure; pop leaves *out unchanged on failure.
 * A failed push preserves all entries. free(NULL) is permitted. */
int moo_defer_stack_new(MooDeferStack **out);
int moo_defer_stack_push(MooDeferStack *stack, uint32_t action_id);
int moo_defer_stack_pop(MooDeferStack *stack, uint32_t *out);
void moo_defer_stack_free(MooDeferStack *stack);

#ifdef __cplusplus
}
#endif
#endif

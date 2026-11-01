#ifndef MOO_DEFER_ERROR_H
#define MOO_DEFER_ERROR_H
#include "moo_runtime.h"
#include "moo_defer_stack.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Allokationsfreier, unveraenderlicher OOM-Fehler fuer den Defer-Stack.
 * out darf keinen owning Wert enthalten. Der Wert kann wie ein +1-Ergebnis an
 * moo_throw uebergeben und beliebig oft retained/released werden; sein
 * statischer MooError hat gemäss moo_memory.c refcount 0 und wird nie frei.
 * Diese Garantie umfasst nicht moo_to_string, printf oder Catch-Code.
 * NULL -> MOO_DEFER_INVALID, sonst MOO_DEFER_OK. */
int moo_defer_oom_value(MooValue *out);

/* Maps error statuses to distinct allocation-free immortal errors, with the
 * ownership contract above. Unknown statuses map to an internal-status error.
 * OK/EMPTY or NULL out return MOO_DEFER_INVALID and leave out unchanged.
 * Does not change the Moo error flag; caller preserves any earlier error. */
int moo_defer_status_value(int status, MooValue *out);

#ifdef __cplusplus
}
#endif
#endif

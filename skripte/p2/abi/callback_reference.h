#ifndef MOO_P2_CALLBACK_REFERENCE_H
#define MOO_P2_CALLBACK_REFERENCE_H
#include <stddef.h>
#include <stdint.h>
typedef int (*MooP2Compare)(const void *, const void *);
typedef int32_t (*MooP2Visit)(int32_t, void *);
/* Both callbacks are borrowed and invoked synchronously, never retained. */
void moo_p2_callback_sort(int32_t *values, size_t count, MooP2Compare compare);
int32_t moo_p2_callback_visit(int32_t value, MooP2Visit visit, void *user_data);
#endif

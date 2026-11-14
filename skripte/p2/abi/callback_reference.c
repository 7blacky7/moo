#include "callback_reference.h"
#include <stdlib.h>
void moo_p2_callback_sort(int32_t *values, size_t count, MooP2Compare compare) {
    qsort(values, count, sizeof(*values), compare);
}
int32_t moo_p2_callback_visit(int32_t value, MooP2Visit visit, void *user_data) {
    return visit(value, user_data);
}

#include "moo_runtime.h"
#include <stdint.h>

int32_t m1_foreign_error(int32_t value) {
    (void)value;
    moo_throw(moo_string_new("M1_FOREIGN_ERROR"));
    return 123;
}

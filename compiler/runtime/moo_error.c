#include "moo_runtime.h"

jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
int moo_try_depth = 0;
MooValue moo_last_error;

void moo_throw(MooValue error) {
    moo_last_error = error;
    if (moo_try_depth > 0) {
        moo_try_depth--;
        longjmp(moo_try_stack[moo_try_depth], 1);
    } else {
        fprintf(stderr, "Unbehandelter Fehler: ");
        moo_print(error);
        exit(1);
    }
}

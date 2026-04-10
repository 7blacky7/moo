#include "moo_runtime.h"

int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];

void moo_throw(MooValue error) {
    moo_last_error = error;
    moo_error_flag = 1;
    if (moo_try_depth > 0) {
        // Wir sind in einem try-Block — nichts tun, der generierte Code prueft das Flag
    } else {
        fprintf(stderr, "Unbehandelter Fehler: ");
        moo_print(error);
        exit(1);
    }
}

void moo_try_enter(void) {
    moo_try_depth++;
    moo_error_flag = 0;
}

int moo_try_check(void) {
    // Gibt 1 zurueck wenn ein Fehler aufgetreten ist
    return moo_error_flag;
}

void moo_try_leave(void) {
    if (moo_try_depth > 0) moo_try_depth--;
    moo_error_flag = 0;
}

MooValue moo_get_error(void) {
    return moo_last_error;
}

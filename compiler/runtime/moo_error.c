#include "moo_runtime.h"

int moo_error_flag = 0;
MooValue moo_last_error = { MOO_NONE, 0 };
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];

static void clear_last_error(void) {
    /* moo_throw darf jeden MooValue konsumieren. Der zentrale Release-Dispatch
       ist fuer Nicht-Heap-Tags ein No-op und gibt Heap-Werte passend frei. */
    moo_release(moo_last_error);
    moo_last_error = moo_none();
    moo_error_flag = 0;
}

/* Konsumiert error (+1 owning) und uebertraegt ihn in die globale Ablage. */
void moo_throw(MooValue error) {
    clear_last_error();
    moo_last_error = error;
    moo_error_flag = 1;
    if (moo_try_depth == 0) {
        fprintf(stderr, "Unbehandelter Fehler: ");
        moo_print(error);
        clear_last_error();
        exit(1);
    }
}

void moo_try_enter(void) {
    if (moo_try_depth == 0) clear_last_error();
    moo_try_depth++;
}

int moo_try_check(void) {
    return moo_error_flag;
}

void moo_try_leave(void) {
    if (moo_try_depth > 0) moo_try_depth--;
    if (moo_try_depth == 0) clear_last_error();
}

/* Liefert +1 owning; der Catch-Code darf den Wert ueber try_leave hinaus halten. */
MooValue moo_get_error(void) {
    MooValue error = moo_last_error;
    moo_retain(error);
    return error;
}

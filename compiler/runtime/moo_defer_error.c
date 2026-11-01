#include "moo_defer_error.h"

static char moo_defer_oom_message[] = "Defer: Speicher voll";
static char moo_defer_invalid_message[] = "Defer: ungueltiger Zustand";
static char moo_defer_overflow_message[] = "Defer: Kapazitaet uebergelaufen";
static char moo_defer_unknown_action_message[] = "Defer: unbekannte Aktion";
static char moo_defer_unknown_message[] = "Defer: unbekannter Status";
static MooError moo_defer_oom_error = {0, moo_defer_oom_message};
static MooError moo_defer_invalid_error = {0, moo_defer_invalid_message};
static MooError moo_defer_overflow_error = {0, moo_defer_overflow_message};
static MooError moo_defer_unknown_action_error = {0, moo_defer_unknown_action_message};
static MooError moo_defer_unknown_error = {0, moo_defer_unknown_message};

int moo_defer_status_value(int status, MooValue *out) {
    if (!out || status == MOO_DEFER_OK || status == MOO_DEFER_EMPTY)
        return MOO_DEFER_INVALID;
    MooError *error;
    switch (status) {
        case MOO_DEFER_ALLOC: error = &moo_defer_oom_error; break;
        case MOO_DEFER_INVALID: error = &moo_defer_invalid_error; break;
        case MOO_DEFER_OVERFLOW: error = &moo_defer_overflow_error; break;
        case MOO_DEFER_UNKNOWN_ACTION: error = &moo_defer_unknown_action_error; break;
        default: error = &moo_defer_unknown_error; break;
    }
    out->tag = MOO_ERROR;
    moo_val_set_ptr(out, error);
    return MOO_DEFER_OK;
}

int moo_defer_oom_value(MooValue *out) {
    return moo_defer_status_value(MOO_DEFER_ALLOC, out);
}

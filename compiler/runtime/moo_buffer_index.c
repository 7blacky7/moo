#include "moo_buffer_index.h"
#include "moo_buffer_value.h"
#include <stdint.h>

static int buffer_index(MooValue index, size_t *out) {
    if (index.tag != MOO_NUMBER) return 0;
    double d = moo_val_as_double(index);
    /* Strict upper bound makes the cast defined even when SIZE_MAX rounds up. */
    if (!(d >= 0.0 && d < (double)SIZE_MAX)) return 0;
    size_t n = (size_t)d;
    if ((double)n != d) return 0;
    *out = n;
    return 1;
}

static int buffer_i32(MooValue value, int32_t *out) {
    if (value.tag != MOO_NUMBER) return 0;
    double d = moo_val_as_double(value);
    if (!(d >= -2147483648.0 && d <= 2147483647.0)) return 0;
    int32_t n = (int32_t)d;
    if ((double)n != d) return 0;
    *out = n;
    return 1;
}

MooValue moo_buffer_index_get(MooValue container, MooValue index) {
    MooBuffer *buffer = NULL;
    size_t i = 0;
    int32_t result = 0;
    int ok = buffer_index(index, &i);
    moo_release(index);
    if (!ok) {
        moo_throw(moo_error("Pufferindex muss eine nichtnegative Ganzzahl sein"));
        return moo_none();
    }
    MooBufferStatus status = moo_buffer_value_borrow(container, MOO_BUFFER_I32, &buffer);
    if (status == MOO_BUFFER_OK)
        status = moo_buffer_get_scalar(buffer, MOO_BUFFER_I32, i, &result);
    if (status != MOO_BUFFER_OK) {
        moo_throw(moo_error("Pufferzugriff: falscher Typ oder Index ausserhalb"));
        return moo_none();
    }
    return moo_number((double)result);
}

void moo_buffer_index_set(MooValue container, MooValue index, MooValue value) {
    MooBuffer *buffer = NULL;
    size_t i = 0;
    int32_t number = 0;
    int index_ok = buffer_index(index, &i);
    int value_ok = buffer_i32(value, &number);
    moo_release(index);
    moo_release(value);
    if (!index_ok || !value_ok) {
        moo_throw(moo_error("Pufferzuweisung erwartet Index und i32-Ganzzahl"));
        return;
    }
    MooBufferStatus status = moo_buffer_value_borrow(container, MOO_BUFFER_I32, &buffer);
    if (status == MOO_BUFFER_OK)
        status = moo_buffer_set_scalar(buffer, MOO_BUFFER_I32, i, &number);
    if (status != MOO_BUFFER_OK)
        moo_throw(moo_error("Pufferzuweisung: falscher Typ oder Index ausserhalb"));
}

#include "moo_runtime.h"

MooValue moo_add(MooValue a, MooValue b) {
    if (a.tag == MOO_STRING || b.tag == MOO_STRING)
        return moo_string_concat(a, b);
    return moo_number(moo_as_number(a) + moo_as_number(b));
}

MooValue moo_sub(MooValue a, MooValue b) {
    return moo_number(moo_as_number(a) - moo_as_number(b));
}

MooValue moo_mul(MooValue a, MooValue b) {
    return moo_number(moo_as_number(a) * moo_as_number(b));
}

MooValue moo_div(MooValue a, MooValue b) {
    double divisor = moo_as_number(b);
    if (divisor == 0.0) {
        moo_throw(moo_error("Division durch Null!"));
        return moo_none();
    }
    return moo_number(moo_as_number(a) / divisor);
}

MooValue moo_mod(MooValue a, MooValue b) {
    return moo_number(fmod(moo_as_number(a), moo_as_number(b)));
}

MooValue moo_pow(MooValue a, MooValue b) {
    return moo_number(pow(moo_as_number(a), moo_as_number(b)));
}

MooValue moo_neg(MooValue v) {
    return moo_number(-moo_as_number(v));
}

MooValue moo_eq(MooValue a, MooValue b) {
    if (a.tag != b.tag) return moo_bool(false);
    switch (a.tag) {
        case MOO_NUMBER: return moo_bool(MV_NUM(a) == MV_NUM(b));
        case MOO_STRING: return moo_string_compare(a, b);
        case MOO_BOOL:   return moo_bool(MV_BOOL(a) == MV_BOOL(b));
        case MOO_NONE:   return moo_bool(true);
        default:         return moo_bool(false);
    }
}

MooValue moo_neq(MooValue a, MooValue b) {
    return moo_bool(!MV_BOOL(moo_eq(a, b)));
}

MooValue moo_lt(MooValue a, MooValue b) { return moo_bool(moo_as_number(a) < moo_as_number(b)); }
MooValue moo_gt(MooValue a, MooValue b) { return moo_bool(moo_as_number(a) > moo_as_number(b)); }
MooValue moo_lte(MooValue a, MooValue b) { return moo_bool(moo_as_number(a) <= moo_as_number(b)); }
MooValue moo_gte(MooValue a, MooValue b) { return moo_bool(moo_as_number(a) >= moo_as_number(b)); }
MooValue moo_and(MooValue a, MooValue b) { return moo_bool(moo_is_truthy(a) && moo_is_truthy(b)); }
MooValue moo_or(MooValue a, MooValue b) { return moo_bool(moo_is_truthy(a) || moo_is_truthy(b)); }
MooValue moo_not(MooValue v) { return moo_bool(!moo_is_truthy(v)); }

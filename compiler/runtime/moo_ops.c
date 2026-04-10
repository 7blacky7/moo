#include "moo_runtime.h"

// Julia-inspirierte Vektor-Operationen: Liste op Zahl, Liste op Liste
// op_func: Zeiger auf die skalare Operation (rekursionsfrei)
typedef MooValue (*ScalarOp)(double, double);

static MooValue scalar_add(double a, double b) { return moo_number(a + b); }
static MooValue scalar_sub(double a, double b) { return moo_number(a - b); }
static MooValue scalar_mul(double a, double b) { return moo_number(a * b); }
static MooValue scalar_div(double a, double b) {
    if (b == 0.0) { moo_throw(moo_error("Division durch Null!")); return moo_none(); }
    return moo_number(a / b);
}
static MooValue scalar_mod(double a, double b) { return moo_number(fmod(a, b)); }
static MooValue scalar_pow(double a, double b) { return moo_number(pow(a, b)); }

static MooValue vec_op(MooValue a, MooValue b, ScalarOp op) {
    if (a.tag == MOO_LIST && b.tag == MOO_LIST) {
        // Liste + Liste → elementweise
        int32_t len = moo_list_iter_len(a);
        int32_t len_b = moo_list_iter_len(b);
        if (len_b < len) len = len_b;
        MooValue result = moo_list_new(len);
        for (int32_t i = 0; i < len; i++) {
            MooValue ea = moo_list_iter_get(a, i);
            MooValue eb = moo_list_iter_get(b, i);
            moo_list_append(result, op(moo_as_number(ea), moo_as_number(eb)));
        }
        return result;
    }
    if (a.tag == MOO_LIST) {
        // Liste op Zahl
        int32_t len = moo_list_iter_len(a);
        double bv = moo_as_number(b);
        MooValue result = moo_list_new(len);
        for (int32_t i = 0; i < len; i++) {
            MooValue el = moo_list_iter_get(a, i);
            moo_list_append(result, op(moo_as_number(el), bv));
        }
        return result;
    }
    if (b.tag == MOO_LIST) {
        // Zahl op Liste
        int32_t len = moo_list_iter_len(b);
        double av = moo_as_number(a);
        MooValue result = moo_list_new(len);
        for (int32_t i = 0; i < len; i++) {
            MooValue el = moo_list_iter_get(b, i);
            moo_list_append(result, op(av, moo_as_number(el)));
        }
        return result;
    }
    return op(moo_as_number(a), moo_as_number(b));
}

MooValue moo_add(MooValue a, MooValue b) {
    if (a.tag == MOO_STRING || b.tag == MOO_STRING)
        return moo_string_concat(a, b);
    if (a.tag == MOO_LIST || b.tag == MOO_LIST)
        return vec_op(a, b, scalar_add);
    return moo_number(moo_as_number(a) + moo_as_number(b));
}

MooValue moo_sub(MooValue a, MooValue b) {
    if (a.tag == MOO_LIST || b.tag == MOO_LIST)
        return vec_op(a, b, scalar_sub);
    return moo_number(moo_as_number(a) - moo_as_number(b));
}

MooValue moo_mul(MooValue a, MooValue b) {
    // String * Zahl = Wiederholung
    if (a.tag == MOO_STRING && b.tag == MOO_NUMBER)
        return moo_string_repeat(a, b);
    if (a.tag == MOO_NUMBER && b.tag == MOO_STRING)
        return moo_string_repeat(b, a);
    if (a.tag == MOO_LIST || b.tag == MOO_LIST)
        return vec_op(a, b, scalar_mul);
    return moo_number(moo_as_number(a) * moo_as_number(b));
}

MooValue moo_div(MooValue a, MooValue b) {
    if (a.tag == MOO_LIST || b.tag == MOO_LIST)
        return vec_op(a, b, scalar_div);
    double divisor = moo_as_number(b);
    if (divisor == 0.0) {
        moo_throw(moo_error("Division durch Null!"));
        return moo_none();
    }
    return moo_number(moo_as_number(a) / divisor);
}

MooValue moo_mod(MooValue a, MooValue b) {
    if (a.tag == MOO_LIST || b.tag == MOO_LIST)
        return vec_op(a, b, scalar_mod);
    return moo_number(fmod(moo_as_number(a), moo_as_number(b)));
}

MooValue moo_pow(MooValue a, MooValue b) {
    if (a.tag == MOO_LIST || b.tag == MOO_LIST)
        return vec_op(a, b, scalar_pow);
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

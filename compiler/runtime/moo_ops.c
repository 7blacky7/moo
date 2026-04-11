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

// Prüft ob alle Elemente einer Liste Zahlen sind
static bool list_all_numbers(MooList* l) {
    for (int32_t i = 0; i < l->length; i++) {
        if (l->items[i].tag != MOO_NUMBER) return false;
    }
    return true;
}

// Schneller Pfad: Direkte double-Array-Operationen fuer reine Zahlenlisten.
// Der C-Compiler kann diese Schleifen mit -O2/-O3 auto-vektorisieren (SSE/AVX).
typedef double (*FastOp)(double, double);
static inline double fast_add(double a, double b) { return a + b; }
static inline double fast_sub(double a, double b) { return a - b; }
static inline double fast_mul(double a, double b) { return a * b; }
static inline double fast_div(double a, double b) { return a / b; }

// Schneller Pfad: Liste op Zahl (alle Numbers)
__attribute__((optimize("O3")))
static MooValue vec_op_fast_scalar(MooList* la, double bv, FastOp fop) {
    int32_t len = la->length;
    MooValue result = moo_list_new(len);
    MooList* rl = MV_LIST(result);
    // Direkt in den items-Array schreiben (kein append-Overhead)
    for (int32_t i = 0; i < len; i++) {
        double av = moo_val_as_double(la->items[i]);
        rl->items[i] = moo_number(fop(av, bv));
    }
    rl->length = len;
    return result;
}

// Schneller Pfad: Liste op Liste (beide nur Numbers)
__attribute__((optimize("O3")))
static MooValue vec_op_fast_pair(MooList* la, MooList* lb, FastOp fop) {
    int32_t len = la->length < lb->length ? la->length : lb->length;
    MooValue result = moo_list_new(len);
    MooList* rl = MV_LIST(result);
    for (int32_t i = 0; i < len; i++) {
        double av = moo_val_as_double(la->items[i]);
        double bv = moo_val_as_double(lb->items[i]);
        rl->items[i] = moo_number(fop(av, bv));
    }
    rl->length = len;
    return result;
}

// Mapping von ScalarOp zu FastOp
static FastOp get_fast_op(ScalarOp op) {
    if (op == scalar_add) return fast_add;
    if (op == scalar_sub) return fast_sub;
    if (op == scalar_mul) return fast_mul;
    if (op == scalar_div) return fast_div;
    return NULL; // mod, pow → kein schneller Pfad
}

static MooValue vec_op(MooValue a, MooValue b, ScalarOp op) {
    FastOp fop = get_fast_op(op);

    if (a.tag == MOO_LIST && b.tag == MOO_LIST) {
        MooList* la = MV_LIST(a);
        MooList* lb = MV_LIST(b);
        // Schneller Pfad: beide nur Zahlen
        if (fop && list_all_numbers(la) && list_all_numbers(lb)) {
            return vec_op_fast_pair(la, lb, fop);
        }
        // Fallback: generisch
        int32_t len = la->length < lb->length ? la->length : lb->length;
        MooValue result = moo_list_new(len);
        for (int32_t i = 0; i < len; i++) {
            moo_list_append(result, op(moo_as_number(la->items[i]), moo_as_number(lb->items[i])));
        }
        return result;
    }
    if (a.tag == MOO_LIST) {
        MooList* la = MV_LIST(a);
        double bv = moo_as_number(b);
        if (fop && list_all_numbers(la)) {
            return vec_op_fast_scalar(la, bv, fop);
        }
        int32_t len = la->length;
        MooValue result = moo_list_new(len);
        for (int32_t i = 0; i < len; i++) {
            moo_list_append(result, op(moo_as_number(la->items[i]), bv));
        }
        return result;
    }
    if (b.tag == MOO_LIST) {
        MooList* lb = MV_LIST(b);
        double av = moo_as_number(a);
        if (fop && list_all_numbers(lb)) {
            return vec_op_fast_scalar(lb, av, fop);
        }
        int32_t len = lb->length;
        MooValue result = moo_list_new(len);
        for (int32_t i = 0; i < len; i++) {
            moo_list_append(result, op(av, moo_as_number(lb->items[i])));
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

MooValue moo_lt(MooValue a, MooValue b) {
    if (a.tag == MOO_STRING && b.tag == MOO_STRING)
        return moo_bool(strcmp(MV_STR(a)->chars, MV_STR(b)->chars) < 0);
    return moo_bool(moo_as_number(a) < moo_as_number(b));
}
MooValue moo_gt(MooValue a, MooValue b) {
    if (a.tag == MOO_STRING && b.tag == MOO_STRING)
        return moo_bool(strcmp(MV_STR(a)->chars, MV_STR(b)->chars) > 0);
    return moo_bool(moo_as_number(a) > moo_as_number(b));
}
MooValue moo_lte(MooValue a, MooValue b) {
    if (a.tag == MOO_STRING && b.tag == MOO_STRING)
        return moo_bool(strcmp(MV_STR(a)->chars, MV_STR(b)->chars) <= 0);
    return moo_bool(moo_as_number(a) <= moo_as_number(b));
}
MooValue moo_gte(MooValue a, MooValue b) {
    if (a.tag == MOO_STRING && b.tag == MOO_STRING)
        return moo_bool(strcmp(MV_STR(a)->chars, MV_STR(b)->chars) >= 0);
    return moo_bool(moo_as_number(a) >= moo_as_number(b));
}
MooValue moo_and(MooValue a, MooValue b) { return moo_bool(moo_is_truthy(a) && moo_is_truthy(b)); }
MooValue moo_or(MooValue a, MooValue b) { return moo_bool(moo_is_truthy(a) || moo_is_truthy(b)); }
MooValue moo_not(MooValue v) { return moo_bool(!moo_is_truthy(v)); }

// Bitwise Operationen
MooValue moo_bitand(MooValue a, MooValue b) { return moo_number((double)((int64_t)moo_as_number(a) & (int64_t)moo_as_number(b))); }
MooValue moo_bitor(MooValue a, MooValue b) { return moo_number((double)((int64_t)moo_as_number(a) | (int64_t)moo_as_number(b))); }
MooValue moo_bitxor(MooValue a, MooValue b) { return moo_number((double)((int64_t)moo_as_number(a) ^ (int64_t)moo_as_number(b))); }
MooValue moo_bitnot(MooValue v) { return moo_number((double)(~(int64_t)moo_as_number(v))); }
MooValue moo_lshift(MooValue a, MooValue b) { return moo_number((double)((int64_t)moo_as_number(a) << (int64_t)moo_as_number(b))); }
MooValue moo_rshift(MooValue a, MooValue b) { return moo_number((double)((int64_t)moo_as_number(a) >> (int64_t)moo_as_number(b))); }

// Raw Memory Access (GEFAEHRLICH — nur fuer Systemprogrammierung)
MooValue moo_mem_read(MooValue addr, MooValue size) {
    uintptr_t a = (uintptr_t)moo_as_number(addr);
    int s = (int)moo_as_number(size);
    if (s == 1) return moo_number((double)(*(volatile uint8_t*)a));
    if (s == 2) return moo_number((double)(*(volatile uint16_t*)a));
    if (s == 4) return moo_number((double)(*(volatile uint32_t*)a));
    if (s == 8) return moo_number((double)(*(volatile uint64_t*)a));
    return moo_none();
}

void moo_mem_write(MooValue addr, MooValue value, MooValue size) {
    uintptr_t a = (uintptr_t)moo_as_number(addr);
    uint64_t v = (uint64_t)moo_as_number(value);
    int s = (int)moo_as_number(size);
    if (s == 1) *(volatile uint8_t*)a = (uint8_t)v;
    else if (s == 2) *(volatile uint16_t*)a = (uint16_t)v;
    else if (s == 4) *(volatile uint32_t*)a = (uint32_t)v;
    else if (s == 8) *(volatile uint64_t*)a = v;
}

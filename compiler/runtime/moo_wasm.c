/**
 * moo_wasm.c — Minimale WASM-kompatible Runtime fuer moo.
 * Kein malloc, kein printf, kein stdio.
 * Nutzt importierte JS-Funktionen fuer Output.
 * Bump-Allocator fuer Speicher.
 *
 * Kompilieren: clang --target=wasm32 -O2 -c moo_wasm.c -o moo_wasm.o
 * Linken: wasm-ld --no-entry --export-all moo_code.o moo_wasm.o -o app.wasm
 */

/* Importierte JS-Funktionen */
__attribute__((import_module("env"), import_name("__wasm_print_str")))
void __wasm_print_str(const char* ptr, int len);

__attribute__((import_module("env"), import_name("__wasm_print_num")))
void __wasm_print_num(double val);

/* ====== Bump Allocator ====== */
static char heap[65536]; /* 64KB Heap */
static int heap_pos = 0;

static void* bump_alloc(int size) {
    /* 8-Byte Alignment */
    int aligned = (heap_pos + 7) & ~7;
    if (aligned + size > (int)sizeof(heap)) return (void*)0;
    void* ptr = &heap[aligned];
    heap_pos = aligned + size;
    return ptr;
}

/* ====== MooValue (identisch zu moo_runtime.h Layout) ====== */
typedef unsigned long long uint64_t;
typedef long long int64_t;
typedef unsigned int uint32_t;
typedef int int32_t;

typedef struct { uint64_t tag; uint64_t data; } MooValue;

#define MOO_NUMBER 0
#define MOO_STRING 1
#define MOO_BOOL   2
#define MOO_NONE   3
#define MOO_LIST   4

/* String-Typ */
typedef struct { int length; char chars[1]; } MooString;

/* List-Typ */
typedef struct { int length; int capacity; MooValue* items; } MooList;

/* MooValue Zugriff (identisch zu moo_runtime.h) */
static double mv_as_double(MooValue v) {
    union { uint64_t i; double d; } u;
    u.i = v.data;
    return u.d;
}

static MooValue mv_from_double(double d) {
    union { double d; uint64_t i; } u;
    u.d = d;
    MooValue v;
    v.tag = MOO_NUMBER;
    v.data = u.i;
    return v;
}

static void* mv_as_ptr(MooValue v) {
    union { uint64_t i; void* p; } u;
    u.i = v.data;
    return u.p;
}

static MooValue mv_from_ptr(uint64_t tag, void* p) {
    union { void* p; uint64_t i; } u;
    u.p = p;
    MooValue v;
    v.tag = tag;
    v.data = u.i;
    return v;
}

/* Forward Declarations */
MooValue moo_string_concat(MooValue a, MooValue b);
MooValue moo_to_string(MooValue v);

/* ====== Konstruktoren ====== */

MooValue moo_number(double d) { return mv_from_double(d); }

MooValue moo_string_new(const char* s) {
    int len = 0;
    while (s[len]) len++;
    MooString* ms = (MooString*)bump_alloc(sizeof(MooString) + len);
    if (!ms) return (MooValue){MOO_NONE, 0};
    ms->length = len;
    for (int i = 0; i < len; i++) ms->chars[i] = s[i];
    ms->chars[len] = 0;
    return mv_from_ptr(MOO_STRING, ms);
}

MooValue moo_bool(int b) {
    MooValue v;
    v.tag = MOO_BOOL;
    v.data = b ? 1 : 0;
    return v;
}

MooValue moo_none(void) { return (MooValue){MOO_NONE, 0}; }

MooValue moo_list_new(int32_t cap) {
    if (cap < 4) cap = 4;
    MooList* l = (MooList*)bump_alloc(sizeof(MooList));
    if (!l) return moo_none();
    l->items = (MooValue*)bump_alloc(cap * sizeof(MooValue));
    l->length = 0;
    l->capacity = cap;
    return mv_from_ptr(MOO_LIST, l);
}

/* ====== Print ====== */

void moo_print(MooValue v) {
    if (v.tag == MOO_NUMBER) {
        __wasm_print_num(mv_as_double(v));
    } else if (v.tag == MOO_STRING) {
        MooString* s = (MooString*)mv_as_ptr(v);
        __wasm_print_str(s->chars, s->length);
    } else if (v.tag == MOO_BOOL) {
        if (v.data) __wasm_print_str("wahr", 4);
        else __wasm_print_str("falsch", 6);
    } else if (v.tag == MOO_NONE) {
        __wasm_print_str("nichts", 6);
    }
}

/* ====== Arithmetik ====== */

double moo_as_number(MooValue v) {
    if (v.tag == MOO_NUMBER) return mv_as_double(v);
    if (v.tag == MOO_BOOL) return v.data ? 1.0 : 0.0;
    return 0.0;
}

MooValue moo_add(MooValue a, MooValue b) {
    if (a.tag == MOO_STRING && b.tag == MOO_STRING) {
        return moo_string_concat(a, b);
    }
    return moo_number(moo_as_number(a) + moo_as_number(b));
}
MooValue moo_sub(MooValue a, MooValue b) { return moo_number(moo_as_number(a) - moo_as_number(b)); }
MooValue moo_mul(MooValue a, MooValue b) { return moo_number(moo_as_number(a) * moo_as_number(b)); }
MooValue moo_div(MooValue a, MooValue b) {
    double d = moo_as_number(b);
    if (d == 0.0) return moo_none();
    return moo_number(moo_as_number(a) / d);
}
MooValue moo_mod(MooValue a, MooValue b) { return moo_number((int)moo_as_number(a) % (int)moo_as_number(b)); }
MooValue moo_neg(MooValue v) { return moo_number(-moo_as_number(v)); }

/* Vergleiche */
int moo_is_truthy(MooValue v) {
    if (v.tag == MOO_BOOL) return v.data != 0;
    if (v.tag == MOO_NUMBER) return mv_as_double(v) != 0.0;
    if (v.tag == MOO_NONE) return 0;
    return 1;
}

int moo_is_none(MooValue v) { return v.tag == MOO_NONE; }

MooValue moo_eq(MooValue a, MooValue b) {
    if (a.tag != b.tag) return moo_bool(0);
    if (a.tag == MOO_NUMBER) return moo_bool(mv_as_double(a) == mv_as_double(b));
    if (a.tag == MOO_BOOL) return moo_bool(a.data == b.data);
    if (a.tag == MOO_NONE) return moo_bool(1);
    return moo_bool(0);
}
MooValue moo_neq(MooValue a, MooValue b) { return moo_bool(!moo_is_truthy(moo_eq(a, b))); }
MooValue moo_lt(MooValue a, MooValue b) { return moo_bool(moo_as_number(a) < moo_as_number(b)); }
MooValue moo_gt(MooValue a, MooValue b) { return moo_bool(moo_as_number(a) > moo_as_number(b)); }
MooValue moo_lte(MooValue a, MooValue b) { return moo_bool(moo_as_number(a) <= moo_as_number(b)); }
MooValue moo_gte(MooValue a, MooValue b) { return moo_bool(moo_as_number(a) >= moo_as_number(b)); }
MooValue moo_and(MooValue a, MooValue b) { return moo_bool(moo_is_truthy(a) && moo_is_truthy(b)); }
MooValue moo_or(MooValue a, MooValue b) { return moo_bool(moo_is_truthy(a) || moo_is_truthy(b)); }
MooValue moo_not(MooValue v) { return moo_bool(!moo_is_truthy(v)); }

/* String-Concat */
MooValue moo_string_concat(MooValue a, MooValue b) {
    MooValue sa = moo_to_string(a);
    MooValue sb = moo_to_string(b);
    MooString* as = (MooString*)mv_as_ptr(sa);
    MooString* bs = (MooString*)mv_as_ptr(sb);
    int total = as->length + bs->length;
    MooString* result = (MooString*)bump_alloc(sizeof(MooString) + total);
    if (!result) return moo_none();
    for (int i = 0; i < as->length; i++) result->chars[i] = as->chars[i];
    for (int i = 0; i < bs->length; i++) result->chars[as->length + i] = bs->chars[i];
    result->chars[total] = 0;
    result->length = total;
    return mv_from_ptr(MOO_STRING, result);
}

/* Einfaches to_string */
MooValue moo_to_string(MooValue v) {
    if (v.tag == MOO_STRING) return v;
    if (v.tag == MOO_BOOL) return moo_string_new(v.data ? "wahr" : "falsch");
    if (v.tag == MOO_NONE) return moo_string_new("nichts");
    /* Zahl als String — einfache Integer-Konvertierung */
    if (v.tag == MOO_NUMBER) {
        double d = mv_as_double(v);
        char buf[32];
        int pos = 0;
        int n = (int)d;
        if (d == (double)n) {
            /* Integer */
            if (n < 0) { buf[pos++] = '-'; n = -n; }
            if (n == 0) { buf[pos++] = '0'; }
            else {
                char tmp[16]; int tpos = 0;
                while (n > 0) { tmp[tpos++] = '0' + (n % 10); n /= 10; }
                while (tpos > 0) buf[pos++] = tmp[--tpos];
            }
            buf[pos] = 0;
        } else {
            /* Float — grob */
            buf[0] = '?'; buf[1] = 0; pos = 1;
        }
        return moo_string_new(buf);
    }
    return moo_string_new("?");
}

/* ====== Stubs fuer nicht-unterstuetzte Features ====== */
MooValue moo_pow(MooValue a, MooValue b) { return moo_number(0); } /* TODO */
MooValue moo_error(const char* msg) { return moo_string_new(msg); }
void moo_throw(MooValue v) { /* kann in WASM nicht throwsn */ }
void moo_retain(MooValue v) { /* Bump-Allocator: kein refcount */ }
void moo_release(MooValue v) { /* Bump-Allocator: kein free */ }
MooValue moo_string_compare(MooValue a, MooValue b) { return moo_bool(0); }
MooValue moo_string_repeat(MooValue s, MooValue n) { return s; }
int32_t moo_try_check(void) { return 0; }
void moo_try_enter(void) {}
void moo_try_leave(void) {}
MooValue moo_get_error(void) { return moo_none(); }

/* Listen-Stubs */
void moo_list_append(MooValue list, MooValue item) {
    if (list.tag != MOO_LIST) return;
    MooList* l = (MooList*)mv_as_ptr(list);
    if (l->length < l->capacity) {
        l->items[l->length++] = item;
    }
}
MooValue moo_list_get(MooValue list, MooValue idx) { return moo_none(); }
int32_t moo_list_iter_len(MooValue list) {
    if (list.tag != MOO_LIST) return 0;
    return ((MooList*)mv_as_ptr(list))->length;
}
MooValue moo_list_iter_get(MooValue list, int32_t i) {
    if (list.tag != MOO_LIST) return moo_none();
    MooList* l = (MooList*)mv_as_ptr(list);
    if (i < 0 || i >= l->length) return moo_none();
    return l->items[i];
}
MooValue moo_range(MooValue start, MooValue end) {
    int s = (int)moo_as_number(start);
    int e = (int)moo_as_number(end);
    MooValue list = moo_list_new(e - s);
    for (int i = s; i < e; i++) moo_list_append(list, moo_number(i));
    return list;
}

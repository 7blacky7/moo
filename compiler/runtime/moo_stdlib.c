#include "moo_runtime.h"
#include <time.h>
#include <unistd.h>

static bool random_seeded = false;

MooValue moo_abs(MooValue v) { return moo_number(fabs(moo_as_number(v))); }
MooValue moo_sqrt(MooValue v) { return moo_number(sqrt(moo_as_number(v))); }
MooValue moo_round(MooValue v) { return moo_number(round(moo_as_number(v))); }
MooValue moo_floor(MooValue v) { return moo_number(floor(moo_as_number(v))); }
MooValue moo_ceil(MooValue v) { return moo_number(ceil(moo_as_number(v))); }

MooValue moo_min(MooValue a, MooValue b) {
    double na = moo_as_number(a), nb = moo_as_number(b);
    return moo_number(na < nb ? na : nb);
}

MooValue moo_max(MooValue a, MooValue b) {
    double na = moo_as_number(a), nb = moo_as_number(b);
    return moo_number(na > nb ? na : nb);
}

MooValue moo_random(void) {
    if (!random_seeded) { srand((unsigned)time(NULL)); random_seeded = true; }
    return moo_number((double)rand() / RAND_MAX);
}

MooValue moo_index_get(MooValue container, MooValue index) {
    switch (container.tag) {
        case MOO_LIST:   return moo_list_get(container, index);
        case MOO_DICT:   return moo_dict_get(container, index);
        case MOO_STRING: return moo_string_index(container, index);
        default:         return moo_none();
    }
}

void moo_index_set(MooValue container, MooValue index, MooValue value) {
    switch (container.tag) {
        case MOO_LIST: moo_list_set(container, index, value); break;
        case MOO_DICT: moo_dict_set(container, index, value); break;
        default: break;
    }
}

MooValue moo_length(MooValue v) {
    switch (v.tag) {
        case MOO_STRING: return moo_number((double)MV_STR(v)->length);
        case MOO_LIST:   return moo_number((double)MV_LIST(v)->length);
        case MOO_DICT:   return moo_number((double)MV_DICT(v)->count);
        default:         return moo_number(0);
    }
}

MooValue moo_range(MooValue start, MooValue end) {
    int32_t s = (int32_t)moo_as_number(start);
    int32_t e = (int32_t)moo_as_number(end);
    int32_t len = e > s ? e - s : 0;
    MooValue list = moo_list_new(len);
    for (int32_t i = s; i < e; i++)
        moo_list_append(list, moo_number((double)i));
    return list;
}

MooValue moo_input(MooValue prompt) {
    if (prompt.tag == MOO_STRING) {
        printf("%s", MV_STR(prompt)->chars);
        fflush(stdout);
    }
    char buf[1024];
    if (fgets(buf, sizeof(buf), stdin)) {
        int len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        return moo_string_new(buf);
    }
    return moo_none();
}

// === Immutable/Freeze ===

MooValue moo_freeze(MooValue v) {
    switch (v.tag) {
        case MOO_LIST:   MV_LIST(v)->frozen = true; break;
        case MOO_DICT:   MV_DICT(v)->frozen = true; break;
        case MOO_OBJECT: MV_OBJ(v)->frozen = true; break;
        default: break; // Zahlen, Strings, Booleans sind ohnehin immutable
    }
    return v; // Gibt den eingefrorenen Wert zurück
}

MooValue moo_is_frozen(MooValue v) {
    switch (v.tag) {
        case MOO_LIST:   return moo_bool(MV_LIST(v)->frozen);
        case MOO_DICT:   return moo_bool(MV_DICT(v)->frozen);
        case MOO_OBJECT: return moo_bool(MV_OBJ(v)->frozen);
        default: return moo_bool(true); // Primitive sind immer "frozen"
    }
}

// === Currying ===
// Erzeugt eine neue Funktion die den ersten Parameter bindet.
// Speichert: Original-Funktionspointer + gebundene Argumente in einem MooObject.
// Beim Aufruf werden gebundene Args vorangestellt.

typedef struct {
    void* original_fn;
    int32_t original_arity;
    MooValue* bound_args;
    int32_t bound_count;
} MooCurried;

// Trampoline-Funktionen fuer verschiedene Aritaeten
static MooValue curry_call_1(MooValue arg1) {
    // Wir holen das MooCurried ueber einen globalen Pointer — das ist NICHT threadsafe
    // EHRLICH: Eine echte Loesung braeuchte Closures mit Capture-Environment
    return moo_none();
}

MooValue moo_curry(MooValue func, MooValue arg) {
    if (func.tag != MOO_FUNC) {
        moo_throw(moo_string_new("curry: Erstes Argument muss eine Funktion sein"));
        return moo_none();
    }
    MooFunc* fn = MV_FUNC(func);

    // Neues MooFunc mit gebundenen Args als Object speichern
    MooValue curried_obj = moo_object_new("__curried__");
    moo_object_set(curried_obj, "__fn_ptr", func);
    moo_object_set(curried_obj, "__bound", arg);
    moo_object_set(curried_obj, "__arity", moo_number((double)(fn->arity - 1)));

    // EHRLICH: Echtes Currying braeuchte einen Trampoline der zur Laufzeit
    // die gebundenen Args voranstellt und die echte Funktion aufruft.
    // Das ist mit reinem C ohne JIT/Closure-Runtime nicht sauber loesbar.
    // Wir speichern die Daten, aber der Aufruf muss im Codegen passieren.
    return curried_obj;
}

// === Timing ===

MooValue moo_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double secs = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    return moo_number(secs);
}

// === Syscall (Linux only) ===
#ifdef __linux__
#include <sys/syscall.h>

MooValue moo_syscall(MooValue nr, MooValue arg1, MooValue arg2, MooValue arg3) {
    long sys_nr = (long)moo_as_number(nr);
    long a1 = 0, a2 = 0, a3 = 0;

    // Argumente: Zahlen als long, Strings als Pointer
    if (arg1.tag == MOO_STRING) a1 = (long)MV_STR(arg1)->chars;
    else if (arg1.tag == MOO_NUMBER) a1 = (long)moo_as_number(arg1);

    if (arg2.tag == MOO_STRING) a2 = (long)MV_STR(arg2)->chars;
    else if (arg2.tag == MOO_NUMBER) a2 = (long)moo_as_number(arg2);

    if (arg3.tag == MOO_STRING) a3 = (long)MV_STR(arg3)->chars;
    else if (arg3.tag == MOO_NUMBER) a3 = (long)moo_as_number(arg3);

    long result = syscall(sys_nr, a1, a2, a3);
    return moo_number((double)result);
}
#else
MooValue moo_syscall(MooValue nr, MooValue arg1, MooValue arg2, MooValue arg3) {
    moo_throw(moo_string_new("syscall ist nur auf Linux verfuegbar"));
    return moo_none();
}
#endif

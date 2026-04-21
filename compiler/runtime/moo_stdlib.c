#include "moo_runtime.h"
#include <time.h>
#include <unistd.h>

static bool random_seeded = false;

MooValue moo_abs(MooValue v) { return moo_number(fabs(moo_as_number(v))); }
MooValue moo_sqrt(MooValue v) { return moo_number(sqrt(moo_as_number(v))); }
MooValue moo_sin(MooValue v)  { return moo_number(sin(moo_as_number(v))); }
MooValue moo_cos(MooValue v)  { return moo_number(cos(moo_as_number(v))); }
MooValue moo_tan(MooValue v)  { return moo_number(tan(moo_as_number(v))); }
MooValue moo_atan2(MooValue y, MooValue x) { return moo_number(atan2(moo_as_number(y), moo_as_number(x))); }
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
    // RB9 (v2): Return-Wert muss +1 owning sein, weil Codegen v2 den
    // Arg nach builtin-Call released. Ohne retain waere der Return ein
    // toter Handle (Arg-refcount faellt auf 0).
    moo_retain(v);
    return v;
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

    // HINWEIS: Universeller moo_curry auf beliebigen Funktionen setzt voraus,
    // dass jede Funktion einen Curry-Trampoline besitzt. Fuer Lambdas mit
    // Captures (die bereits einen Trampoline haben) kann der Codegen direkt
    // moo_func_with_captures nutzen. Dieser Weg hier wird fuer selten genutzte
    // Dynamic-Curry-Faelle vorgehalten.
    return curried_obj;
}

// === First-Class Funktionen (MOO_FUNC-Values) ===
//
// moo_func_new: Erstellt ein MOO_FUNC-Value fuer eine einfache Funktion
// (benannte Funktion oder Lambda ohne Captures). Der fn_ptr wird beim Aufruf
// direkt als MooValue(*)(MooValue...)-Funktion gecastet.
MooValue moo_func_new(void* fn_ptr, int32_t arity, const char* name) {
    MooFunc* f = (MooFunc*)moo_alloc(sizeof(MooFunc));
    f->refcount = 1;
    f->fn_ptr = fn_ptr;
    f->arity = arity;
    f->name = name ? strdup(name) : NULL;
    f->captured = NULL;
    f->n_captured = 0;
    MooValue v;
    v.tag = MOO_FUNC;
    moo_val_set_ptr(&v, f);
    return v;
}

// Helper fuer den Trampoline-Codegen: liefert die gebundene Capture an
// Position i. Wird vom vom Codegen generierten Trampoline einmal pro
// Capture aufgerufen um das Environment auszupacken.
// Rueckgabe bei Index out of bounds: moo_none() (defensiv).
MooValue moo_func_captured_at(MooFunc* fn, int32_t i) {
    if (!fn || !fn->captured || i < 0 || i >= fn->n_captured) {
        return moo_none();
    }
    return fn->captured[i];
}

// Universelle Indirect-Call-Helpers fuer MOO_FUNC-Values.
// Waehlen plain- oder Closure-Call-Konvention basierend auf n_captured.
// Werden u.a. von list.map / list.filter und von Variable-Aufrufen genutzt,
// wo der Codegen keine direkte LLVM-Function kennt.
MooValue moo_func_call_0(MooValue func) {
    if (func.tag != MOO_FUNC) {
        moo_throw(moo_string_new("Aufruf auf Nicht-Funktion"));
        return moo_none();
    }
    MooFunc* fn = MV_FUNC(func);
    if (fn->n_captured > 0) {
        MooValue (*tramp)(MooFunc*) = (MooValue(*)(MooFunc*))fn->fn_ptr;
        return tramp(fn);
    }
    MooValue (*plain)(void) = (MooValue(*)(void))fn->fn_ptr;
    return plain();
}

MooValue moo_func_call_1(MooValue func, MooValue a0) {
    if (func.tag != MOO_FUNC) {
        moo_throw(moo_string_new("Aufruf auf Nicht-Funktion"));
        return moo_none();
    }
    MooFunc* fn = MV_FUNC(func);
    if (fn->n_captured > 0) {
        MooValue (*tramp)(MooFunc*, MooValue) =
            (MooValue(*)(MooFunc*, MooValue))fn->fn_ptr;
        return tramp(fn, a0);
    }
    MooValue (*plain)(MooValue) = (MooValue(*)(MooValue))fn->fn_ptr;
    return plain(a0);
}

MooValue moo_func_call_2(MooValue func, MooValue a0, MooValue a1) {
    if (func.tag != MOO_FUNC) {
        moo_throw(moo_string_new("Aufruf auf Nicht-Funktion"));
        return moo_none();
    }
    MooFunc* fn = MV_FUNC(func);
    if (fn->n_captured > 0) {
        MooValue (*tramp)(MooFunc*, MooValue, MooValue) =
            (MooValue(*)(MooFunc*, MooValue, MooValue))fn->fn_ptr;
        return tramp(fn, a0, a1);
    }
    MooValue (*plain)(MooValue, MooValue) =
        (MooValue(*)(MooValue, MooValue))fn->fn_ptr;
    return plain(a0, a1);
}

MooValue moo_func_call_3(MooValue func, MooValue a0, MooValue a1, MooValue a2) {
    if (func.tag != MOO_FUNC) {
        moo_throw(moo_string_new("Aufruf auf Nicht-Funktion"));
        return moo_none();
    }
    MooFunc* fn = MV_FUNC(func);
    if (fn->n_captured > 0) {
        MooValue (*tramp)(MooFunc*, MooValue, MooValue, MooValue) =
            (MooValue(*)(MooFunc*, MooValue, MooValue, MooValue))fn->fn_ptr;
        return tramp(fn, a0, a1, a2);
    }
    MooValue (*plain)(MooValue, MooValue, MooValue) =
        (MooValue(*)(MooValue, MooValue, MooValue))fn->fn_ptr;
    return plain(a0, a1, a2);
}

MooValue moo_func_call_4(MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3) {
    if (func.tag != MOO_FUNC) {
        moo_throw(moo_string_new("Aufruf auf Nicht-Funktion"));
        return moo_none();
    }
    MooFunc* fn = MV_FUNC(func);
    if (fn->n_captured > 0) {
        MooValue (*tramp)(MooFunc*, MooValue, MooValue, MooValue, MooValue) =
            (MooValue(*)(MooFunc*, MooValue, MooValue, MooValue, MooValue))fn->fn_ptr;
        return tramp(fn, a0, a1, a2, a3);
    }
    MooValue (*plain)(MooValue, MooValue, MooValue, MooValue) =
        (MooValue(*)(MooValue, MooValue, MooValue, MooValue))fn->fn_ptr;
    return plain(a0, a1, a2, a3);
}

MooValue moo_func_call_5(MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4) {
    if (func.tag != MOO_FUNC) {
        moo_throw(moo_string_new("Aufruf auf Nicht-Funktion"));
        return moo_none();
    }
    MooFunc* fn = MV_FUNC(func);
    if (fn->n_captured > 0) {
        MooValue (*tramp)(MooFunc*, MooValue, MooValue, MooValue, MooValue, MooValue) =
            (MooValue(*)(MooFunc*, MooValue, MooValue, MooValue, MooValue, MooValue))fn->fn_ptr;
        return tramp(fn, a0, a1, a2, a3, a4);
    }
    MooValue (*plain)(MooValue, MooValue, MooValue, MooValue, MooValue) =
        (MooValue(*)(MooValue, MooValue, MooValue, MooValue, MooValue))fn->fn_ptr;
    return plain(a0, a1, a2, a3, a4);
}

MooValue moo_func_call_6(MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4, MooValue a5) {
    if (func.tag != MOO_FUNC) {
        moo_throw(moo_string_new("Aufruf auf Nicht-Funktion"));
        return moo_none();
    }
    MooFunc* fn = MV_FUNC(func);
    if (fn->n_captured > 0) {
        MooValue (*tramp)(MooFunc*, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue) =
            (MooValue(*)(MooFunc*, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue))fn->fn_ptr;
        return tramp(fn, a0, a1, a2, a3, a4, a5);
    }
    MooValue (*plain)(MooValue, MooValue, MooValue, MooValue, MooValue, MooValue) =
        (MooValue(*)(MooValue, MooValue, MooValue, MooValue, MooValue, MooValue))fn->fn_ptr;
    return plain(a0, a1, a2, a3, a4, a5);
}

MooValue moo_func_call_7(MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4, MooValue a5, MooValue a6) {
    if (func.tag != MOO_FUNC) {
        moo_throw(moo_string_new("Aufruf auf Nicht-Funktion"));
        return moo_none();
    }
    MooFunc* fn = MV_FUNC(func);
    if (fn->n_captured > 0) {
        MooValue (*tramp)(MooFunc*, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue) =
            (MooValue(*)(MooFunc*, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue))fn->fn_ptr;
        return tramp(fn, a0, a1, a2, a3, a4, a5, a6);
    }
    MooValue (*plain)(MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue) =
        (MooValue(*)(MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue))fn->fn_ptr;
    return plain(a0, a1, a2, a3, a4, a5, a6);
}

MooValue moo_func_call_8(MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4, MooValue a5, MooValue a6, MooValue a7) {
    if (func.tag != MOO_FUNC) {
        moo_throw(moo_string_new("Aufruf auf Nicht-Funktion"));
        return moo_none();
    }
    MooFunc* fn = MV_FUNC(func);
    if (fn->n_captured > 0) {
        MooValue (*tramp)(MooFunc*, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue) =
            (MooValue(*)(MooFunc*, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue))fn->fn_ptr;
        return tramp(fn, a0, a1, a2, a3, a4, a5, a6, a7);
    }
    MooValue (*plain)(MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue) =
        (MooValue(*)(MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue, MooValue))fn->fn_ptr;
    return plain(a0, a1, a2, a3, a4, a5, a6, a7);
}

// moo_func_with_captures: Erstellt ein MOO_FUNC-Value fuer ein Closure-Lambda.
// Der tramp_ptr zeigt auf einen vom Codegen erzeugten Trampoline mit der
// Signatur (MooFunc* env, MooValue... user_args). Der Trampoline liest
// env->captured[i] und ruft die eigentliche Inner-Function mit
// (user_args..., captures...).
//
// Captures werden retain-t — sie gehoeren dem MooFunc.
// caps darf ein temporaeres Array sein (wir kopieren es intern).
MooValue moo_func_with_captures(void* tramp_ptr, int32_t arity,
                                const char* name,
                                MooValue* caps, int32_t n) {
    MooFunc* f = (MooFunc*)moo_alloc(sizeof(MooFunc));
    f->refcount = 1;
    f->fn_ptr = tramp_ptr;
    f->arity = arity;
    f->name = name ? strdup(name) : NULL;
    if (n > 0 && caps != NULL) {
        f->captured = (MooValue*)moo_alloc(sizeof(MooValue) * n);
        for (int32_t i = 0; i < n; i++) {
            f->captured[i] = caps[i];
            moo_retain(caps[i]);
        }
        f->n_captured = n;
    } else {
        f->captured = NULL;
        f->n_captured = 0;
    }
    MooValue v;
    v.tag = MOO_FUNC;
    moo_val_set_ptr(&v, f);
    return v;
}

// === Timing ===

MooValue moo_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double secs = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
    return moo_number(secs);
}

// Zeit in Millisekunden (monotonic) — fuer Game-Loops und Performance-Messung
MooValue moo_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
    return moo_number(ms);
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

// === Interaktiver Debugger ===

void moo_breakpoint(MooValue line_num) {
    int line = (line_num.tag == MOO_NUMBER) ? (int)MV_NUM(line_num) : 0;
    fprintf(stderr, "\n[Haltepunkt Zeile %d]\n", line);
    fprintf(stderr, "  Befehle: weiter/continue, ende/quit\n");

    char buf[256];
    while (1) {
        fprintf(stderr, "> ");
        fflush(stderr);
        if (!fgets(buf, sizeof(buf), stdin)) break;

        // Newline entfernen
        int len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';

        // Leere Eingabe ignorieren
        if (buf[0] == '\0') continue;

        // Befehle
        if (strcmp(buf, "weiter") == 0 || strcmp(buf, "continue") == 0 || strcmp(buf, "c") == 0) {
            return;
        }
        if (strcmp(buf, "ende") == 0 || strcmp(buf, "quit") == 0 || strcmp(buf, "q") == 0) {
            fprintf(stderr, "Programm beendet.\n");
            exit(0);
        }
        if (strcmp(buf, "hilfe") == 0 || strcmp(buf, "help") == 0 || strcmp(buf, "h") == 0) {
            fprintf(stderr, "  weiter/continue/c — weiter bis zum naechsten Haltepunkt\n");
            fprintf(stderr, "  ende/quit/q       — Programm beenden\n");
            fprintf(stderr, "  hilfe/help/h      — diese Hilfe\n");
            continue;
        }

        // Alles andere: Kann Variablen nicht inspizieren (ehrlich)
        fprintf(stderr, "  Variablen-Inspektion nicht verfuegbar (nativer Compiler).\n");
        fprintf(stderr, "  Tipp: 'zeige variable' vor dem Haltepunkt einfuegen.\n");
    }
}

/**
 * moo_core.c — Kern-Builtins die jede Sprache braucht.
 * sleep, env, args, exit, string-to-number
 */

#include "moo_runtime.h"
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

/* === prozess_id/pid — getpid() Cross-Platform === */
#ifdef _WIN32
#include <process.h>
#define moo_getpid() ((int)_getpid())
#else
#define moo_getpid() ((int)getpid())
#endif

MooValue moo_pid(void) {
    return moo_number((double)moo_getpid());
}

/* === sleep/schlafe === */
/* schlafe(sekunden) — pausiert die Ausfuehrung */
void moo_sleep(MooValue duration) {
    double secs = moo_as_number(duration);
    if (secs <= 0) return;

    struct timespec ts;
    ts.tv_sec = (time_t)secs;
    ts.tv_nsec = (long)((secs - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

/* === env/umgebung === */
/* umgebung("HOME") → "/home/user" oder nichts */
MooValue moo_env(MooValue name) {
    if (name.tag != MOO_STRING) return moo_none();
    const char* val = getenv(MV_STR(name)->chars);
    if (!val) return moo_none();
    return moo_string_new(val);
}

/* === exit/beende === */
/* beende(code) — beendet das Programm mit Exit-Code */
void moo_exit(MooValue code) {
    int c = (int)moo_as_number(code);
    exit(c);
}

/* === string-to-number/zahl === */
/* zahl("42") → 42.0, zahl("3.14") → 3.14, zahl("abc") → nichts */
MooValue moo_to_number(MooValue v) {
    if (v.tag == MOO_NUMBER) return v;
    if (v.tag == MOO_BOOL) return moo_number(MV_BOOL(v) ? 1.0 : 0.0);
    if (v.tag == MOO_STRING) {
        const char* s = MV_STR(v)->chars;
        char* end;
        double result = strtod(s, &end);
        if (end == s) return moo_none(); /* Konvertierung fehlgeschlagen */
        return moo_number(result);
    }
    return moo_none();
}

/* === args/argumente === */
/* Globale Variablen die von main gesetzt werden */
static int g_argc = 0;
static char** g_argv = NULL;

void moo_args_init(int argc, char** argv) {
    g_argc = argc;
    g_argv = argv;
}

/* argumente() → Liste aller Kommandozeilen-Argumente */
MooValue moo_args(void) {
    MooValue list = moo_list_new(g_argc);
    for (int i = 0; i < g_argc; i++) {
        moo_list_append(list, moo_string_new(g_argv[i]));
    }
    return list;
}

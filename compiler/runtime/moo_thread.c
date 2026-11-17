#include "moo_runtime.h"
#include "moo_rc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "moo_buffer_value.h"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

// === Thread ===
//
// Unterscheidet zwei Call-Konventionen:
//   * Plain-Function (n_captured == 0): fn_ptr(arg)
//   * Closure       (n_captured >  0): fn_ptr(env=MooFunc*, arg)
// Der MooFunc selbst wird als Env-Pointer uebergeben; der vom Codegen
// erzeugte Trampoline liest die Captures ueber moo_func_captured_at.

typedef struct {
    void* fn_ptr;       // plain oder trampoline
    MooFunc* env;       // NULL fuer Plain, sonst MooFunc*
    MooValue arg;
    // P2-T119: -1 = Einzelargument (arg, bisheriger Weg); sonst ist arg eine
    // (geteilte) Liste mit n_args Werten, aufgerufen ueber den Funktionswert
    // env (dann immer gesetzt).
    int32_t n_args;
#ifdef _WIN32
    MooThread* owner;   // Windows: Ergebnis direkt im Thread-Objekt speichern
#endif
} MooThreadArgs;

#ifdef _WIN32
static DWORD WINAPI thread_runner_win(LPVOID raw);
#else
static void* thread_runner_posix(void* raw);
#endif

/* === P2-T120: Postfach je Thread (Freigabe-Handoff) ===
 * Ein ungeteiltes Thread-Ergebnis gehoert dem ersten Wartenden; sein
 * Refcount ist nicht atomar. Gibt ein ANDERER Thread den Thread-Wert zuletzt
 * frei, darf er das Ergebnis deshalb nicht selbst freigeben. Er legt es in
 * das Postfach des Wartenden (atomarer Besitzwechsel unter der Postfach-
 * Sperre, der Refcount wird dabei nicht angefasst); der Wartende gibt es
 * selbst frei (bei seinem naechsten warten/Thread-Freigeben bzw. am Ende
 * seines Threads). Ist der Wartende ein beendeter Worker, gehoert alles, was
 * er hielt, seinem Joiner (Erbe): das Postfach verweist nach dem Join auf das
 * Postfach des Joiners, die Abgabe folgt der Kette. Liegt am Ende der Kette
 * das eigene Postfach, gibt der Thread direkt frei.
 * Der Worker-Start (MooThreadArgs) steckt im Postfach des Workers: keine
 * zusaetzliche Allokation je starte. */
typedef struct MooAbgabe {
    MooValue wert;
    struct MooAbgabe* next;
} MooAbgabe;

typedef struct MooPostfach {
    MooThreadArgs start;        /* erstes Feld: Startdaten des Workers */
    int32_t refs;               /* atomar */
    int32_t sperre;             /* Spinlock (atomar), schuetzt die Felder darunter */
    bool lebt;                  /* Besitzer-Thread laeuft noch */
    struct MooPostfach* erbe;   /* nach dem Join: Postfach des Joiners (+1) */
    MooAbgabe* liste;           /* abgegebene Werte, die der Besitzer freigibt */
} MooPostfach;

static MOO_TLS MooPostfach* moo_mein_postfach;

#if defined(_MSC_VER) && !defined(__clang__)
#define MOO_POST_INC(p) ((int32_t)InterlockedIncrement((volatile LONG*)(p)))
#define MOO_POST_DEC(p) ((int32_t)InterlockedDecrement((volatile LONG*)(p)))
#define MOO_POST_XCHG(p, v) ((int32_t)InterlockedExchange((volatile LONG*)(p), (v)))
#define MOO_POST_STORE(p, v) ((void)InterlockedExchange((volatile LONG*)(p), (v)))
#else
#define MOO_POST_INC(p) __atomic_add_fetch((p), 1, __ATOMIC_RELAXED)
#define MOO_POST_DEC(p) __atomic_sub_fetch((p), 1, __ATOMIC_ACQ_REL)
#define MOO_POST_XCHG(p, v) __atomic_exchange_n((p), (v), __ATOMIC_ACQUIRE)
#define MOO_POST_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#endif

static void post_lock(MooPostfach* p) {
    while (MOO_POST_XCHG(&p->sperre, 1)) {
#ifdef _WIN32
        YieldProcessor();
#endif
    }
}

static void post_unlock(MooPostfach* p) {
    MOO_POST_STORE(&p->sperre, 0);
}

static MooPostfach* post_neu(int32_t refs) {
    MooPostfach* p = (MooPostfach*)malloc(sizeof(MooPostfach));
    p->refs = refs;
    p->sperre = 0;
    p->lebt = true;
    p->erbe = NULL;
    p->liste = NULL;
    return p;
}

static void post_liste_freigeben(MooAbgabe* l) {
    while (l) {
        MooAbgabe* n = l->next;
        moo_release(l->wert);
        free(l);
        l = n;
    }
}

static void post_release(MooPostfach* p) {
    while (p && MOO_POST_DEC(&p->refs) == 0) {
        /* Letzte Referenz: niemand kann mehr abgeben oder uebernehmen. Eine
           Restliste (Besitzer und Erbe weg) gehoert damit diesem Thread. */
        MooPostfach* erbe = p->erbe;
        MooAbgabe* l = p->liste;
        free(p);
        post_liste_freigeben(l);
        p = erbe;
    }
}

/* Eigenes Postfach (lazy fuer Hauptthread und fremde C-Threads; diese gelten
   als lebend bis zum Prozessende). */
static MooPostfach* post_mein(void) {
    if (!moo_mein_postfach) moo_mein_postfach = post_neu(1);
    return moo_mein_postfach;
}

/* Vom Besitzer-Thread: abgegebene Werte selbst freigeben. */
static void post_leeren(void) {
    MooPostfach* p = moo_mein_postfach;
    if (!p) return;
    post_lock(p);
    MooAbgabe* l = p->liste;
    p->liste = NULL;
    post_unlock(p);
    post_liste_freigeben(l);
}

/* Wert (+1, ungeteilt) an den Besitzer von p uebergeben. */
static void post_abgeben(MooPostfach* p, MooValue wert) {
    MooAbgabe* a = NULL;
    MOO_POST_INC(&p->refs);
    for (;;) {
        if (p == moo_mein_postfach) {
            /* Der Besitzer bin ich: direkt freigeben. */
            post_release(p);
            free(a);
            moo_release(wert);
            return;
        }
        if (!a) {
            a = (MooAbgabe*)malloc(sizeof(MooAbgabe));
            a->wert = wert;
        }
        post_lock(p);
        if (p->lebt || !p->erbe) {
            /* Lebender Besitzer, oder Worker beendet und noch nicht gejoint:
               dann uebernimmt der kuenftige Joiner die Liste. */
            a->next = p->liste;
            p->liste = a;
            post_unlock(p);
            post_release(p);
            return;
        }
        MooPostfach* n = p->erbe;
        MOO_POST_INC(&n->refs);
        post_unlock(p);
        post_release(p);
        p = n;
    }
}

/* Joiner nach dem Join des Workers mit Postfach w: erbt dessen Werte. */
static void post_uebernehmen(MooPostfach* w) {
    if (!w) return;
    MooPostfach* ich = post_mein();
    post_lock(w);
    MooAbgabe* l = w->liste;
    w->liste = NULL;
    if (!w->erbe) {
        MOO_POST_INC(&ich->refs);
        w->erbe = ich;
    }
    post_unlock(w);
    post_liste_freigeben(l);
}

/* P2-T114 Phase C: Globale gehoeren dem Hauptthread (Besitzer). Jeder per
   moo_thread_spawn gestartete Thread setzt seine Kennung. Seit Phase B
   gelten auch fremde C-Threads als fremd (siehe unten). */
/* P2-T114 B: nicht static, der Codegen liest die Kennung auf Linux direkt
   (local-exec wie moo_error_flag) einmal je Funktionseintritt.
   Bedeutung "dieser Thread ist nicht Besitzer der Globalen": Startwert 1 fuer
   JEDEN Thread (Moo-Worker und fremde C-Threads, z. B. Rueckrufe aus
   Bibliotheken); nur der Hauptthread setzt sich vor main auf 0. */
MOO_TLS uint8_t moo_ist_moo_worker = 1;

#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((constructor))
#endif
static void moo_hauptthread_markieren(void) {
    moo_ist_moo_worker = 0;
}
#if defined(_MSC_VER) && !defined(__clang__)
/* MSVC besitzt kein GNU-constructor-Attribut. Auch vor dem ersten starte
   muss der Hauptthread bereits Besitzer sein; fremde C-Threads bleiben 1. */
#pragma section(".CRT$XCU", read)
__declspec(allocate(".CRT$XCU"))
void (__cdecl* const moo_hauptthread_initialisierung)(void) = moo_hauptthread_markieren;
/* Den CRT-Eintrag auch bei Ganzprogrammoptimierung als Wurzel erhalten. */
#ifdef _M_IX86
#pragma comment(linker, "/include:_moo_hauptthread_initialisierung")
#else
#pragma comment(linker, "/include:moo_hauptthread_initialisierung")
#endif
#endif
#endif
/* Thread-Marke: ihre Adresse kennzeichnet den Thread (warten, Sperre). */
static MOO_TLS char moo_thread_marke;

uint8_t moo_global_fremd(void) {
    return moo_ist_moo_worker;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((cold, noinline, noreturn))
#endif
void moo_global_fremdzugriff(const char* name) {
    fprintf(stderr,
            "moo: Laufzeitfehler: Ein Moo-Thread greift auf die Globale '%s' zu. "
            "Globale gehoeren dem Hauptthread.\n"
            "  Abhilfe: den Wert per Kanal senden oder als Argument von starte "
            "uebergeben, die Globale als 'geteilt' markieren (wird dann "
            "eingefroren) oder eine Sperre bzw. ein Atom verwenden.\n",
            name ? name : "?");
    fflush(NULL); /* bisherige Ausgaben nicht verlieren (_Exit leert nicht) */
    _Exit(1);
}

/* Vom Codegen vor jedem Globalzugriff in Code, der vielleicht auf einem
   Moo-Thread laeuft, ohne dass der Compiler das beweisen konnte
   (Funktionswerte, Methoden). Nur in Programmen mit starte; gilt in allen
   Builds (Determinismus vor Mikrokosten, ADR 2290622c). */
void moo_global_pruefen(const char* name) {
    if (MOO_RC_SELTEN(moo_ist_moo_worker)) moo_global_fremdzugriff(name);
}

/* P2-T119: starte(f, a, b, ...) -> f(a, b, ...). Die Liste haelt der
   Thread-Wert (halt_arg); jeder Parameter-Slot verbraucht eine eigene +1.
   Liste und Elemente sind geteilt (moo_teilen beim Start), die Retains also
   atomar. Die Argumentzahl ist beim Start geprueft (moo_func_aritaet_ok). */
static MooValue moo_thread_rufe_liste(MooValue func, MooValue liste) {
    MooList* l = MV_LIST(liste);
    MooValue* a = l->items;
    for (int32_t i = 0; i < l->length; i++) moo_retain(a[i]);
    switch (l->length) {
    case 0: return moo_func_call_0(func);
    case 1: return moo_func_call_1(func, a[0]);
    case 2: return moo_func_call_2(func, a[0], a[1]);
    case 3: return moo_func_call_3(func, a[0], a[1], a[2]);
    case 4: return moo_func_call_4(func, a[0], a[1], a[2], a[3]);
    case 5: return moo_func_call_5(func, a[0], a[1], a[2], a[3], a[4]);
    case 6: return moo_func_call_6(func, a[0], a[1], a[2], a[3], a[4], a[5]);
    case 7: return moo_func_call_7(func, a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
    default: return moo_func_call_8(func, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    }
}

static void* thread_runner_common(void* raw) {
    /* P2-T120: raw ist das Postfach des Workers, die Startdaten liegen vorn. */
    MooPostfach* post = (MooPostfach*)raw;
    MooThreadArgs* targs = &post->start;
    moo_mein_postfach = post;
    /* P2-T21/T57 D: fehlbare C-Rueckrufe sind auf Moo-Threads erlaubt. */
    moo_native_thread_markieren();
    moo_ist_moo_worker = 1;
    MooValue arg = targs->arg;
    MooValue result;
    if (targs->n_args >= 0) {
        MooValue func;
        func.tag = MOO_FUNC;
        moo_val_set_ptr(&func, targs->env);
        result = moo_thread_rufe_liste(func, arg);
    } else if (targs->env) {
        // Closure-Call-Konvention
        MooValue (*tramp)(MooFunc*, MooValue) =
            (MooValue (*)(MooFunc*, MooValue))targs->fn_ptr;
        result = tramp(targs->env, arg);
    } else {
        MooValue (*plain)(MooValue) =
            (MooValue (*)(MooValue))targs->fn_ptr;
        result = plain(arg);
    }
    // Closure-Environment und Extra-Retain des Arguments haelt der Thread-Wert
    // (halt_func/halt_arg); der Worker gibt sie nicht frei, damit kein zweiter
    // Thread gleichzeitig denselben (nicht atomaren) Refcount aendert.
    // P2-T120: Postfach schliessen: Abgegebenes selbst freigeben; was danach
    // kommt, uebernimmt der Joiner (post_uebernehmen).
    post_lock(post);
    MooAbgabe* rest = post->liste;
    post->liste = NULL;
    post->lebt = false;
    post_unlock(post);
    post_liste_freigeben(rest);
    moo_mein_postfach = NULL;
    post_release(post);

    MooValue* heap_result = (MooValue*)malloc(sizeof(MooValue));
    *heap_result = result;
    return (void*)heap_result;
}

#ifdef _WIN32
static DWORD WINAPI thread_runner_win(LPVOID raw) {
    MooThread* owner = ((MooPostfach*)raw)->start.owner;
    owner->retval = (MooValue*)thread_runner_common(raw);
    return 0;
}
#else
static void* thread_runner_posix(void* raw) {
    return thread_runner_common(raw);
}
#endif

static void moo_mutex_init_runtime(MooThread* t) {
#ifdef _WIN32
    InitializeCriticalSection(&t->mutex);
#else
    pthread_mutex_init(&t->mutex, NULL);
#endif
}

static void moo_mutex_lock_thread(MooThread* t) {
#ifdef _WIN32
    EnterCriticalSection(&t->mutex);
#else
    pthread_mutex_lock(&t->mutex);
#endif
}

static void moo_mutex_unlock_thread(MooThread* t) {
#ifdef _WIN32
    LeaveCriticalSection(&t->mutex);
#else
    pthread_mutex_unlock(&t->mutex);
#endif
}

// Laufzeitreferenzen eines Threads freigeben: nur nach dem Join und nur im
// Thread, der den Thread-Wert haelt; danach auf nichts gesetzt (einmalig).
static void moo_thread_halt_freigeben(MooThread* t) {
    moo_release(t->halt_func);
    moo_release(t->halt_arg);
    t->halt_func = moo_none();
    t->halt_arg = moo_none();
}

/* n_args < 0: starte(f, x) wie bisher (ein Argument, arg owning +1).
   n_args >= 0 (P2-T119): arg ist eine Liste mit n_args Werten (owning +1). */
static MooValue moo_thread_starten(MooValue func, MooValue arg, int32_t n_args) {
    // Strikte (native) Funktionswerte: Argumentzahl vor dem Start pruefen.
    if (func.tag == MOO_FUNC && !moo_func_aritaet_ok(MV_FUNC(func), n_args < 0 ? 1 : n_args)) {
        // Der Codegen uebergibt Funktion und Argument owning (+1); ohne
        // Thread gibt der Fehlerpfad beide hier frei.
        moo_release(func);
        moo_release(arg);
        return moo_none();
    }
    // P2-T112: Ab hier erreichen zwei Threads Funktion (Captures), Argument
    // und Globale. Markieren, solange dieser Thread sie noch allein haelt.
#if !defined(__GNUC__) && !defined(__clang__) && !defined(_MSC_VER)
    /* Fallback ohne Startkonstruktor: der erste starte kommt aus dem Hauptthread
       (Worker gibt es erst danach); C-Threads davor gelten als Besitzer. */
    {
        static int markiert = 0;
        if (!markiert) { markiert = 1; moo_ist_moo_worker = 0; }
    }
#endif
    moo_mt_aktivieren();
    moo_teilen(func);
    moo_teilen(arg);
    MooThread* t = (MooThread*)malloc(sizeof(MooThread));
    t->refcount = 1;
    t->done = false;
    t->wartender = NULL;
    /* P2-T120: eine Referenz haelt der Thread-Wert, eine der Worker. */
    MooPostfach* post = post_neu(2);
    t->worker_post = post;
    t->result.tag = MOO_NONE;
    t->result.data = 0;
    t->halt_func = moo_none();
    t->halt_arg = moo_none();
#ifdef _WIN32
    t->retval = NULL;
#endif
    moo_mutex_init_runtime(t);

    MooFunc* mf = MV_FUNC(func);
    MooThreadArgs* targs = &post->start;
    targs->fn_ptr = mf->fn_ptr;
    targs->env = (mf->n_captured > 0 || n_args >= 0) ? mf : NULL;
    targs->arg = arg;
    targs->n_args = n_args;
#ifdef _WIN32
    targs->owner = t;
#endif

    if (n_args >= 0) {
        // P2-T119: Funktionswert und Argumentliste haelt der Thread-Wert bis
        // nach dem Join (jeweils die +1 des Aufrufers); der Worker gibt nichts frei.
        t->halt_func = func;
        t->halt_arg = arg;
    } else {

    // Der Codegen uebergibt func owning (+1). Plain-Funktion: nur fn_ptr wird
    // gebraucht, der Funktionswert wird hier freigegeben. Closure: die +1 haelt
    // das Environment; der Thread-Wert gibt sie nach dem Join frei.
    if (targs->env) {
        t->halt_func = func;
    } else {
        moo_release(func);
    }
    // Arg muss ebenfalls bis Thread-Ende leben (die +1 des Aufrufers verbraucht
    // der Parameter-Slot der Funktion). Den Extra-Retain haelt der Thread-Wert.
    moo_retain(arg);
    t->halt_arg = arg;
    }

#ifdef _WIN32
    t->thread = CreateThread(NULL, 0, thread_runner_win, post, 0, &t->thread_id);
    int fehlgeschlagen = t->thread == NULL;
    /* Vor der Freigabe sichern: weitere Win32-Aufrufe koennen LastError setzen. */
    unsigned long systemfehler = fehlgeschlagen ? (unsigned long)GetLastError() : 0;
    const char* systemaufruf = "CreateThread";
#else
    /* pthread_create liefert den Fehlercode direkt; errno ist hier nicht der Vertrag. */
    int startcode = pthread_create(&t->thread, NULL, thread_runner_posix, post);
    int fehlgeschlagen = startcode != 0;
    unsigned long systemfehler = (unsigned long)startcode;
    const char* systemaufruf = "pthread_create";
#endif
    if (fehlgeschlagen) {
        /* Kein Worker hat Besitz uebernommen: Einargument-Parameterslot und
         * gehaltene Referenzen exakt abbauen. Im Mehrargumentpfad gehoert die
         * uebergebene Liste bereits halt_arg. Kein Join auf ungueltigem Handle. */
        if (n_args < 0) moo_release(arg);
        moo_thread_halt_freigeben(t);
#ifdef _WIN32
        DeleteCriticalSection(&t->mutex);
#else
        pthread_mutex_destroy(&t->mutex);
#endif
        /* Das noch unveroeffentlichte Postfach haelt Worker- und Thread-Ref. */
        post_release(post);
        post_release(post);
        free(t);
        char meldung[128];
        snprintf(meldung, sizeof(meldung), "Thread erstellen fehlgeschlagen (%s: %lu)",
                 systemaufruf, systemfehler);
        moo_throw(moo_string_new(meldung));
        return moo_none();
    }

    MooValue v;
    v.tag = MOO_THREAD;
    moo_val_set_ptr(&v, t);
    return v;
}

MooValue moo_thread_spawn(MooValue func, MooValue arg) {
    return moo_thread_starten(func, arg, -1);
}

/* P2-T119: starte(f, a, b, ...) mit 0 oder 2..8 Argumenten; der Codegen
   packt sie in eine Liste (owning +1), jedes Element gilt nach der
   Teilen-Regel (moo_teilen der Liste teilt sie tief). */
MooValue moo_thread_spawn_n(MooValue func, MooValue liste) {
    if (liste.tag != MOO_LIST || MV_LIST(liste)->length > 8) {
        moo_release(func);
        moo_release(liste);
        moo_throw(moo_string_new("starte: hoechstens 8 Argumente fuer die Funktion"));
        return moo_none();
    }
    return moo_thread_starten(func, liste, MV_LIST(liste)->length);
}

MooValue moo_thread_wait(MooValue thread) {
    // Kein Thread (z. B. starte nach einem Arityfehler ohne versuche:):
    // Moo-Fehler statt Zugriff auf fremden Speicher.
    if (thread.tag != MOO_THREAD) {
        moo_throw(moo_string_new("warten: der Wert ist kein Thread"));
        return moo_none();
    }
    MooThread* t = (MooThread*)moo_val_as_ptr(thread);
    // P2-T114 B (Entscheid): Warten mehrere Threads auf denselben Thread,
    // bekommt der erste Wartende das Ergebnis; jeder andere Thread erhaelt
    // einen fangbaren Fehler (auch nach Ende des Threads). Kein doppelter
    // Join, kein Rennen um das Ergebnis.
    // Zweites warten im selben Thread: nicht erneut joinen (UB), sondern das
    // gemerkte Ergebnis erneut liefern (P2-T119: es gehoert dem ersten
    // Wartenden, jedes warten bekommt eine eigene +1).
    // P2-T120: Kennung ist das eigene Postfach; vorher Abgegebenes freigeben.
    MooPostfach* ich = post_mein();
    post_leeren();
    moo_mutex_lock_thread(t);
    if (t->wartender == NULL) {
        MOO_POST_INC(&ich->refs);
        t->wartender = ich;
    } else if (t->wartender != (void*)ich) {
        moo_mutex_unlock_thread(t);
        moo_throw(moo_string_new("warten: auf diesen Thread wartet bereits ein anderer Thread; "
                                 "das Ergebnis gehoert dem ersten Wartenden. Ergebnis per Kanal weitergeben."));
        return moo_none();
    }
    if (t->done) {
        MooValue r = t->result;
        moo_retain(r);
        moo_mutex_unlock_thread(t);
        return r;
    }
    moo_mutex_unlock_thread(t);
    void* retval = NULL;
#ifdef _WIN32
    WaitForSingleObject(t->thread, INFINITE);
    retval = (void*)t->retval;
    t->retval = NULL;
    CloseHandle(t->thread);
    t->thread = NULL;
#else
    pthread_join(t->thread, &retval);
#endif
    moo_thread_halt_freigeben(t);
    post_uebernehmen((MooPostfach*)t->worker_post);

    moo_mutex_lock_thread(t);
    if (retval) {
        MooValue* res = (MooValue*)retval;
        t->result = *res;
        free(res);
    }
    t->done = true;
    MooValue r = t->result;
    // P2-T119 (Entscheid): t->result behaelt seine Referenz, damit jedes
    // weitere warten des ersten Wartenden dasselbe Ergebnis bekommt; der
    // Aufrufer erhaelt eine eigene +1 (INV-1). Ein ungeteiltes Ergebnis bleibt
    // ungeteilt und veraenderbar; seine Refcount-Aenderungen passieren nur im
    // Thread des ersten Wartenden (siehe moo_thread_free).
    moo_retain(r);
    moo_mutex_unlock_thread(t);

    return r;
}

MooValue moo_thread_done(MooValue thread) {
    if (thread.tag != MOO_THREAD) {
        moo_throw(moo_string_new("fertig: der Wert ist kein Thread"));
        return moo_none();
    }
    MooThread* t = (MooThread*)moo_val_as_ptr(thread);
    moo_mutex_lock_thread(t);
    bool d = t->done;
    moo_mutex_unlock_thread(t);
    return moo_bool(d);
}

// === Channel ===

static void moo_channel_lock(MooChannel* ch) {
#ifdef _WIN32
    EnterCriticalSection(&ch->mutex);
#else
    pthread_mutex_lock(&ch->mutex);
#endif
}

static void moo_channel_unlock(MooChannel* ch) {
#ifdef _WIN32
    LeaveCriticalSection(&ch->mutex);
#else
    pthread_mutex_unlock(&ch->mutex);
#endif
}

static void moo_channel_wait_not_full(MooChannel* ch) {
#ifdef _WIN32
    SleepConditionVariableCS(&ch->not_full, &ch->mutex, INFINITE);
#else
    pthread_cond_wait(&ch->not_full, &ch->mutex);
#endif
}

static void moo_channel_wait_not_empty(MooChannel* ch) {
#ifdef _WIN32
    SleepConditionVariableCS(&ch->not_empty, &ch->mutex, INFINITE);
#else
    pthread_cond_wait(&ch->not_empty, &ch->mutex);
#endif
}

static void moo_channel_signal_not_full(MooChannel* ch) {
#ifdef _WIN32
    WakeConditionVariable(&ch->not_full);
#else
    pthread_cond_signal(&ch->not_full);
#endif
}

static void moo_channel_signal_not_empty(MooChannel* ch) {
#ifdef _WIN32
    WakeConditionVariable(&ch->not_empty);
#else
    pthread_cond_signal(&ch->not_empty);
#endif
}

static void moo_channel_broadcast_not_full(MooChannel* ch) {
#ifdef _WIN32
    WakeAllConditionVariable(&ch->not_full);
#else
    pthread_cond_broadcast(&ch->not_full);
#endif
}

static void moo_channel_broadcast_not_empty(MooChannel* ch) {
#ifdef _WIN32
    WakeAllConditionVariable(&ch->not_empty);
#else
    pthread_cond_broadcast(&ch->not_empty);
#endif
}

MooValue moo_channel_new(MooValue capacity) {
    int32_t cap = 16;
    if (capacity.tag == MOO_NUMBER) {
        cap = (int32_t)MV_NUM(capacity);
        if (cap < 1) cap = 1;
    }

    MooChannel* ch = (MooChannel*)malloc(sizeof(MooChannel));
    ch->refcount = 1;
    ch->buffer = (MooValue*)malloc(sizeof(MooValue) * cap);
    ch->capacity = cap;
    ch->count = 0;
    ch->read_pos = 0;
    ch->write_pos = 0;
    ch->closed = false;
#ifdef _WIN32
    InitializeCriticalSection(&ch->mutex);
    InitializeConditionVariable(&ch->not_empty);
    InitializeConditionVariable(&ch->not_full);
#else
    pthread_mutex_init(&ch->mutex, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
#endif

    MooValue v;
    v.tag = MOO_CHANNEL;
    moo_val_set_ptr(&v, ch);
    return v;
}

void moo_channel_send(MooValue channel, MooValue value) {
    MooChannel* ch = (MooChannel*)moo_val_as_ptr(channel);
    // P2-T112: Der Empfaenger kann ein anderer Thread sein; der Sender darf
    // den Wert weiter halten. Vor der Uebergabe (Mutex) markieren.
    moo_teilen(value);
    moo_channel_lock(ch);

    while (ch->count == ch->capacity && !ch->closed) {
        moo_channel_wait_not_full(ch);
    }

    if (ch->closed) {
        moo_channel_unlock(ch);
        fprintf(stderr, "Fehler: Senden auf geschlossenen Kanal\n");
        return;
    }

    ch->buffer[ch->write_pos] = value;
    ch->write_pos = (ch->write_pos + 1) % ch->capacity;
    ch->count++;

    moo_channel_signal_not_empty(ch);
    moo_channel_unlock(ch);
}

MooValue moo_channel_recv(MooValue channel) {
    MooChannel* ch = (MooChannel*)moo_val_as_ptr(channel);
    moo_channel_lock(ch);

    while (ch->count == 0 && !ch->closed) {
        moo_channel_wait_not_empty(ch);
    }

    if (ch->count == 0 && ch->closed) {
        moo_channel_unlock(ch);
        return moo_none();
    }

    MooValue val = ch->buffer[ch->read_pos];
    ch->read_pos = (ch->read_pos + 1) % ch->capacity;
    ch->count--;

    moo_channel_signal_not_full(ch);
    moo_channel_unlock(ch);

    return val;
}

void moo_channel_close(MooValue channel) {
    MooChannel* ch = (MooChannel*)moo_val_as_ptr(channel);
    moo_channel_lock(ch);
    ch->closed = true;
    moo_channel_broadcast_not_empty(ch);
    moo_channel_broadcast_not_full(ch);
    moo_channel_unlock(ch);
}

void moo_thread_free(void* ptr) {
    if (!ptr) return;
    MooThread* t = (MooThread*)ptr;
#ifdef _WIN32
    if (t->thread) {
        if (!t->done) {
            WaitForSingleObject(t->thread, INFINITE);
        }
        CloseHandle(t->thread);
    }
    if (t->retval) {
        MooValue* res = (MooValue*)t->retval;
        moo_release(*res);
        free(res);
    }
    DeleteCriticalSection(&t->mutex);
#else
    if (!t->done) {
        void* retval = NULL;
        pthread_join(t->thread, &retval);
        if (retval) {
            MooValue* res = (MooValue*)retval;
            moo_release(*res);
            free(res);
        }
    }
    pthread_mutex_destroy(&t->mutex);
#endif
    moo_thread_halt_freigeben(t);
    // P2-T120: Dieser Thread hat (falls noch noetig) gejoint und erbt damit,
    // was der Worker hielt; vorher an ihn Abgegebenes freigeben.
    post_uebernehmen((MooPostfach*)t->worker_post);
    post_leeren();
    // Ein ungeteiltes Ergebnis gehoert dem ersten Wartenden (P2-T119): nur
    // geteilt (atomar) oder ohne Wartenden hier freigeben, sonst an seinen
    // Besitzer uebergeben (post_abgeben; bin ich es selbst, direkt).
    MooPostfach* w = (MooPostfach*)t->wartender;
    if (w == NULL || moo_ist_geteilt(t->result)) {
        moo_release(t->result);
    } else {
        post_abgeben(w, t->result);
    }
    post_release(w);
    post_release((MooPostfach*)t->worker_post);
    free(t);
}

/* === P2-T114 Phase B: Atom, Sperre, Uebergabe ===
 * Atom: atomare Zahl (Bits eines double), seq_cst. Darf geteilt sein, ohne
 * eingefroren zu werden: jede Operation ist atomar.
 * Sperre: besitzt ihren Inhalt; nur innerhalb von `mit s als k:` erreichbar
 * (Codegen: moo_sperre_betreten/verlassen). moo_teilen markiert die Sperre,
 * nicht ihren Inhalt: der Inhalt bleibt veraenderbar und wird nur unter dem
 * Mutex angefasst (der Mutex ordnet die nicht atomaren Zaehler). */

typedef struct MooAtom {
    int32_t refcount; /* erstes Feld (moo_retain/moo_release) */
    int32_t reserve;
    uint64_t bits;
} MooAtom;

typedef struct MooSperre {
    int32_t refcount; /* erstes Feld */
    MooValue inhalt;
    void* besitzer;   /* Thread-Marke des Halters, NULL = frei */
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
} MooSperre;

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static uint64_t atom_lade(uint64_t* p) { return (uint64_t)_InterlockedCompareExchange64((volatile long long*)p, 0, 0); }
static void atom_speichere(uint64_t* p, uint64_t v) { _InterlockedExchange64((volatile long long*)p, (long long)v); }
static uint64_t atom_tausche(uint64_t* p, uint64_t v) { return (uint64_t)_InterlockedExchange64((volatile long long*)p, (long long)v); }
static int atom_cas(uint64_t* p, uint64_t* erwartet, uint64_t neu) {
    uint64_t alt = (uint64_t)_InterlockedCompareExchange64((volatile long long*)p, (long long)neu, (long long)*erwartet);
    if (alt == *erwartet) return 1;
    *erwartet = alt;
    return 0;
}
static void* marke_lade(void** p) { return _InterlockedCompareExchangePointer(p, NULL, NULL); }
static void marke_setze(void** p, void* v) { _InterlockedExchangePointer(p, v); }
#else
static uint64_t atom_lade(uint64_t* p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static void atom_speichere(uint64_t* p, uint64_t v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }
static uint64_t atom_tausche(uint64_t* p, uint64_t v) { return __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST); }
static int atom_cas(uint64_t* p, uint64_t* erwartet, uint64_t neu) {
    return __atomic_compare_exchange_n(p, erwartet, neu, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static void* marke_lade(void** p) { return __atomic_load_n(p, __ATOMIC_RELAXED); }
static void marke_setze(void** p, void* v) { __atomic_store_n(p, v, __ATOMIC_RELAXED); }
#endif

static uint64_t zahl_bits(double d) { uint64_t b; memcpy(&b, &d, sizeof b); return b; }
static double bits_zahl(uint64_t b) { double d; memcpy(&d, &b, sizeof d); return d; }

static MooAtom* atom_von(MooValue a, const char* wer) {
    if (a.tag != MOO_ATOM || !moo_val_as_ptr(a)) {
        char text[160];
        snprintf(text, sizeof(text), "%s erwartet ein Atom, erhalten: %s", wer, moo_type_name(a));
        moo_throw(moo_string_new(text));
        return NULL;
    }
    return (MooAtom*)moo_val_as_ptr(a);
}

static int zahl_von(MooValue w, const char* wer, double* out) {
    if (w.tag != MOO_NUMBER) {
        char text[160];
        snprintf(text, sizeof(text), "%s erwartet eine Zahl, erhalten: %s", wer, moo_type_name(w));
        moo_throw(moo_string_new(text));
        return 0;
    }
    *out = MV_NUM(w);
    return 1;
}

MooValue moo_atom_neu(MooValue start) {
    double d;
    if (!zahl_von(start, "atom", &d)) return moo_none();
    MooAtom* a = (MooAtom*)moo_alloc(sizeof(MooAtom));
    a->refcount = 1;
    a->reserve = 0;
    a->bits = zahl_bits(d);
    MooValue v;
    v.tag = MOO_ATOM;
    moo_val_set_ptr(&v, a);
    return v;
}

MooValue moo_atom_laden(MooValue atom) {
    MooAtom* a = atom_von(atom, "atom_laden");
    if (!a) return moo_none();
    return moo_number(bits_zahl(atom_lade(&a->bits)));
}

MooValue moo_atom_speichern(MooValue atom, MooValue wert) {
    MooAtom* a = atom_von(atom, "atom_speichern");
    double d;
    if (!a || !zahl_von(wert, "atom_speichern", &d)) return moo_none();
    atom_speichere(&a->bits, zahl_bits(d));
    return moo_none();
}

/* Liefert den alten Wert (wie atomicrmw add in @nativ). */
MooValue moo_atom_addieren(MooValue atom, MooValue wert) {
    MooAtom* a = atom_von(atom, "atom_addieren");
    double d;
    if (!a || !zahl_von(wert, "atom_addieren", &d)) return moo_none();
    uint64_t alt = atom_lade(&a->bits);
    while (!atom_cas(&a->bits, &alt, zahl_bits(bits_zahl(alt) + d))) {
        /* alt wurde von atom_cas aktualisiert */
    }
    return moo_number(bits_zahl(alt));
}

MooValue moo_atom_tauschen(MooValue atom, MooValue wert) {
    MooAtom* a = atom_von(atom, "atom_tauschen");
    double d;
    if (!a || !zahl_von(wert, "atom_tauschen", &d)) return moo_none();
    return moo_number(bits_zahl(atom_tausche(&a->bits, zahl_bits(d))));
}

/* Vergleich nach Zahlwert (0 == -0); liefert wahr, wenn getauscht wurde. */
MooValue moo_atom_vergleichen_und_tauschen(MooValue atom, MooValue erwartet, MooValue neu) {
    MooAtom* a = atom_von(atom, "atom_vergleichen_und_tauschen");
    double e, n;
    if (!a || !zahl_von(erwartet, "atom_vergleichen_und_tauschen", &e)
        || !zahl_von(neu, "atom_vergleichen_und_tauschen", &n)) return moo_none();
    uint64_t alt = atom_lade(&a->bits);
    for (;;) {
        if (bits_zahl(alt) != e) return moo_bool(false);
        if (atom_cas(&a->bits, &alt, zahl_bits(n))) return moo_bool(true);
    }
}

void moo_atom_free(void* ptr) {
    free(ptr);
}

static MooSperre* sperre_von(MooValue s, const char* wer) {
    if (s.tag != MOO_SPERRE || !moo_val_as_ptr(s)) {
        char text[160];
        snprintf(text, sizeof(text), "%s erwartet eine Sperre, erhalten: %s", wer, moo_type_name(s));
        moo_throw(moo_string_new(text));
        return NULL;
    }
    return (MooSperre*)moo_val_as_ptr(s);
}

/* Referenzen eines Heap-Werts (geteilt: Betrag); 0 fuer Werttypen/unsterblich. */
static int32_t referenzen(MooValue v) {
    switch (v.tag) {
        case MOO_NUMBER: case MOO_BOOL: case MOO_NONE: return 0;
        default: break;
    }
    if (!moo_val_as_ptr(v) || !moo_ist_heap_wert(v)) return 0;
    int32_t r = moo_rc_lesen((int32_t*)moo_val_as_ptr(v));
    return r < 0 ? -r : r;
}

MooValue moo_sperre_neu(MooValue wert) {
    /* wert kommt owning (+1). Weitere Referenzen hiessen: jemand kann den
       Inhalt ohne Sperre veraendern. */
    if (referenzen(wert) > 1) {
        moo_release(wert);
        moo_throw(moo_string_new("sperre: der Wert ist noch an anderer Stelle referenziert und "
                                 "koennte ohne Sperre veraendert werden. Den Inhalt direkt in der "
                                 "Sperre anlegen, z. B. sperre({\"stand\": 0})."));
        return moo_none();
    }
    moo_sperre_unterelemente_sichern(wert);
    moo_sperre_freigabe_haken = moo_sperre_free;
    MooSperre* s = (MooSperre*)moo_alloc(sizeof(MooSperre));
    s->refcount = 1;
    s->inhalt = wert;
    s->besitzer = NULL;
#ifdef _WIN32
    InitializeCriticalSection(&s->mutex);
#else
    pthread_mutex_init(&s->mutex, NULL);
#endif
    MooValue v;
    v.tag = MOO_SPERRE;
    moo_val_set_ptr(&v, s);
    return v;
}

MooValue moo_sperre_betreten(MooValue sperre) {
    MooSperre* s = sperre_von(sperre, "mit ... als");
    if (!s) return moo_none();
    void* ich = (void*)&moo_thread_marke;
    if (marke_lade(&s->besitzer) == ich) {
        /* Nur dieser Thread kann die Marke auf sich gesetzt haben. */
        moo_throw(moo_string_new("mit ... als: diese Sperre haelt der Thread bereits "
                                 "(verschachteltes mit auf dieselbe Sperre wuerde sich selbst blockieren)"));
        return moo_none();
    }
#ifdef _WIN32
    EnterCriticalSection(&s->mutex);
#else
    pthread_mutex_lock(&s->mutex);
#endif
    marke_setze(&s->besitzer, ich);
    moo_retain(s->inhalt);
    return s->inhalt;
}

MooValue moo_sperre_verlassen(MooValue sperre, MooValue pruefen) {
    if (sperre.tag != MOO_SPERRE || !moo_val_as_ptr(sperre)) return moo_none();
    MooSperre* s = (MooSperre*)moo_val_as_ptr(sperre);
    if (marke_lade(&s->besitzer) != (void*)&moo_thread_marke) return moo_none();
    int entkommen = pruefen.tag == MOO_BOOL && MV_BOOL(pruefen) && referenzen(s->inhalt) > 1;
    /* Auch im Fehlerpfad vor dem Entsperren: ein Alias bleibt nur lesbar. */
    moo_sperre_unterelemente_sichern(s->inhalt);
    marke_setze(&s->besitzer, NULL);
#ifdef _WIN32
    LeaveCriticalSection(&s->mutex);
#else
    pthread_mutex_unlock(&s->mutex);
#endif
    if (entkommen) {
        moo_throw(moo_string_new("mit ... als: der Inhalt der Sperre wird nach dem Block noch "
                                 "ausserhalb referenziert (entkommen). Werte aus dem Block per "
                                 "Kopie herausgeben, nicht den Inhalt selbst speichern."));
    }
    return moo_none();
}

void moo_sperre_free(void* ptr) {
    if (!ptr) return;
    MooSperre* s = (MooSperre*)ptr;
    moo_release(s->inhalt);
#ifdef _WIN32
    DeleteCriticalSection(&s->mutex);
#else
    pthread_mutex_destroy(&s->mutex);
#endif
    free(s);
}

MooValue moo_channel_uebergeben(MooValue channel, MooValue wert) {
    if (channel.tag != MOO_CHANNEL || !moo_val_as_ptr(channel)) {
        moo_throw(moo_string_new("senden(übergib ...): der Empfaenger ist kein Kanal"));
        return wert;
    }
    if (wert.tag != MOO_BUFFER) {
        moo_throw(moo_string_new("übergib: nur Puffer koennen ohne Kopie uebergeben werden"));
        return wert;
    }
    if (!moo_buffer_alleinbesitz(wert)) {
        moo_throw(moo_string_new("übergib: der Puffer ist nicht im Alleinbesitz (weitere Referenz, "
                                 "Sicht, Pin oder geteilt); er bleibt beim Sender"));
        return wert;
    }
    MooChannel* ch = (MooChannel*)moo_val_as_ptr(channel);
    moo_channel_lock(ch);
    while (ch->count == ch->capacity && !ch->closed) {
        moo_channel_wait_not_full(ch);
    }
    if (ch->closed) {
        moo_channel_unlock(ch);
        moo_throw(moo_string_new("senden(übergib ...): Kanal ist geschlossen"));
        return wert;
    }
    /* Ohne moo_teilen: der Empfaenger erhaelt die einzige Referenz, der
       Kanal-Mutex ordnet die Zugriffe des Senders vor die des Empfaengers. */
    ch->buffer[ch->write_pos] = wert;
    ch->write_pos = (ch->write_pos + 1) % ch->capacity;
    ch->count++;
    moo_channel_signal_not_empty(ch);
    moo_channel_unlock(ch);
    return moo_none();
}

void moo_channel_free(void* ptr) {
    if (!ptr) return;
    MooChannel* ch = (MooChannel*)ptr;
    if (ch->buffer) {
        while (ch->count > 0) {
            MooValue val = ch->buffer[ch->read_pos];
            moo_release(val);
            ch->read_pos = (ch->read_pos + 1) % ch->capacity;
            ch->count--;
        }
        free(ch->buffer);
    }
#ifdef _WIN32
    DeleteCriticalSection(&ch->mutex);
#else
    pthread_mutex_destroy(&ch->mutex);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
#endif
    free(ch);
}

#include "moo_runtime.h"
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
#ifdef _WIN32
    MooThread* owner;   // Windows: Ergebnis direkt im Thread-Objekt speichern
#endif
} MooThreadArgs;

#ifdef _WIN32
static DWORD WINAPI thread_runner_win(LPVOID raw);
#else
static void* thread_runner_posix(void* raw);
#endif

static void* thread_runner_common(void* raw) {
    MooThreadArgs* targs = (MooThreadArgs*)raw;
    MooValue arg = targs->arg;
    MooValue result;
    if (targs->env) {
        // Closure-Call-Konvention
        MooValue (*tramp)(MooFunc*, MooValue) =
            (MooValue (*)(MooFunc*, MooValue))targs->fn_ptr;
        result = tramp(targs->env, arg);
    } else {
        MooValue (*plain)(MooValue) =
            (MooValue (*)(MooValue))targs->fn_ptr;
        result = plain(arg);
    }
    free(targs);
    // Arg-Retain aus moo_thread_spawn freigeben. Die Funktion hat mit
    // arg als Parameter gearbeitet (Function-Exit-Cleanup released den
    // Parameter-Slot) — der zusaetzliche retain hier haelt die Struktur
    // fuer den gesamten Thread-Lauf lebendig.
    moo_release(arg);

    MooValue* heap_result = (MooValue*)malloc(sizeof(MooValue));
    *heap_result = result;
    return (void*)heap_result;
}

#ifdef _WIN32
static DWORD WINAPI thread_runner_win(LPVOID raw) {
    MooThreadArgs* targs = (MooThreadArgs*)raw;
    MooThread* owner = targs->owner;
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

MooValue moo_thread_spawn(MooValue func, MooValue arg) {
    MooThread* t = (MooThread*)malloc(sizeof(MooThread));
    t->done = false;
    t->result.tag = MOO_NONE;
    t->result.data = 0;
#ifdef _WIN32
    t->retval = NULL;
#endif
    moo_mutex_init_runtime(t);

    MooFunc* mf = MV_FUNC(func);
    MooThreadArgs* targs = (MooThreadArgs*)malloc(sizeof(MooThreadArgs));
    targs->fn_ptr = mf->fn_ptr;
    targs->env = (mf->n_captured > 0) ? mf : NULL;
    targs->arg = arg;
#ifdef _WIN32
    targs->owner = t;
#endif

    // Closure-Environment am Leben halten bis Thread fertig ist.
    if (targs->env) moo_retain(func);
    // Arg muss ebenfalls bis Thread-Ende leben. Ohne diesen retain
    // wuerde der Caller den arg-Temp beim Return freigeben (Function-
    // Exit-Cleanup), der Thread griffe auf freed memory zu.
    moo_retain(arg);

#ifdef _WIN32
    t->thread = CreateThread(NULL, 0, thread_runner_win, targs, 0, &t->thread_id);
    if (!t->thread) {
        moo_release(arg);
        if (targs->env) moo_release(func);
        free(targs);
        free(t);
        moo_throw(moo_string_new("Thread erstellen fehlgeschlagen"));
        return moo_none();
    }
#else
    pthread_create(&t->thread, NULL, thread_runner_posix, targs);
#endif

    MooValue v;
    v.tag = MOO_THREAD;
    moo_val_set_ptr(&v, t);
    return v;
}

MooValue moo_thread_wait(MooValue thread) {
    MooThread* t = (MooThread*)moo_val_as_ptr(thread);
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

    moo_mutex_lock_thread(t);
    if (retval) {
        MooValue* res = (MooValue*)retval;
        t->result = *res;
        free(res);
    }
    t->done = true;
    moo_mutex_unlock_thread(t);

    return t->result;
}

MooValue moo_thread_done(MooValue thread) {
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

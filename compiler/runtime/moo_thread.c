#include "moo_runtime.h"
#include <pthread.h>

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
} MooThreadArgs;

static void* thread_runner(void* raw) {
    MooThreadArgs* targs = (MooThreadArgs*)raw;
    MooValue result;
    if (targs->env) {
        // Closure-Call-Konvention
        MooValue (*tramp)(MooFunc*, MooValue) =
            (MooValue (*)(MooFunc*, MooValue))targs->fn_ptr;
        result = tramp(targs->env, targs->arg);
    } else {
        MooValue (*plain)(MooValue) =
            (MooValue (*)(MooValue))targs->fn_ptr;
        result = plain(targs->arg);
    }
    free(targs);

    MooValue* heap_result = (MooValue*)malloc(sizeof(MooValue));
    *heap_result = result;
    return (void*)heap_result;
}

MooValue moo_thread_spawn(MooValue func, MooValue arg) {
    MooThread* t = (MooThread*)malloc(sizeof(MooThread));
    t->done = false;
    t->result.tag = MOO_NONE;
    t->result.data = 0;
    pthread_mutex_init(&t->mutex, NULL);

    MooFunc* mf = MV_FUNC(func);
    MooThreadArgs* targs = (MooThreadArgs*)malloc(sizeof(MooThreadArgs));
    targs->fn_ptr = mf->fn_ptr;
    targs->env = (mf->n_captured > 0) ? mf : NULL;
    targs->arg = arg;

    // Closure-Environment am Leben halten bis Thread fertig ist.
    if (targs->env) moo_retain(func);

    pthread_create(&t->thread, NULL, thread_runner, targs);

    MooValue v;
    v.tag = MOO_THREAD;
    moo_val_set_ptr(&v, t);
    return v;
}

MooValue moo_thread_wait(MooValue thread) {
    MooThread* t = (MooThread*)moo_val_as_ptr(thread);
    void* retval = NULL;
    pthread_join(t->thread, &retval);

    pthread_mutex_lock(&t->mutex);
    if (retval) {
        MooValue* res = (MooValue*)retval;
        t->result = *res;
        free(res);
    }
    t->done = true;
    pthread_mutex_unlock(&t->mutex);

    return t->result;
}

MooValue moo_thread_done(MooValue thread) {
    MooThread* t = (MooThread*)moo_val_as_ptr(thread);
    pthread_mutex_lock(&t->mutex);
    bool d = t->done;
    pthread_mutex_unlock(&t->mutex);
    return moo_bool(d);
}

// === Channel ===

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
    pthread_mutex_init(&ch->mutex, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);

    MooValue v;
    v.tag = MOO_CHANNEL;
    moo_val_set_ptr(&v, ch);
    return v;
}

void moo_channel_send(MooValue channel, MooValue value) {
    MooChannel* ch = (MooChannel*)moo_val_as_ptr(channel);
    pthread_mutex_lock(&ch->mutex);

    while (ch->count == ch->capacity && !ch->closed) {
        pthread_cond_wait(&ch->not_full, &ch->mutex);
    }

    if (ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        fprintf(stderr, "Fehler: Senden auf geschlossenen Kanal\n");
        return;
    }

    ch->buffer[ch->write_pos] = value;
    ch->write_pos = (ch->write_pos + 1) % ch->capacity;
    ch->count++;

    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
}

MooValue moo_channel_recv(MooValue channel) {
    MooChannel* ch = (MooChannel*)moo_val_as_ptr(channel);
    pthread_mutex_lock(&ch->mutex);

    while (ch->count == 0 && !ch->closed) {
        pthread_cond_wait(&ch->not_empty, &ch->mutex);
    }

    if (ch->count == 0 && ch->closed) {
        pthread_mutex_unlock(&ch->mutex);
        return moo_none();
    }

    MooValue val = ch->buffer[ch->read_pos];
    ch->read_pos = (ch->read_pos + 1) % ch->capacity;
    ch->count--;

    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);

    return val;
}

void moo_channel_close(MooValue channel) {
    MooChannel* ch = (MooChannel*)moo_val_as_ptr(channel);
    pthread_mutex_lock(&ch->mutex);
    ch->closed = true;
    pthread_cond_broadcast(&ch->not_empty);
    pthread_cond_broadcast(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
}

/* RC-Vertrag und Besitzerkennung vor dem ersten Moo-Thread pruefen. */
#include "moo_runtime.h"
#include "moo_rc.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

uint8_t moo_global_fremd(void);
static int32_t geteilt = -1;

static unsigned worker_pruefen(void) {
    if (moo_global_fremd() != 1) return 1;
    for (unsigned i = 0; i < 200000; ++i) {
        moo_rc_inc(&geteilt);
        if (moo_rc_dec(&geteilt)) return 2;
    }
    return 0;
}

#ifdef _WIN32
static DWORD WINAPI worker(void* p) { (void)p; return worker_pruefen(); }
#else
static void* worker(void* p) { (void)p; return (void*)(uintptr_t)worker_pruefen(); }
#endif

int main(void) {
    /* Noch kein moo_thread_spawn: der Besitzer muss bereits feststehen. */
    if (moo_global_fremd() != 0) return 10;
    int32_t lokal = 1, unsterblich = 0;
    moo_rc_inc(&lokal);
    if (moo_rc_lesen(&lokal) != 2 || moo_rc_dec(&lokal)) return 11;
    if (!moo_rc_dec(&lokal) || moo_rc_lesen(&lokal) != 0) return 12;
    moo_rc_inc(&unsterblich);
    if (moo_rc_lesen(&unsterblich) != 0 || moo_rc_dec(&unsterblich)) return 13;
    size_t sichten = 1;
    moo_rc_size_inc(&sichten);
    if (moo_rc_size_dec(&sichten) != 1 || moo_rc_size_dec(&sichten) != 0) return 14;

#ifdef _WIN32
    HANDLE threads[4];
    for (unsigned i = 0; i < 4; ++i) {
        threads[i] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (!threads[i]) return 20;
    }
    for (unsigned i = 0; i < 4; ++i) {
        DWORD code = 99;
        if (WaitForSingleObject(threads[i], INFINITE) != WAIT_OBJECT_0 ||
            !GetExitCodeThread(threads[i], &code) || code != 0) return 21;
        CloseHandle(threads[i]);
    }
#else
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; ++i)
        if (pthread_create(&threads[i], NULL, worker, NULL) != 0) return 20;
    for (unsigned i = 0; i < 4; ++i) {
        void* code = NULL;
        if (pthread_join(threads[i], &code) != 0 || (uintptr_t)code != 0) return 21;
    }
#endif
    if (moo_global_fremd() != 0 || moo_rc_lesen(&geteilt) != -1) return 30;
    if (!moo_rc_dec(&geteilt) || moo_rc_lesen(&geteilt) != 0) return 31;
    puts("MOO_RC_THREAD_KENNUNG_PASS");
    return 0;
}

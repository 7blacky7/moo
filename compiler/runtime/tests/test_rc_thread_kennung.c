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

/* Wie die Capture-Harnesses: nur die benutzten Komponenten verknuepfen.
 * MSVC verlangt auch Anbieter fuer Referenzen aus unbenutzten Funktionen.
 * Diese Adapter duerfen nie erreicht werden und liefern KEINE Ersatzwerte.
 * RC-Inlines und moo_global_fremd kommen ausschliesslich aus Produktcode. */
#if defined(_MSC_VER) || defined(MOO_RC_KENNUNG_LINK_STUBS)
#include <stdlib.h>
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable:4100)
#define MOO_RC_TEST_NORETURN __declspec(noreturn)
#else
#define MOO_RC_TEST_NORETURN __attribute__((noreturn))
#endif
static MOO_RC_TEST_NORETURN void moo_rc_test_fremdpfad(const char* name) {
    fprintf(stderr, "FATAL: unbenutzter Runtime-Pfad im RC-Vertragstest: %s\n", name);
    abort();
}
#define MOO_RC_TEST_DEP(typ, name, params) \
    typ name params { moo_rc_test_fremdpfad(#name); }
MOO_RC_TEST_DEP(void*, moo_alloc, (size_t size))
MOO_RC_TEST_DEP(MooValue, moo_bool, (bool v))
MOO_RC_TEST_DEP(int, moo_buffer_alleinbesitz, (MooValue value))
MOO_RC_TEST_DEP(bool, moo_func_aritaet_ok, (MooFunc* fn, int32_t n))
MOO_RC_TEST_DEP(bool, moo_ist_geteilt, (MooValue v))
MOO_RC_TEST_DEP(bool, moo_ist_heap_wert, (MooValue v))
MOO_RC_TEST_DEP(void, moo_mt_aktivieren, (void))
MOO_RC_TEST_DEP(void, moo_native_thread_markieren, (void))
MOO_RC_TEST_DEP(MooValue, moo_none, (void))
MOO_RC_TEST_DEP(MooValue, moo_number, (double v))
MOO_RC_TEST_DEP(void, moo_release, (MooValue v))
MOO_RC_TEST_DEP(void, moo_retain, (MooValue v))
MOO_RC_TEST_DEP(MooValue, moo_string_new, (const char* s))
MOO_RC_TEST_DEP(void, moo_teilen, (MooValue v))
MOO_RC_TEST_DEP(void, moo_sperre_unterelemente_sichern, (MooValue v))
MOO_RC_TEST_DEP(void, moo_throw, (MooValue error))
MOO_RC_TEST_DEP(const char*, moo_type_name, (MooValue v))
MOO_RC_TEST_DEP(MooValue, moo_func_call_0, (MooValue func))
MOO_RC_TEST_DEP(MooValue, moo_func_call_1, (MooValue func, MooValue a0))
MOO_RC_TEST_DEP(MooValue, moo_func_call_2, (MooValue func, MooValue a0, MooValue a1))
MOO_RC_TEST_DEP(MooValue, moo_func_call_3, (MooValue func, MooValue a0, MooValue a1, MooValue a2))
MOO_RC_TEST_DEP(MooValue, moo_func_call_4, (MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3))
MOO_RC_TEST_DEP(MooValue, moo_func_call_5, (MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4))
MOO_RC_TEST_DEP(MooValue, moo_func_call_6, (MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4, MooValue a5))
MOO_RC_TEST_DEP(MooValue, moo_func_call_7, (MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4, MooValue a5, MooValue a6))
MOO_RC_TEST_DEP(MooValue, moo_func_call_8, (MooValue func, MooValue a0, MooValue a1, MooValue a2, MooValue a3, MooValue a4, MooValue a5, MooValue a6, MooValue a7))
void (*moo_sperre_freigabe_haken)(void*) = NULL;
#undef MOO_RC_TEST_DEP
#undef MOO_RC_TEST_NORETURN
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#endif


#define MOO_THREAD_START_HOOK_IMPLEMENTATION
#include "thread_start_fehler_hook.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

/* Die Fixture startet ausschliesslich im Hauptthread. Zaehlerzugriffe sind
 * vor dem Start/nach dem Join; die Worker erzeugen keine weiteren Threads. */
static int erzwungen = 1;
static int32_t aufrufe = 0;
static int32_t offene_mutexe = 0;
void moo_test_thread_start_modus(int32_t wert) { erzwungen = wert != 0; }
int32_t moo_test_thread_start_anzahl(void) { return aufrufe; }
int32_t moo_test_thread_start_mutexe(void) { return offene_mutexe; }
static void zaehle_start(void) {
    if (aufrufe == INT32_MAX) abort();
    aufrufe++;
}
static void zaehle_mutex(void) {
    if (offene_mutexe == INT32_MAX) abort();
    offene_mutexe++;
}
static void freier_mutex(void) {
    if (offene_mutexe <= 0) abort();
    offene_mutexe--;
}
#ifdef _WIN32
int32_t moo_test_thread_start_fehlercode(void) { return ERROR_NOT_ENOUGH_MEMORY; }
HANDLE WINAPI moo_test_thread_create(LPSECURITY_ATTRIBUTES a, SIZE_T s,
    LPTHREAD_START_ROUTINE f, LPVOID p, DWORD flags, LPDWORD id) {
    zaehle_start();
    if (erzwungen) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    return CreateThread(a, s, f, p, flags, id);
}
void WINAPI moo_test_mutex_init(LPCRITICAL_SECTION m) {
    InitializeCriticalSection(m);
    zaehle_mutex();
}
void WINAPI moo_test_mutex_destroy(LPCRITICAL_SECTION m) {
    DeleteCriticalSection(m);
    freier_mutex();
    /* Prueft, dass der CreateThread-Fehler VOR der Freigabe gesichert wurde. */
    SetLastError(ERROR_INVALID_HANDLE);
}
#else
int32_t moo_test_thread_start_fehlercode(void) { return EAGAIN; }
int moo_test_thread_create(pthread_t* t, const pthread_attr_t* a,
    void* (*f)(void*), void* p) {
    zaehle_start();
    if (erzwungen) {
        errno = EPERM; /* pthread_create liefert seinen Fehler selbst. */
        return EAGAIN;
    }
    return pthread_create(t, a, f, p);
}
int moo_test_mutex_init(pthread_mutex_t* m, const pthread_mutexattr_t* a) {
    int rc = pthread_mutex_init(m, a);
    if (!rc) zaehle_mutex();
    return rc;
}
int moo_test_mutex_destroy(pthread_mutex_t* m) {
    int rc = pthread_mutex_destroy(m);
    if (!rc) freier_mutex();
    errno = EINVAL;
    return rc;
}
#endif

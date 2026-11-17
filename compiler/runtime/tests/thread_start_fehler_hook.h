#ifndef MOO_THREAD_START_FEHLER_HOOK_H
#define MOO_THREAD_START_FEHLER_HOOK_H
/* Nur per -include fuer das gesonderte Test-Threadobjekt; keine Produkt-Hooks. */
#include <stdint.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
HANDLE WINAPI moo_test_thread_create(LPSECURITY_ATTRIBUTES, SIZE_T,
    LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
void WINAPI moo_test_mutex_init(LPCRITICAL_SECTION);
void WINAPI moo_test_mutex_destroy(LPCRITICAL_SECTION);
#ifndef MOO_THREAD_START_HOOK_IMPLEMENTATION
#define CreateThread moo_test_thread_create
#define InitializeCriticalSection moo_test_mutex_init
#define DeleteCriticalSection moo_test_mutex_destroy
#endif
#else
#include <pthread.h>
int moo_test_thread_create(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
int moo_test_mutex_init(pthread_mutex_t*, const pthread_mutexattr_t*);
int moo_test_mutex_destroy(pthread_mutex_t*);
#ifndef MOO_THREAD_START_HOOK_IMPLEMENTATION
#define pthread_create moo_test_thread_create
#define pthread_mutex_init moo_test_mutex_init
#define pthread_mutex_destroy moo_test_mutex_destroy
#endif
#endif
#endif

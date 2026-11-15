/* P2-T21/T57 Batch E (Plan f4655d20 E-1): C-Gegenstelle fuer Moo-@rückruf-Thunks.
 * Anders als skripte/p2/abi/callback_reference.c (borrowed, synchron, nie
 * gespeichert) prueft diese Datei auch gespeicherte Rueckrufe, Reentranz
 * (Rueckruf -> C -> Rueckruf), Strukturwerte und Aufrufe von einem fremden,
 * nicht von Moo gestarteten Thread. user_data ist fuer C opak (void*). */
#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef struct { int32_t a; int64_t b; } RrPaar;
typedef int32_t (*RrBesuch)(int32_t, void *);
typedef void (*RrMelde)(int32_t, void *);
typedef RrPaar (*RrTausch)(RrPaar);

int32_t rr_wiederhole(int32_t n, RrBesuch cb, void *ud);
void rr_melde(int32_t n, RrMelde cb, void *ud);
int32_t rr_tiefer(int32_t tiefe, RrBesuch cb, void *ud);
void rr_merke(RrBesuch cb, void *ud);
int32_t rr_loese_aus(int32_t x);
int64_t rr_tausche(RrTausch cb, int32_t a, int64_t b);
int32_t rr_auf_thread(RrBesuch cb, void *ud, int32_t x);

/* Ruft cb(i, ud) fuer i = 0..n-1 und summiert die Ergebnisse. Laeuft immer
 * bis zum Ende: ein Moo-Fehler darf C nicht abbrechen (kein Unwinding). */
int32_t rr_wiederhole(int32_t n, RrBesuch cb, void *ud) {
    int64_t summe = 0;
    for (int32_t i = 0; i < n; i++) summe += cb(i, ud);
    return (int32_t)summe;
}

void rr_melde(int32_t n, RrMelde cb, void *ud) {
    for (int32_t i = 0; i < n; i++) cb(i, ud);
}

/* Reentranz: der Moo-Rueckruf darf rr_tiefer erneut aufrufen. */
int32_t rr_tiefer(int32_t tiefe, RrBesuch cb, void *ud) {
    return cb(tiefe, ud) + 1;
}

/* Asynchron: Zeiger und user_data werden gespeichert und spaeter (in einem
 * anderen Moo->C-Aufruf) ausgeloest. */
static RrBesuch rr_gemerkt;
static void *rr_gemerkt_ud;

void rr_merke(RrBesuch cb, void *ud) {
    rr_gemerkt = cb;
    rr_gemerkt_ud = ud;
}

int32_t rr_loese_aus(int32_t x) {
    return rr_gemerkt ? rr_gemerkt(x, rr_gemerkt_ud) : -1;
}

/* Strukturwert hin und zurueck (SysV: zwei INTEGER-Achtbytes; Win64: per
 * Referenz bzw. sret). */
int64_t rr_tausche(RrTausch cb, int32_t a, int64_t b) {
    RrPaar p = { a, b };
    RrPaar r = cb(p);
    return (int64_t)r.a * 1000000 + r.b;
}

typedef struct { RrBesuch cb; void *ud; int32_t x; int32_t erg; } RrThreadArg;

#ifdef _WIN32
static DWORD WINAPI rr_thread_lauf(LPVOID roh) {
    RrThreadArg *t = (RrThreadArg *)roh;
    t->erg = t->cb(t->x, t->ud);
    return 0;
}
#else
static void *rr_thread_lauf(void *roh) {
    RrThreadArg *t = (RrThreadArg *)roh;
    t->erg = t->cb(t->x, t->ud);
    return NULL;
}
#endif

/* Ruft cb auf einem frisch erzeugten C-Thread (kein Moo-Thread) und wartet. */
int32_t rr_auf_thread(RrBesuch cb, void *ud, int32_t x) {
    RrThreadArg t = { cb, ud, x, -1 };
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, rr_thread_lauf, &t, 0, NULL);
    if (h == NULL) return -2;
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
#else
    pthread_t th;
    if (pthread_create(&th, NULL, rr_thread_lauf, &t) != 0) return -2;
    pthread_join(th, NULL);
#endif
    return t.erg;
}

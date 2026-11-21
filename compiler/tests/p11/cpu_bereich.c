/* P11-T27: Linux-Affinität/TLS/Fehlerpfade; nur Testtreiber. */
#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern uint64_t cpu_bereich_probe(uint64_t);
extern uint64_t cpu_bereich_unguard(uint64_t);
extern uint64_t cpu_bereich_fehler(uint64_t);
extern int __real_sched_setaffinity(pid_t, size_t, const cpu_set_t *);
static _Thread_local cpu_set_t original;
static _Thread_local unsigned sets, cpuid_calls, observations, cleanups, fail_mode;
static _Thread_local int barrier_active;
static pthread_barrier_t barrier;

static void check(int ok, const char *why) {
    if (!ok) { fprintf(stderr, "FAIL CPU-Bereich: %s\n", why); _exit(91); }
}
void cpu_cpuid_zaehlen(void) { ++cpuid_calls; }
void cpu_cleanup_erhoehen(void) { ++cleanups; }
int __wrap_sched_setaffinity(pid_t pid, size_t bytes, const cpu_set_t *mask) {
    ++sets;
    if (fail_mode == 1 || (fail_mode == 2 && sets == 2)) {
        errno = EPERM; return -1;
    }
    return __real_sched_setaffinity(pid, bytes, mask);
}
void cpu_beobachten(void) {
    cpu_set_t actual;
    check(sched_getaffinity(0,sizeof actual,&actual)==0,"Maske im Bereich lesen");
    if (fail_mode == 1) check(CPU_EQUAL(&actual,&original),"Bindungsfehler veraendert keine Maske");
    else check(CPU_COUNT(&actual)==1 && CPU_ISSET(sched_getcpu(),&actual),"auf genau aktuellen Kern gebunden");
    if (barrier_active && observations==0) {
        int rc=pthread_barrier_wait(&barrier);
        check(rc==0 || rc==PTHREAD_BARRIER_SERIAL_THREAD,"Threadbarriere");
    }
    ++observations;
}
static void start(unsigned mode) {
    check(sched_getaffinity(0,sizeof original,&original)==0,"alte Maske lesen");
    sets=cpuid_calls=observations=cleanups=0; fail_mode=mode;
}
static void restored(void) {
    cpu_set_t actual;
    check(sched_getaffinity(0,sizeof actual,&actual)==0 && CPU_EQUAL(&actual,&original),"alte Maske wiederhergestellt");
}
static uint32_t reference(void) {
    uint32_t crc=0;
    for (unsigned i=0;i<64;++i) {
        crc^=7;
        for(unsigned b=0;b<8;++b) crc=(crc>>1)^((crc&1)?UINT32_C(0x82f63b78):0);
    }
    return crc;
}
static void normal(unsigned mode) {
    const uint64_t rounds=100;
    start(mode);
    check(cpu_bereich_probe(rounds)==rounds*(7+(uint64_t)reference()),"CRC-/SIMD-Bytes");
    restored();
    check(observations==3,"aeusserer/innerer Bereich beobachtet");
    if(mode==1) {
        check(sets==2,"aeusserer/innerer Bindungsversuch jeweils einmal");
        check(cpuid_calls>=rounds,"Bindungsfehler behaelt normale Guards");
    } else {
        check(sets==2,"Verschachtelung bindet nur aeusseren Bereich");
        check(cpuid_calls>0 && cpuid_calls<20,"CPU-Auswahl einmal statt je Rechenaufruf");
    }
    unsigned before=cpuid_calls;
    check(cpu_bereich_unguard(1)==7+(uint64_t)reference(),"Rechnung nach Bereich");
    check(cpuid_calls>before,"ausserhalb Bereich wieder CPUID-Guard");
}
static void *worker(void *unused) {
    (void)unused; barrier_active=1; normal(0); barrier_active=0; return 0;
}
void moo_native_abbruch(const char *message) {
    (void)message;
    check(cleanups==1,"Benutzer-Cleanup vor terminalem Fehler");
    if (fail_mode!=2) restored();
    unsigned before=cpuid_calls;
    (void)cpu_bereich_unguard(1);
    check(cpuid_calls>before,"TLS-Freigabe vor Restore/Abbruch");
    raise(SIGILL); _exit(92);
}
void moo_native_fehler(void) { check(0,"unerwarteter fangbarer Fehler im C-Treiber"); }
static void terminal(unsigned mode) {
    pid_t child=fork(); check(child>=0,"fork");
    if(!child) {
        struct rlimit r={0,0}; (void)setrlimit(RLIMIT_CORE,&r);
        start(mode); (void)cpu_bereich_fehler(2); _exit(93);
    }
    int status=0; check(waitpid(child,&status,0)==child,"waitpid");
    check(WIFSIGNALED(status) && WTERMSIG(status)==SIGILL,"Fehler/Restorefehler bricht nach Cleanup ab");
}
int main(void) {
    normal(0); normal(1);
    check(pthread_barrier_init(&barrier,0,4)==0,"Barriere anlegen");
    pthread_t threads[4];
    for(unsigned i=0;i<4;++i) check(pthread_create(&threads[i],0,worker,0)==0,"Thread anlegen");
    for(unsigned i=0;i<4;++i) check(pthread_join(threads[i],0)==0,"Thread abwarten");
    check(pthread_barrier_destroy(&barrier)==0,"Barriere entfernen");
    terminal(0); terminal(1); terminal(2);
    puts("CPU-Bereich OK: Verschachtelung, 4 TLS-Threads, einmalige CPUID-Auswahl, Fallback, Affinitaet und Cleanup");
    return 0;
}

#ifndef MOO_RC_H
#define MOO_RC_H
/*
 * P2-T112: Refcount mit Geteilt-Kennung im Vorzeichen.
 *
 * Jeder refcountete Heap-Wert hat als erstes Feld einen int32_t-Zaehler r:
 *   r > 0   ungeteilt, r Referenzen; nur EIN Thread erreicht den Wert.
 *           Gezaehlt wird mit einfachem Laden/Speichern (kein lock-Praefix).
 *   r == 0  statisch/unsterblich (z. B. statische MooErrors), nie gezaehlt.
 *   r < 0   geteilt, -r Referenzen; mehrere Threads koennen ihn erreichen.
 *           Gezaehlt wird atomar (Inkrement = atomar -1, Dekrement = atomar +1).
 * Der schnelle Pfad (r > 0) prueft damit genau dasselbe wie vor P2-T112
 * (r <= 0 ? Sonderfall : einfach zaehlen); Kosten entstehen nur fuer geteilte Werte.
 *
 * Invarianten:
 *  - Die Kennung wird nur gesetzt (moo_teilen: r -> -r), nie entfernt.
 *  - Sie wird gesetzt, BEVOR der Wert einem anderen Thread zugaenglich wird
 *    (starte, Kanal-senden, Speichern in Globale nach dem ersten starte,
 *    Einfuegen in einen geteilten Container). Bis dahin gehoert der Wert
 *    genau einem Thread, das Setzen ist also rennfrei; die Veroeffentlichung
 *    (pthread_create, Kanal-Mutex) ordnet es vor jeden fremden Zugriff.
 *  - Ist ein Wert geteilt, ist alles von ihm Erreichbare ebenfalls geteilt
 *    (moo_teilen markiert tief).
 *  - P2-T114: Geteilt heisst eingefroren. Setter auf geteilte Container
 *    (Liste, Woerterbuch, Objekt) werfen MOO_GETEILT_TEXT; ein geteilter
 *    Container bekommt also nie neue Elemente, die markiert werden muessten.
 *
 * Speicherordnung geteilter Werte: Referenz hinzufuegen relaxed, abgeben
 * release mit acquire-Fence beim Erreichen von 0 (Muster wie std::shared_ptr).
 * x86: lock add / lock xadd; ARM64: LDADD bzw. LL/SC. Ungeteilte Werte werden
 * ueber relaxed-Laden/Speichern angefasst: dieselben Befehle wie ein normaler
 * Zugriff, aber sauber fuer TSan und das C-Speichermodell.
 */
#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#include <intrin.h>
#define MOO_RC_INLINE static __forceinline
#define MOO_RC_SELTEN(x) (x)
MOO_RC_INLINE int32_t moo_rc_lesen(const int32_t* rc) { return *(const volatile int32_t*)rc; }
MOO_RC_INLINE void moo_rc_schreiben(int32_t* rc, int32_t v) { *(volatile int32_t*)rc = v; }
MOO_RC_INLINE size_t moo_rc_size_lesen(const size_t* z) { return *(const volatile size_t*)z; }
/* Atomar addieren, liefert den NEUEN Wert (volle Barriere). */
MOO_RC_INLINE int32_t moo_rc_atomar_add(int32_t* rc, int32_t d) {
    return (int32_t)_InterlockedExchangeAdd((volatile long*)rc, (long)d) + d;
}
#define MOO_RC_ACQUIRE_FENCE() ((void)0)
/* size_t-Zaehler (Puffer-Speicher, von mehreren Sichten geteilt): immer atomar. */
MOO_RC_INLINE void moo_rc_size_inc(size_t* z) {
#ifdef _WIN64
    _InterlockedIncrement64((volatile __int64*)z);
#else
    _InterlockedIncrement((volatile long*)z);
#endif
}
MOO_RC_INLINE size_t moo_rc_size_dec(size_t* z) {
#ifdef _WIN64
    return (size_t)_InterlockedDecrement64((volatile __int64*)z);
#else
    return (size_t)_InterlockedDecrement((volatile long*)z);
#endif
}
#else
#define MOO_RC_INLINE static inline __attribute__((always_inline))
#define MOO_RC_SELTEN(x) __builtin_expect(!!(x), 0)
MOO_RC_INLINE int32_t moo_rc_lesen(const int32_t* rc) { return __atomic_load_n(rc, __ATOMIC_RELAXED); }
MOO_RC_INLINE void moo_rc_schreiben(int32_t* rc, int32_t v) { __atomic_store_n(rc, v, __ATOMIC_RELAXED); }
/* Atomar addieren, liefert den NEUEN Wert. Positive d nur beim Abgeben. */
#if defined(__SANITIZE_THREAD__)
#define MOO_RC_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define MOO_RC_TSAN 1
#endif
#endif
#ifdef MOO_RC_TSAN
/* TSan modelliert Fences nicht: dort acq_rel am Dekrement statt Fence. */
#define MOO_RC_ABGABE_ORDNUNG __ATOMIC_ACQ_REL
#define MOO_RC_ACQUIRE_FENCE() ((void)0)
#else
#define MOO_RC_ABGABE_ORDNUNG __ATOMIC_RELEASE
#define MOO_RC_ACQUIRE_FENCE() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#endif
MOO_RC_INLINE int32_t moo_rc_atomar_add(int32_t* rc, int32_t d) {
    return d > 0 ? __atomic_add_fetch(rc, d, MOO_RC_ABGABE_ORDNUNG)
                 : __atomic_add_fetch(rc, d, __ATOMIC_RELAXED);
}
/* size_t-Zaehler (Puffer-Speicher, von mehreren Sichten geteilt): immer atomar,
 * die Operationen sind selten (Sicht/Pin anlegen und freigeben). */
static inline void moo_rc_size_inc(size_t* z) { __atomic_fetch_add(z, 1, __ATOMIC_RELAXED); }
static inline size_t moo_rc_size_dec(size_t* z) { return __atomic_sub_fetch(z, 1, __ATOMIC_ACQ_REL); }
static inline size_t moo_rc_size_lesen(const size_t* z) { return __atomic_load_n(z, __ATOMIC_ACQUIRE); }
#endif

/* true, wenn der Zaehler einen geteilten Wert beschreibt. */
MOO_RC_INLINE int moo_rc_ist_geteilt(const int32_t* rc) { return moo_rc_lesen(rc) < 0; }

/* P2-T114: Meldung fuer Setter auf geteilte (und damit eingefrorene) Werte. */
#define MOO_GETEILT_TEXT "Wert ist zwischen Threads geteilt und damit eingefroren"

/* Als geteilt kennzeichnen. Nur vom (noch) alleinigen Besitzer aufzurufen;
 * unsterbliche (0) und bereits geteilte Werte bleiben unveraendert. */
MOO_RC_INLINE void moo_rc_bit_setzen(int32_t* rc) {
    int32_t r = moo_rc_lesen(rc);
    if (r > 0) moo_rc_schreiben(rc, -r);
}

/* Referenz hinzufuegen. */
MOO_RC_INLINE void moo_rc_inc(int32_t* rc) {
    int32_t r = moo_rc_lesen(rc);
    if (MOO_RC_SELTEN(r <= 0)) {
        if (r < 0) moo_rc_atomar_add(rc, -1);
        return;
    }
    moo_rc_schreiben(rc, r + 1);
}

/* Referenz abgeben. Liefert 1, wenn der Wert jetzt freizugeben ist. */
MOO_RC_INLINE int moo_rc_dec(int32_t* rc) {
    int32_t r = moo_rc_lesen(rc);
    if (MOO_RC_SELTEN(r <= 0)) {
        if (r == 0 || moo_rc_atomar_add(rc, 1) != 0) return 0;
        MOO_RC_ACQUIRE_FENCE();
        return 1;
    }
    r -= 1;  /* wie vor P2-T112: sub, speichern, Flags des sub pruefen */
    moo_rc_schreiben(rc, r);
    return r == 0;
}

/* Zaehler voll (Ueberlaufschutz fuer Puffer/Strukturboxen). */
MOO_RC_INLINE int moo_rc_voll(const int32_t* rc) {
    int32_t r = moo_rc_lesen(rc);
    return r == INT32_MAX || r == -INT32_MAX;
}

#endif /* MOO_RC_H */

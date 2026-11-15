#include "moo_runtime.h"

int moo_error_flag = 0;
MooValue moo_last_error = { MOO_NONE, 0 };
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];

static void clear_last_error(void) {
    /* moo_throw darf jeden MooValue konsumieren. Der zentrale Release-Dispatch
       ist fuer Nicht-Heap-Tags ein No-op und gibt Heap-Werte passend frei. */
    moo_release(moo_last_error);
    moo_last_error = moo_none();
    moo_error_flag = 0;
}

/* Konsumiert error (+1 owning) und uebertraegt ihn in die globale Ablage. */
void moo_throw(MooValue error) {
    clear_last_error();
    moo_last_error = error;
    moo_error_flag = 1;
    if (moo_try_depth == 0) {
        fprintf(stderr, "Unbehandelter Fehler: ");
        moo_print(error);
        clear_last_error();
        exit(1);
    }
}

void moo_try_enter(void) {
    if (moo_try_depth == 0) clear_last_error();
    moo_try_depth++;
}

int moo_try_check(void) {
    return moo_error_flag;
}

void moo_try_leave(void) {
    if (moo_try_depth > 0) moo_try_depth--;
    if (moo_try_depth == 0) clear_last_error();
}

/* Uebertraegt den gespeicherten Fehler (+1) und konsumiert nur das Flag.
 * Die Handler-Tiefe bleibt fuer den separaten moo_try_leave-Aufruf unveraendert. */
MooValue moo_try_take_error(void) {
    MooValue error = moo_last_error;
    moo_last_error = moo_none();
    moo_error_flag = 0;
    return error;
}

/* Liefert +1 owning; der Catch-Code darf den Wert ueber try_leave hinaus halten. */
MooValue moo_get_error(void) {
    MooValue error = moo_last_error;
    moo_retain(error);
    return error;
}

/* P2-T34 (ADR f67bfcbd §3): Fehler einer nativen Ganzzahloperation (Division
   oder Rest durch Null bzw. MIN/-1, Shiftzaehler ausserhalb 0..N-1) als
   fangbarer Moo-Fehler. Innerhalb versuche: setzt moo_throw nur das Flag und
   kehrt zurueck; ohne aktiven Handler beendet es den Prozess. */
void moo_native_fehler(const char* text) {
    /* P2-T58 (Plan 4500717b A3): erster Fehler gewinnt. Ein nativer Aufrufer,
       der mit dem Nullwert eines bereits gescheiterten nativen Callees
       weiterrechnet (z. B. 10 / b(x)), darf dessen Meldung nicht
       ueberschreiben. Ausserhalb versuche: hat schon der erste moo_throw den
       Prozess beendet; dort aendert sich nichts. */
    if (moo_error_flag && moo_try_depth > 0) return;
    moo_throw(moo_string_new(text));
}

/* P2-T58 C2: Systemkoerper fragen nach einem fehlbaren nativen Aufruf, ob ein
   Fehler ansteht (Flag), und kehren dann ohne Seiteneffekte zurueck. Starke
   Fassung zur schwachen Moduldefinition (liefert dort 0). */
int moo_native_fehler_offen(void) {
    return moo_error_flag;
}

/* P2-T84 (Plan 91033131 §6): Fehler in einer aus C gerufenen exportierten
   Moo-Funktion (@einstieg). Ein Moo-Fehler kann nicht ueber fremde C-Frames
   abgewickelt werden; auch ein aktives versuche: des Moo-Aufrufers faengt ihn
   nicht. Meldung und ggf. anstehenden Moo-Fehler ausgeben, RC 1. Starke
   Fassung zur schwachen Moduldefinition (dort llvm.trap). */
void moo_native_abbruch(const char* text) {
    fprintf(stderr, "Abbruch: %s\n", text);
    if (moo_error_flag) {
        fprintf(stderr, "Anstehender Fehler: ");
        moo_print(moo_last_error);
    }
    fflush(stdout);
    exit(1);
}

/* P2-T21/T57 Batch D (Plan f4655d20 E5): fehlbare C-Rueckrufe beruehren den
   globalen, nicht thread-lokalen Fehlerzustand (moo_error_flag,
   moo_last_error, moo_try_depth). Sie sind deshalb nur auf Moo-Threads
   erlaubt: dem Hauptthread (vor main per Konstruktor markiert, da der Codegen
   keinen garantierten Runtime-Einstieg ruft) und den per moo_thread_spawn
   gestarteten Threads (thread_runner_common). Fehlerfreie Rueckrufe rufen
   diese Pruefung nicht und bleiben threadneutral wie C. Grenze: wird die
   Runtime per dlopen/LoadLibrary aus einem Nebenthread geladen, gilt dieser
   als Hauptthread. */
#if defined(_MSC_VER)
#define MOO_RUECKRUF_TLS __declspec(thread)
#else
#define MOO_RUECKRUF_TLS _Thread_local
#endif

static MOO_RUECKRUF_TLS int moo_ist_moo_thread = 0;

void moo_native_thread_markieren(void) {
    moo_ist_moo_thread = 1;
}

#if defined(_MSC_VER)
static int moo_native_hauptthread_markieren_(void) {
    moo_native_thread_markieren();
    return 0;
}
__pragma(section(".CRT$XCU", read))
__declspec(allocate(".CRT$XCU"))
static int (*moo_native_hauptthread_ptr_)(void) = moo_native_hauptthread_markieren_;
#else
static void moo_native_hauptthread_markieren_(void) __attribute__((constructor));
static void moo_native_hauptthread_markieren_(void) {
    moo_native_thread_markieren();
}
#endif

/* Starke Fassung zur schwachen Moduldefinition (kehrt dort sofort zurueck).
   Auf einem fremden Thread wird weder moo_throw noch der Fehlerzustand
   angefasst: Diagnose und abort(), kein Unwinding durch C. */
void moo_native_rueckruf_thread(const char* name) {
    if (moo_ist_moo_thread) return;
    fprintf(stderr,
            "Rückruf '%s' auf fremdem Thread: fehlbare Rückrufe sind nur auf Moo-Threads erlaubt\n",
            name ? name : "?");
    fflush(stderr);
    abort();
}

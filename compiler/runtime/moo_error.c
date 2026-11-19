#include "moo_runtime.h"
#if defined(_WIN32)
#include <process.h>  /* _exit (P2-T117) */
#else
#include <unistd.h>   /* _exit (P2-T117) */
#endif

#include <stdlib.h>
#include <string.h>

/* P2-T113: Fehlerzustand je Thread (siehe moo_runtime.h). */
MOO_TLS int moo_error_flag = 0;
MOO_TLS MooValue moo_last_error = { MOO_NONE, 0 };
MOO_TLS int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];

/* P2-T95: Wurfort des anstehenden Fehlers und ob er schon gemeldet ist. */
static MOO_TLS const MooFehlerOrt* moo_fehler_ort = NULL;
static MOO_TLS int moo_fehler_gemeldet = 0;

/* P2-T113: Windows-Waechter fuer den heissen Pfad. MinGW-GCC legt
   _Thread_local als emutls an (jeder Zugriff ruft __emutls_get_address);
   moo_try_check laeuft aber nach jedem Statement im versuche. Es liest dort
   zuerst die prozessweite Zahl der Threads mit gesetztem Flag und greift nur
   bei >0 auf den Thread-Zustand zu. Geschrieben wird der Zaehler nur beim
   Setzen/Loeschen des Flags (selten). Ein Thread, der mit gesetztem Flag
   endet, laesst ihn >0 stehen: dann nur langsamer, nie falsch (der eigene
   Thread-Zustand entscheidet). ELF (Linux/BSD): der Linker entspannt den
   TLS-Zugriff im Programm zu einem %fs-relativen Laden, gleich teuer wie
   vorher das globale Flag; dort waere der Waechter Mehrarbeit. */
#if defined(_WIN32)
#define MOO_FEHLER_WAECHTER 1
#if defined(_MSC_VER)
#include <intrin.h>
static volatile long moo_fehler_waechter = 0;
#define MOO_WAECHTER_PLUS() _InterlockedIncrement(&moo_fehler_waechter)
#define MOO_WAECHTER_MINUS() _InterlockedDecrement(&moo_fehler_waechter)
#define MOO_WAECHTER_LESEN() (moo_fehler_waechter)
#else
static long moo_fehler_waechter = 0;
#define MOO_WAECHTER_PLUS() __atomic_add_fetch(&moo_fehler_waechter, 1, __ATOMIC_RELAXED)
#define MOO_WAECHTER_MINUS() __atomic_sub_fetch(&moo_fehler_waechter, 1, __ATOMIC_RELAXED)
#define MOO_WAECHTER_LESEN() __atomic_load_n(&moo_fehler_waechter, __ATOMIC_RELAXED)
#endif
#else
#define MOO_FEHLER_WAECHTER 0
#endif

static inline void fehler_flag_setzen(int wert) {
#if MOO_FEHLER_WAECHTER
    if (wert != moo_error_flag) {
        if (wert) MOO_WAECHTER_PLUS();
        else MOO_WAECHTER_MINUS();
    }
#endif
    moo_error_flag = wert;
}

static void clear_last_error(void) {
    /* moo_throw darf jeden MooValue konsumieren. Der zentrale Release-Dispatch
       ist fuer Nicht-Heap-Tags ein No-op und gibt Heap-Werte passend frei. */
    moo_release(moo_last_error);
    moo_last_error = moo_none();
    fehler_flag_setzen(0);
    moo_fehler_ort = NULL;
    moo_fehler_gemeldet = 0;
}

/* ---- P2-T95: Hinweise statt Abbruch -------------------------------------- */

/* Je Thread einmal aus der Umgebung gelesen (kein geteilter Zwischenspeicher). */
static MOO_TLS int moo_streng_wert = -1;

int moo_fehler_streng(void) {
    if (moo_streng_wert < 0) {
        const char* wert = getenv("MOO_STRENG");
        /* Opt-in nur mit exakt "1" (User-Entscheid #38187); jeder andere Wert
         * ("2", "true", "ja", leer) bleibt Standard: Hinweis + Weiterlauf. */
        moo_streng_wert = (wert && strcmp(wert, "1") == 0) ? 1 : 0;
    }
    return moo_streng_wert;
}

/* Fehlertext ohne Besitzuebernahme: Strings direkt, sonst moo_to_string. */
static void fehler_text(MooValue fehler, char* puffer, size_t groesse) {
    if (fehler.tag == MOO_STRING) {
        MooString* s = MV_STR(fehler);
        snprintf(puffer, groesse, "%s", (s && s->chars) ? s->chars : "");
        return;
    }
    MooValue text = moo_to_string(fehler);
    MooString* s = text.tag == MOO_STRING ? MV_STR(text) : NULL;
    snprintf(puffer, groesse, "%s", (s && s->chars) ? s->chars : "?");
    moo_release(text);
}

/* " (datei:zeile)" bzw. " (datei)" / " (Zeile n)"; leer ohne Angaben. */
static void ort_text(const char* datei, int64_t zeile, char* puffer, size_t groesse) {
    if (datei && datei[0] && zeile > 0) snprintf(puffer, groesse, " (%s:%lld)", datei, (long long)zeile);
    else if (datei && datei[0]) snprintf(puffer, groesse, " (%s)", datei);
    else if (zeile > 0) snprintf(puffer, groesse, " (Zeile %lld)", (long long)zeile);
    else puffer[0] = '\0';
}

static void wurfort_drucken(void) {
    char ort[512];
    if (moo_fehler_ort) {
        ort_text(moo_fehler_ort->datei, moo_fehler_ort->zeile, ort, sizeof ort);
        fprintf(stderr, "  geworfen in %s%s\n",
                moo_fehler_ort->funktion ? moo_fehler_ort->funktion : "Hauptprogramm", ort);
    } else {
        fprintf(stderr, "  aufgetreten in der Laufzeit\n");
    }
}

/* Zaehler je Hinweisstelle (Schleifen fluten nicht). */
#define MOO_HINWEIS_STELLEN 64
typedef struct {
    const void* schluessel;
    uint64_t text_hash;
    int64_t anzahl;
    char zusammenfassung[192];
} MooHinweisZaehler;

static MooHinweisZaehler moo_hinweise[MOO_HINWEIS_STELLEN];
static int moo_hinweis_belegt = 0;
static int64_t moo_hinweis_ueberlauf = 0;
static int moo_hinweis_atexit = 0;

/* P2-T113: Die Zaehltabelle ist prozessweit (Zusammenfassung am Ende); Hinweise
   aus mehreren Threads greifen nur unter dieser Sperre zu (selten, kalt). */
#if defined(_MSC_VER)
#include <intrin.h>
static volatile long moo_hinweis_sperre = 0;
#define MOO_HINWEIS_SPERREN() while (_InterlockedExchange(&moo_hinweis_sperre, 1)) {}
#define MOO_HINWEIS_FREIGEBEN() _InterlockedExchange(&moo_hinweis_sperre, 0)
#else
static int moo_hinweis_sperre = 0;
#define MOO_HINWEIS_SPERREN() while (__atomic_exchange_n(&moo_hinweis_sperre, 1, __ATOMIC_ACQUIRE)) {}
#define MOO_HINWEIS_FREIGEBEN() __atomic_store_n(&moo_hinweis_sperre, 0, __ATOMIC_RELEASE)
#endif

static uint64_t text_hash(const char* text) {
    uint64_t h = UINT64_C(1469598103934665603);
    for (const unsigned char* p = (const unsigned char*)text; p && *p; p++) {
        h ^= *p;
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static void hinweise_zusammenfassen(void) {
    MOO_HINWEIS_SPERREN();
    fflush(stdout);
    for (int i = 0; i < moo_hinweis_belegt; i++) {
        if (moo_hinweise[i].anzahl > 1) {
            fprintf(stderr, "Hinweis: %s trat insgesamt %lld Mal auf\n",
                    moo_hinweise[i].zusammenfassung, (long long)moo_hinweise[i].anzahl);
        }
    }
    if (moo_hinweis_ueberlauf > 0) {
        fprintf(stderr, "Hinweis: %lld weitere Hinweise an anderen Stellen nicht einzeln ausgegeben\n",
                (long long)moo_hinweis_ueberlauf);
    }
    MOO_HINWEIS_FREIGEBEN();
}

/* 1 = erster Fall dieser Stelle (ausgeben), 0 = nur gezaehlt. Schluessel NULL:
   Stelle ueber den Text (Laufzeitfehler ohne Wurfort). */
static int hinweis_zaehlen_gesperrt(const void* schluessel, const char* text, const char* zusammenfassung) {
    uint64_t hash = schluessel ? 0 : text_hash(text);
    for (int i = 0; i < moo_hinweis_belegt; i++) {
        if (moo_hinweise[i].schluessel == schluessel && moo_hinweise[i].text_hash == hash) {
            moo_hinweise[i].anzahl++;
            return 0;
        }
    }
    if (!moo_hinweis_atexit) {
        moo_hinweis_atexit = 1;
        atexit(hinweise_zusammenfassen);
    }
    if (moo_hinweis_belegt == MOO_HINWEIS_STELLEN) {
        moo_hinweis_ueberlauf++;
        return moo_hinweis_ueberlauf == 1;  /* Ueberlauf: einmal sichtbar */
    }
    MooHinweisZaehler* z = &moo_hinweise[moo_hinweis_belegt++];
    z->schluessel = schluessel;
    z->text_hash = hash;
    z->anzahl = 1;
    snprintf(z->zusammenfassung, sizeof z->zusammenfassung, "%s", zusammenfassung ? zusammenfassung : "");
    return 1;
}

static int hinweis_zaehlen(const void* schluessel, const char* text, const char* zusammenfassung) {
    MOO_HINWEIS_SPERREN();
    int erster = hinweis_zaehlen_gesperrt(schluessel, text, zusammenfassung);
    MOO_HINWEIS_FREIGEBEN();
    return erster;
}

void moo_fehler_hinweis(const void* schluessel, const char* zusammenfassung, const char* text) {
    if (hinweis_zaehlen(schluessel, text ? text : "", zusammenfassung)) {
        fprintf(stderr, "Hinweis: %s\n", text ? text : "");
        fflush(stderr);
    }
}

/* Tiefe 0: niemand kann den Fehler mehr fangen. P2-T117 (Koordinator-
   Entscheid, korrigiert P2-T95): Diagnose auf stderr und IMMER Exit 1, auch
   ohne MOO_STRENG. Kalter Pfad; der heisse Pfad (moo_try_check) ist
   unberuehrt. */
static int moo_ist_hauptthread(void);
static void moo_nebenthread_fehler_merken(void);

static void unbehandelt_ohne_versuche(MooValue error) {
    char text[256];
    fehler_text(error, text, sizeof text);
    fflush(stdout);
    fprintf(stderr, "Unbehandelter Fehler: %s\n", text);
    wurfort_drucken();
    if (!moo_ist_hauptthread()) {
        /* Nebenthread (P2-T113: Fehlerzustand je Thread): andere Threads
           laufen weiter, der Fehler wird aber nicht verschluckt. Der
           Prozess endet am Schluss mit Exit-Code 1 (atexit-Haken). */
        fprintf(stderr, "  nicht behandelt im Nebenthread – das Programm endet mit Exit-Code 1\n");
        fflush(stderr);
        moo_nebenthread_fehler_merken();
        clear_last_error();
        return;
    }
    fprintf(stderr, "  nicht behandelt: kein versuche: im Aufrufpfad – Abbruch mit Exit-Code 1\n");
    fflush(stderr);
    clear_last_error();
    exit(1);
}

static void moo_throw_intern(MooValue error, const MooFehlerOrt* ort) {
    clear_last_error();
    moo_last_error = error;
    fehler_flag_setzen(1);
    moo_fehler_ort = ort;
    if (moo_try_depth == 0) unbehandelt_ohne_versuche(error);
}

/* Konsumiert error (+1 owning) und uebertraegt ihn in die globale Ablage. */
void moo_throw(MooValue error) {
    moo_throw_intern(error, NULL);
}

void moo_throw_ort(MooValue error, const MooFehlerOrt* ort) {
    moo_throw_intern(error, ort);
}

void moo_fehler_unbehandelt(const MooFehlerStelle* stelle) {
    if (!moo_error_flag || moo_fehler_gemeldet || !stelle) return;
    moo_fehler_gemeldet = 1;
    char text[256];
    fehler_text(moo_last_error, text, sizeof text);
    const char* funktion = stelle->funktion ? stelle->funktion : "Hauptprogramm";
    const char* aufruf = stelle->aufruf ? stelle->aufruf : "?";
    char ort[512];
    ort_text(stelle->datei, stelle->zeile, ort, sizeof ort);
    char zusammenfassung[192];
    /* Ortsangabe begrenzt wie in unbehandelt_ohne_versuche (Zusammenfassung 192). */
    snprintf(zusammenfassung, sizeof zusammenfassung, "der unbehandelte Fehler in %s beim Aufruf von %s%.100s",
             funktion, aufruf, ort);
    int streng = moo_fehler_streng();
    if (streng || hinweis_zaehlen(stelle, text, zusammenfassung)) {
        fflush(stdout);
        fprintf(stderr, "Hinweis: unbehandelter Fehler „%s“\n", text);
        wurfort_drucken();
        fprintf(stderr, "  nicht behandelt in %s beim Aufruf von %s%s – %s läuft weiter\n",
                funktion, aufruf, ort, funktion);
        fflush(stderr);
    }
    if (streng) {
        fprintf(stderr, "Unbehandelter Fehler: ");
        moo_print(moo_last_error);
        fflush(stdout);
        fprintf(stderr, "Abbruch, weil MOO_STRENG=1 gesetzt ist\n");
        clear_last_error();
        exit(1);
    }
}

void moo_try_enter(void) {
    if (moo_try_depth == 0) clear_last_error();
    moo_try_depth++;
}

int moo_try_check(void) {
#if MOO_FEHLER_WAECHTER
    if (MOO_WAECHTER_LESEN() == 0) return 0;
#endif
    return moo_error_flag;
}

/* P2-T113: Fehlerzustand des aktuellen Threads fuer die nativen Argument-
   Adapter des Codegens in EINEM Aufruf: Bit 0 = Fehler anstehend, Bit 1 =
   Handler aktiv (moo_try_depth != 0). Thread-lokaler Speicher (ELF-TLS,
   Windows-TLS oder emutls) bleibt so allein Sache der C-Runtime. */
int moo_fehler_zustand(void) {
    return (moo_error_flag != 0) | ((moo_try_depth != 0) << 1);
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
    fehler_flag_setzen(0);
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

/* P2-T126: native Stack-Cleanups verwenden dieselben owning Fehlerwerte
 * wie der dynamische Defer-Drain. Nur Zeiger passieren die native C-ABI. */
void moo_native_cleanup_enter(MooValue *saved) {
    *saved = moo_try_take_error();
    moo_try_enter();
}

void moo_native_cleanup_step(MooValue *saved) {
    MooValue next = moo_try_take_error();
    if (saved->tag == MOO_NONE) *saved = next;
    else moo_release(next);
}

void moo_native_cleanup_leave(MooValue *saved) {
    moo_native_cleanup_step(saved);
    MooValue first = *saved;
    *saved = moo_none();
    moo_try_leave();
    /* Ein aktiver Caller-Handler bleibt erhalten. Ohne Caller sind nun alle
     * eigenen Cleanups beendet, bevor moo_throw den Prozess beendet. */
    if (first.tag != MOO_NONE) moo_throw(first);
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

/* P2-T21/T57 Batch D (Plan f4655d20 E5): fehlbare C-Rueckrufe setzen den
   Fehlerzustand (moo_error_flag, moo_last_error, moo_try_depth; seit P2-T113
   je Thread). Auf einem fremden Thread prueft oder faengt ihn kein Moo-
   Aufrufer; sie sind deshalb nur auf Moo-Threads erlaubt: dem Hauptthread (vor main per Konstruktor markiert, da der Codegen
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

/* P2-T117: Hauptthread-Kennung und Merker fuer unbehandelte Fehler in
   Nebenthreads. Der atexit-Haken wird im Konstruktor (vor allen anderen
   atexit-Registrierungen der Runtime) angemeldet und laeuft deshalb zuletzt. */
static MOO_RUECKRUF_TLS int moo_hauptthread_kennung = 0;
static volatile int moo_nebenthread_fehler = 0;

static int moo_ist_hauptthread(void) {
    return moo_hauptthread_kennung;
}

static void moo_nebenthread_fehler_merken(void) {
    moo_nebenthread_fehler = 1;
}

static void moo_nebenthread_fehler_exit(void) {
    if (!moo_nebenthread_fehler) return;
    fflush(NULL);
    _exit(1);
}

static void moo_hauptthread_einrichten(void) {
    moo_native_thread_markieren();
    moo_hauptthread_kennung = 1;
    atexit(moo_nebenthread_fehler_exit);
}

#if defined(_MSC_VER)
static int moo_native_hauptthread_markieren_(void) {
    moo_hauptthread_einrichten();
    return 0;
}
__pragma(section(".CRT$XCU", read))
__declspec(allocate(".CRT$XCU"))
static int (*moo_native_hauptthread_ptr_)(void) = moo_native_hauptthread_markieren_;
#else
static void moo_native_hauptthread_markieren_(void) __attribute__((constructor));
static void moo_native_hauptthread_markieren_(void) {
    moo_hauptthread_einrichten();
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

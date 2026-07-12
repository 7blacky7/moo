/**
 * test_bare_alloc_asan.c — Host-Harness fuer den Bare-Allocator (K3) und die
 * seriellen Formatter (K2) aus Plan-010, Task T1.
 *
 * LINK-SATZ (run_sanitize.sh EXTRA_HARNESSES):
 *   test_bare_alloc_asan.c ../moo_bare_alloc.c ../moo_bare_console.c   (-lm)
 *
 * BEWUSST NICHT gelinkt:
 *   * moo_bare.c      — kern_inb/kern_outb sind dort echte in/out-Inline-Asm;
 *                       im Userspace ohne ioperm => General Protection Fault.
 *   * moo_bare_boot.c — Multiboot2-Trampolin/_start, gehoert nicht auf den Host.
 *
 * Der Harness liefert stattdessen eigene Stubs:
 *   * kern_inb/kern_outb — 16550-Emulation: DLAB (LCR Bit 7) wird modelliert,
 *     damit der Divisor-Write in kern_seriell_init NICHT als Datenbyte im
 *     Capture-Puffer landet; MCR-Loopback (Bit 4) beantwortet den Selbsttest.
 *     Alle Datenbytes ausserhalb des Loopbacks werden mitgeschnitten — darauf
 *     pruefen die Formatter-Tests exakte Strings.
 *   * kern_panic — longjmp statt cli/hlt; unerwartete Panics => abort().
 *   * moo_number/moo_bool/moo_none — minimale MooValue-Konstruktoren.
 *
 * VGA-Pfade (kern_vga_*, moo_print) werden NIE aufgerufen: sie schreiben
 * direkt nach 0xB8000 und wuerden im Userspace segfaulten. Linken ist ok
 * (toter Code), Aufrufen nicht.
 *
 * Heap = malloc-Arena: ASan ueberwacht die Arena-Raender, UBSan die
 * uintptr_t-Adressrechnung des Allocators (UB-Policy: KEIN -fwrapv).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>

#include "moo_bare_kern.h"

/* ======================================================================
 * Test-Infrastruktur
 * ====================================================================== */
static int g_checks = 0;
static int g_fails  = 0;

#define CHECK(cond, msg) do {                                              \
    g_checks++;                                                            \
    if (!(cond)) {                                                         \
        g_fails++;                                                         \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));    \
    }                                                                      \
} while (0)

/* ======================================================================
 * Stub: kern_panic — longjmp statt halt-Loop
 * ====================================================================== */
static jmp_buf g_panic_jmp;
static volatile int g_panic_armed = 0;
static volatile int g_panic_hit   = 0;
static char g_panic_msg[256];

void kern_panic(const char* msg) {
    g_panic_hit = 1;
    snprintf(g_panic_msg, sizeof(g_panic_msg), "%s", msg ? msg : "");
    if (g_panic_armed) {
        longjmp(g_panic_jmp, 1);
    }
    fprintf(stderr, "UNERWARTETE kern_panic: %s\n", g_panic_msg);
    abort();
}

/* ======================================================================
 * Stub: Port-I/O — 16550-UART-Emulation mit Capture
 * ====================================================================== */
#define STUB_UART_BASE 0x3F8u

static uint8_t g_stub_lcr  = 0;   /* Line Control (Bit 7 = DLAB) */
static uint8_t g_stub_mcr  = 0;   /* Modem Control (Bit 4 = Loopback) */
static uint8_t g_stub_data = 0;   /* zuletzt auf DATA geschriebenes Byte */

static char g_capture[512];
static int  g_capture_len = 0;

static void capture_reset(void) {
    g_capture_len = 0;
    g_capture[0] = '\0';
}

void kern_outb(uint16_t port, uint8_t value) {
    if (port == STUB_UART_BASE + 3u) { g_stub_lcr = value; return; }   /* LCR */
    if (port == STUB_UART_BASE + 4u) { g_stub_mcr = value; return; }   /* MCR */
    if (port == STUB_UART_BASE + 0u) {                                  /* DATA/DLL */
        if (g_stub_lcr & 0x80u) return;        /* DLAB=1: Divisor-Write, kein Datenbyte */
        g_stub_data = value;
        if ((g_stub_mcr & 0x10u) == 0u) {      /* kein Loopback -> capture */
            if (g_capture_len < (int)sizeof(g_capture) - 1) {
                g_capture[g_capture_len++] = (char)value;
                g_capture[g_capture_len] = '\0';
            }
        }
        return;
    }
    /* IER/FCR/CRTC etc.: fuer den Host-Test irrelevant */
    (void)port; (void)value;
}

uint8_t kern_inb(uint16_t port) {
    if (port == STUB_UART_BASE + 5u) return 0x20u;  /* LSR: THR empty, immer sendebereit */
    if (port == STUB_UART_BASE + 0u) return g_stub_data; /* DATA: Loopback-Echo */
    return 0;
}

/* ======================================================================
 * Stub: MooValue-Konstruktoren (sonst in moo_bare.c)
 * ====================================================================== */
MooValue moo_number(double n) {
    MooValue v; v.tag = MOO_NUMBER; v.data = 0;
    kern_set_double(&v, n);
    return v;
}
MooValue moo_bool(bool b) {
    MooValue v; v.tag = MOO_BOOL; v.data = b ? 1u : 0u;
    return v;
}
MooValue moo_none(void) {
    MooValue v; v.tag = MOO_NONE; v.data = 0;
    return v;
}

/* ======================================================================
 * K2 — Formatter-Tests (seriell, via Capture)
 * ====================================================================== */
static void teste_formatter(void) {
    /* Init mit Loopback-Selbsttest gegen den Stub. */
    MooValue ok = kern_seriell_init();
    CHECK(ok.tag == MOO_BOOL && ok.data == 1u, "seriell_init: Loopback-Selbsttest muss bestehen");

    capture_reset();
    kern_seriell_dez_u64(0);
    CHECK(strcmp(g_capture, "0") == 0, "dez(0) muss '0' liefern");

    capture_reset();
    kern_seriell_dez_u64(42);
    CHECK(strcmp(g_capture, "42") == 0, "dez(42)");

    capture_reset();
    kern_seriell_dez_u64(UINT64_MAX);
    CHECK(strcmp(g_capture, "18446744073709551615") == 0, "dez(2^64-1) ohne libc");

    capture_reset();
    kern_seriell_hex_u64(0);
    CHECK(strcmp(g_capture, "0x0000000000000000") == 0, "hex(0): 16 Nibbles fix");

    capture_reset();
    kern_seriell_hex_u64(0xDEADBEEFull);
    CHECK(strcmp(g_capture, "0x00000000deadbeef") == 0, "hex(0xDEADBEEF) lowercase, padded");

    capture_reset();
    kern_seriell_hex_u64(UINT64_MAX);
    CHECK(strcmp(g_capture, "0xffffffffffffffff") == 0, "hex(2^64-1)");

    /* MooValue-Wrapper: Vorzeichen + Zeichen + CRLF-Verhalten. */
    capture_reset();
    kern_seriell_dez(moo_number(-7.0));
    CHECK(strcmp(g_capture, "-7") == 0, "dez-Wrapper: negatives Vorzeichen");

    capture_reset();
    kern_seriell_zeichen(moo_number(65.0));
    CHECK(strcmp(g_capture, "A") == 0, "zeichen-Wrapper: ASCII 65 -> 'A'");

    capture_reset();
    kern_seriell_zeichen_c('\n');
    CHECK(strcmp(g_capture, "\r\n") == 0, "LF wird als CRLF gesendet");
}

/* ======================================================================
 * K3 — Allocator-Tests (malloc-Arena)
 * ====================================================================== */
#define ARENA_GROESSE (1u << 20)   /* 1 MiB */
#define HDR 32u                    /* sizeof(KernBlock) — Layout-Annahme, unten geprueft */

static void teste_allocator(void) {
    unsigned char* arena = (unsigned char*)malloc(ARENA_GROESSE);
    uintptr_t start;
    uintptr_t ende;
    void* p1;
    void* p2;
    void* p3;
    void* p4;
    uint64_t belegt_vor;
    size_t i;
    const uint64_t randgroessen[] = {
        UINT64_MAX, UINT64_MAX - 1u, UINT64_MAX - 14u,
        UINT64_MAX - 15u, UINT64_MAX - 16u
    };

    CHECK(arena != NULL, "Arena-malloc");
    if (!arena) return;
    memset(arena, 0, ARENA_GROESSE);
    start = (uintptr_t)arena;
    ende = start + ARENA_GROESSE;

    /* --- Heap-Init: Alignment/Range fail-closed -------------------------- */
    kern_heap_init_c(UINTPTR_MAX - 7u, UINTPTR_MAX);
    CHECK(kern_alloc_c(1u) == NULL, "near-UINTPTR alignment-wrap deaktiviert Heap");
    kern_heap_init_c((uintptr_t)64u, (uintptr_t)32u);
    CHECK(kern_alloc_c(1u) == NULL, "invertierte Heap-Region abgelehnt");
    kern_heap_init_c((uintptr_t)16u, (uintptr_t)(16u + HDR + 15u));
    CHECK(kern_alloc_c(1u) == NULL, "zu kleine Heap-Region abgelehnt");

    kern_heap_init_c(start, ende);

    /* --- Basis: Alignment + Buchfuehrung -------------------------------- */
    p1 = kern_alloc_c(1u);       /* need -> 16 */
    p2 = kern_alloc_c(100u);     /* need -> 112 */
    CHECK(p1 && p2, "alloc(1)/alloc(100) liefern Speicher");
    CHECK(((uintptr_t)p1 % 16u) == 0u, "p1 16-Byte-aligned");
    CHECK(((uintptr_t)p2 % 16u) == 0u, "p2 16-Byte-aligned");
    CHECK((uintptr_t)p1 >= start && (uintptr_t)p2 < ende, "Pointer in der Arena");
    if (p1) memset(p1, 0xAA, 1u);
    if (p2) memset(p2, 0xBB, 100u);
    CHECK(kern_as_double(kern_speicher_belegt()) == 128.0,
          "belegt = 16 + 112");
    CHECK(kern_as_double(kern_speicher_peak()) == 128.0,
          "peak folgt belegt");
    CHECK((uintptr_t)p2 - (uintptr_t)p1 == 16u + HDR,
          "Blocklayout: payload+hdr-Abstand");

    /* --- Align-/Bump-Overflow: Reject ohne Zustandsaenderung ------------- */
    belegt_vor = (uint64_t)kern_as_double(kern_speicher_belegt());
    for (i = 0u; i < sizeof(randgroessen) / sizeof(randgroessen[0]); ++i)
        CHECK(kern_alloc_c(randgroessen[i]) == NULL,
              "UINT64-Randgroesse muss OOM/reject liefern");
    CHECK((uint64_t)kern_as_double(kern_speicher_belegt()) == belegt_vor,
          "Overflow-Reject aendert Belegt nicht");

    /* --- Free-List: first-fit + Split ------------------------------------ */
    kern_frei_c(p2);
    CHECK(kern_as_double(kern_speicher_belegt()) == 16.0,
          "frei(p2) bucht 112 aus");
    p3 = kern_alloc_c(50u);      /* need 64 */
    CHECK(p3 == p2, "first-fit reuse: p3 nimmt p2-Block");
    CHECK(kern_as_double(kern_speicher_belegt()) == 80.0,
          "belegt = 16 + 64");
    p4 = kern_alloc_c(10u);      /* need 16, Split-Rest */
    CHECK(p4 == (void*)((unsigned char*)p3 + 64u + HDR),
          "Split-Rest wird wiederverwendet");
    CHECK(kern_as_double(kern_speicher_belegt()) == 96.0,
          "belegt = 16 + 64 + 16");
    CHECK(kern_as_double(kern_speicher_peak()) == 128.0,
          "peak bleibt high-water");

    /* --- Invalid free: kein fremder Header wird vor Rangecheck gelesen --- */
    g_panic_armed = 1;
#define ERWARTE_FREE_PANIC(ptr, msg) do {                                  \
        g_panic_hit = 0;                                                    \
        if (setjmp(g_panic_jmp) == 0) {                                    \
            kern_frei_c((ptr));                                             \
            CHECK(0, (msg));                                                \
        }                                                                   \
        CHECK(g_panic_hit == 1, (msg));                                     \
    } while (0)
    ERWARTE_FREE_PANIC((void*)start, "free auf Heap-Header abgelehnt");
    ERWARTE_FREE_PANIC((void*)ende, "free am Heap-Ende abgelehnt");
    if (start > 0u)
        ERWARTE_FREE_PANIC((void*)(start - 1u), "free unter Heap abgelehnt");
    ERWARTE_FREE_PANIC((void*)((unsigned char*)p3 + 1u),
                       "misaligned interior free abgelehnt");
    ERWARTE_FREE_PANIC((void*)((unsigned char*)p3 + 16u),
                       "aligned interior free abgelehnt");
    ERWARTE_FREE_PANIC((void*)(arena + ARENA_GROESSE / 2u),
                       "free in ungebumpter Region abgelehnt");
#undef ERWARTE_FREE_PANIC
    g_panic_armed = 0;

    /* --- MooValue-Wrapper: nur endliche exakte Integer <= 2^53 ---------- */
#define PRUEF_ALLOC_NULL(v, msg)                                            \
    CHECK(kern_as_double(kern_alloc((v))) == 0.0, (msg))
    belegt_vor = (uint64_t)kern_as_double(kern_speicher_belegt());
    PRUEF_ALLOC_NULL(moo_number(NAN), "alloc NaN abgelehnt");
    PRUEF_ALLOC_NULL(moo_number(INFINITY), "alloc +Inf abgelehnt");
    PRUEF_ALLOC_NULL(moo_number(-1.0), "alloc negativ abgelehnt");
    PRUEF_ALLOC_NULL(moo_number(1.5), "alloc fraktional abgelehnt");
    PRUEF_ALLOC_NULL(moo_number(9007199254740994.0), "alloc >2^53 abgelehnt");
    PRUEF_ALLOC_NULL(moo_bool(true), "alloc falscher Tag abgelehnt");
#undef PRUEF_ALLOC_NULL
    CHECK((uint64_t)kern_as_double(kern_speicher_belegt()) == belegt_vor,
          "ungueltige Wrapper-Args aendern Heap nicht");
    CHECK(kern_frei(moo_number(NAN)).data == 0u, "free NaN abgelehnt");
    CHECK(kern_frei(moo_number(INFINITY)).data == 0u, "free +Inf abgelehnt");
    CHECK(kern_frei(moo_number(-1.0)).data == 0u, "free negativ abgelehnt");
    CHECK(kern_frei(moo_number(1.5)).data == 0u, "free fraktional abgelehnt");
    CHECK(kern_frei(moo_number(9007199254740994.0)).data == 0u,
          "free >2^53 abgelehnt");
    CHECK(kern_frei(moo_bool(true)).data == 0u, "free falscher Tag abgelehnt");
    CHECK(kern_frei(moo_number(0.0)).data == 0u,
          "kern_frei(0): falsch, keine Panic");

    {
        MooValue adr = kern_alloc(moo_number(24.0));
        CHECK(adr.tag == MOO_NUMBER && kern_as_double(adr) != 0.0,
              "kern_alloc-Wrapper liefert exakte Adresse");
        CHECK(kern_frei(adr).tag == MOO_BOOL,
              "kern_frei-Wrapper akzeptiert Roundtrip");
    }
    CHECK(kern_as_double(kern_speicher_belegt()) == 96.0,
          "Wrapper-Roundtrip neutral");

    CHECK(kern_alloc_c((uint64_t)ARENA_GROESSE * 2u) == NULL,
          "OOM liefert 0, keine Panic");
    CHECK(kern_as_double(kern_alloc(
              moo_number((double)ARENA_GROESSE * 2.0))) == 0.0,
          "OOM via Wrapper: Adresse 0");

    /* --- Doppel-free ----------------------------------------------------- */
    kern_frei_c(p1);
    g_panic_armed = 1;
    g_panic_hit = 0;
    if (setjmp(g_panic_jmp) == 0) {
        kern_frei_c(p1);
        CHECK(0, "Doppel-free haette kern_panic ausloesen muessen");
    }
    CHECK(g_panic_hit == 1, "Doppel-free -> kern_panic");
    CHECK(strstr(g_panic_msg, "Doppel") != NULL,
          "Panic-Meldung nennt Doppel-free");
    g_panic_armed = 0;

    /* --- Exakt passende Mini-Region: kein Bump-Additionswrap ------------ */
    kern_heap_init_c(start, start + HDR + 16u);
    p1 = kern_alloc_c(16u);
    CHECK(p1 != NULL, "exakte Header+16-Region liefert einen Block");
    CHECK(kern_alloc_c(1u) == NULL, "exakte Mini-Region danach voll");
    kern_frei_c(p1);

    /* --- Wrapper-Heapinit: invalid fail-closed; Pointer <=2^53 ---------- */
    kern_speicher_init(moo_number(NAN), moo_number((double)ende));
    CHECK(kern_alloc_c(1u) == NULL, "heap-init NaN deaktiviert Heap");
    kern_speicher_init(moo_number(INFINITY), moo_number((double)ende));
    CHECK(kern_alloc_c(1u) == NULL, "heap-init +Inf deaktiviert Heap");
    kern_speicher_init(moo_number(-1.0), moo_number((double)ende));
    CHECK(kern_alloc_c(1u) == NULL, "heap-init negativ deaktiviert Heap");
    kern_speicher_init(moo_number((double)start), moo_number(1.5));
    CHECK(kern_alloc_c(1u) == NULL, "heap-init fraktional deaktiviert Heap");
    kern_speicher_init(moo_number((double)start),
                       moo_number(9007199254740994.0));
    CHECK(kern_alloc_c(1u) == NULL, "heap-init >2^53 deaktiviert Heap");
    kern_speicher_init(moo_bool(true), moo_number((double)ende));
    CHECK(kern_alloc_c(1u) == NULL, "heap-init falscher Tag deaktiviert Heap");

    if ((uint64_t)start <= UINT64_C(9007199254740992) &&
        (uint64_t)ende <= UINT64_C(9007199254740992)) {
        kern_speicher_init(moo_number((double)start), moo_number((double)ende));
        p1 = kern_alloc_c(1u);
        CHECK(p1 != NULL, "gueltiger Wrapper-Heapinit bleibt nutzbar");
    }

    /* --- Re-Init + freie-Bytes-Plausibilitaet --------------------------- */
    kern_heap_init_c(start, ende);
    CHECK(kern_as_double(kern_speicher_belegt()) == 0.0,
          "Re-Init: belegt = 0");
    CHECK(kern_as_double(kern_speicher_peak()) == 0.0,
          "Re-Init: peak = 0");
    {
        double frei_bytes = kern_as_double(kern_speicher_frei());
        CHECK(frei_bytes > 0.0 && frei_bytes <= (double)ARENA_GROESSE,
              "speicher_frei plausibel");
    }

    kern_heap_init_c(0u, 0u);
    free(arena);
}

/* ======================================================================
 * main
 * ====================================================================== */
int main(void) {
    teste_formatter();
    teste_allocator();

    printf("test_bare_alloc_asan: %d Checks, %d Fehler\n", g_checks, g_fails);
    return (g_fails == 0) ? 0 : 1;
}

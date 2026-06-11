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
    CHECK(arena != NULL, "Arena-malloc");
    if (!arena) return;
    memset(arena, 0, ARENA_GROESSE);   /* deterministisch, garantiert kein Zufalls-Magic */

    uintptr_t start = (uintptr_t)arena;
    uintptr_t ende  = start + ARENA_GROESSE;
    kern_heap_init_c(start, ende);

    /* --- Basis: Alignment + Buchfuehrung ------------------------------- */
    void* p1 = kern_alloc_c(1);      /* need -> 16 */
    void* p2 = kern_alloc_c(100);    /* need -> 112 */
    CHECK(p1 && p2, "alloc(1)/alloc(100) liefern Speicher");
    CHECK(((uintptr_t)p1 % 16u) == 0u, "p1 16-Byte-aligned");
    CHECK(((uintptr_t)p2 % 16u) == 0u, "p2 16-Byte-aligned");
    CHECK((uintptr_t)p1 >= start && (uintptr_t)p2 < ende, "Pointer in der Arena");
    if (p1) memset(p1, 0xAA, 1);
    if (p2) memset(p2, 0xBB, 100);   /* ASan: darf nicht ueber die Arena hinaus */

    MooValue belegt = kern_speicher_belegt();
    MooValue peak   = kern_speicher_peak();
    CHECK(kern_as_double(belegt) == 128.0, "belegt = 16 + 112");
    CHECK(kern_as_double(peak)   == 128.0, "peak folgt belegt");

    /* Header-Layout-Annahme absichern (Split-Adressrechnung unten). */
    CHECK((uintptr_t)p2 - (uintptr_t)p1 == 16u + HDR, "Blocklayout: payload+hdr-Abstand");

    /* --- Free-List: first-fit + Split ----------------------------------- */
    kern_frei_c(p2);
    CHECK(kern_as_double(kern_speicher_belegt()) == 16.0, "frei(p2) bucht 112 aus");

    void* p3 = kern_alloc_c(50);     /* need 64; Split: 112 -> 64 + (HDR+16) */
    CHECK(p3 == p2, "first-fit reuse: p3 nimmt p2-Block");
    CHECK(kern_as_double(kern_speicher_belegt()) == 80.0, "belegt = 16 + 64");

    void* p4 = kern_alloc_c(10);     /* need 16; exakt der Split-Rest */
    CHECK(p4 == (void*)((unsigned char*)p3 + 64u + HDR), "Split-Rest wird wiederverwendet");
    CHECK(kern_as_double(kern_speicher_belegt()) == 96.0, "belegt = 16 + 64 + 16");
    CHECK(kern_as_double(kern_speicher_peak()) == 128.0, "peak bleibt high-water");

    /* --- MooValue-Wrapper-Roundtrip ------------------------------------- */
    MooValue adr = kern_alloc(moo_number(24.0));     /* need 32, Bump */
    CHECK(adr.tag == MOO_NUMBER && kern_as_double(adr) != 0.0, "kern_alloc-Wrapper liefert Adresse");
    MooValue frei_ok = kern_frei(adr);
    CHECK(frei_ok.tag == MOO_BOOL && frei_ok.data == 1u, "kern_frei-Wrapper: wahr");
    MooValue frei_null = kern_frei(moo_number(0.0));
    CHECK(frei_null.tag == MOO_BOOL && frei_null.data == 0u, "kern_frei(0): falsch, keine Panic");
    CHECK(kern_as_double(kern_speicher_belegt()) == 96.0, "Wrapper-Roundtrip neutral");

    /* --- OOM -------------------------------------------------------------- */
    void* zu_gross = kern_alloc_c((uint64_t)ARENA_GROESSE * 2u);
    CHECK(zu_gross == NULL, "OOM liefert 0, keine Panic");
    MooValue oom = kern_alloc(moo_number((double)ARENA_GROESSE * 2.0));
    CHECK(kern_as_double(oom) == 0.0, "OOM via Wrapper: Adresse 0");

    /* --- Doppel-free => kern_panic --------------------------------------- */
    kern_frei_c(p1);                       /* legitim */
    g_panic_armed = 1; g_panic_hit = 0;
    if (setjmp(g_panic_jmp) == 0) {
        kern_frei_c(p1);                   /* MUSS panicen */
        CHECK(0, "Doppel-free haette kern_panic ausloesen muessen");
    }
    CHECK(g_panic_hit == 1, "Doppel-free -> kern_panic");
    CHECK(strstr(g_panic_msg, "Doppel") != NULL, "Panic-Meldung nennt Doppel-free");

    /* --- Invalid free (Magic-Mismatch) => kern_panic ---------------------- */
    g_panic_hit = 0;
    if (setjmp(g_panic_jmp) == 0) {
        kern_frei_c((void*)(arena + ARENA_GROESSE / 2u));   /* genullter Bereich */
        CHECK(0, "Invalid free haette kern_panic ausloesen muessen");
    }
    CHECK(g_panic_hit == 1, "Magic-Mismatch -> kern_panic");
    CHECK(strstr(g_panic_msg, "Magic") != NULL, "Panic-Meldung nennt Magic-Mismatch");
    g_panic_armed = 0;

    /* --- Re-Init (idempotent) + freie-Bytes-Plausibilitaet ---------------- */
    kern_heap_init_c(start, ende);
    CHECK(kern_as_double(kern_speicher_belegt()) == 0.0, "Re-Init: belegt = 0");
    CHECK(kern_as_double(kern_speicher_peak())   == 0.0, "Re-Init: peak = 0");
    double frei_bytes = kern_as_double(kern_speicher_frei());
    CHECK(frei_bytes > 0.0 && frei_bytes <= (double)ARENA_GROESSE,
          "speicher_frei plausibel (0 < frei <= Arena)");

    /* Aufgeraeumt: Arena freigeben — Allocator-Zustand zeigt danach ins Leere,
     * wird aber nicht mehr benutzt. Fuer ASan-Leak-Check Pflicht. */
    kern_heap_init_c(0, 0);
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

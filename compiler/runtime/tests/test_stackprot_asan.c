/* test_stackprot_asan.c — P012-A4: Stack-Protector-Mechanik auf dem Host.
 *
 * Testet die Fail-Bruecke __stack_chk_fail -> kern_panic (longjmp-Stub).
 * Der ECHTE Canary-Kipp-Beweis laeuft im QEMU-Smoke (scripts/sp-smoke.sh):
 * hosted instrumentiert der Distro-SSP gegen den TLS-Canary (%fs:0x28),
 * nicht gegen unser globales Symbol — ein deterministischer Fail ist hier
 * nicht erzwingbar. kern_stackprot_selbsttest wird trotzdem (gearmt)
 * aufgerufen: beide Ausgaenge (Rueckkehr ODER Panic) sind hosted legal,
 * Hauptsache kein Crash und kein Sanitizer-Befund.
 *
 * Link: test_stackprot_asan.c moo_bare_stackprot.c.
 * Stubs: kern_panic (longjmp), kern_seriell_text (no-op), moo_none.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include "moo_bare_kern.h"

static int g_checks = 0;
static int g_fails  = 0;

#define CHECK(cond, msg) do {                                              \
    g_checks++;                                                            \
    if (!(cond)) {                                                         \
        g_fails++;                                                         \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));    \
    }                                                                      \
} while (0)

/* ---- Stubs ---- */
static jmp_buf g_panic_jmp;
static volatile int g_panic_armed = 0;
static volatile int g_panic_hit   = 0;
static char g_panic_msg[256];

void kern_panic(const char* msg) {
    g_panic_hit = 1;
    snprintf(g_panic_msg, sizeof(g_panic_msg), "%s", msg ? msg : "");
    if (g_panic_armed) longjmp(g_panic_jmp, 1);
    fprintf(stderr, "kern_panic ohne Arm: %s\n", g_panic_msg);
    abort();
}

void kern_seriell_text(const char* s) { (void)s; }

MooValue moo_none(void) { MooValue v = { 3, 0 }; return v; }

int main(void) {
    /* (1) Fail-Bruecke: __stack_chk_fail muss kern_panic mit klarer
     * Meldung erreichen, nicht still verschwinden. */
    g_panic_hit = 0;
    g_panic_armed = 1;
    if (setjmp(g_panic_jmp) == 0) {
        __stack_chk_fail();
        CHECK(0, "__stack_chk_fail kehrte zurueck (noreturn verletzt)");
    }
    g_panic_armed = 0;
    CHECK(g_panic_hit == 1, "kern_panic wurde nicht erreicht");
    CHECK(strstr(g_panic_msg, "STACK-PROTECTOR") != NULL,
          "Panic-Meldung nennt STACK-PROTECTOR nicht");

    /* (2) Selbsttest hosted: Rueckkehr ODER Panic sind beide legal
     * (TLS- vs global-Guard, siehe Kopf). Nur kein Crash. */
    g_panic_hit = 0;
    g_panic_armed = 1;
    if (setjmp(g_panic_jmp) == 0) {
        (void)kern_stackprot_selbsttest();
    }
    g_panic_armed = 0;
    CHECK(1, "Selbsttest hosted ueberlebt");

    printf("test_stackprot: %d checks, %d fails\n", g_checks, g_fails);
    return g_fails == 0 ? 0 : 1;
}

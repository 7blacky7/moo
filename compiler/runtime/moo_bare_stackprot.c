/**
 * moo_bare_stackprot.c — Freestanding Stack-Protector (P012-A4).
 *
 * Die Kernel-C-Runtime wird (sofern der C-Compiler es traegt) mit
 * -fstack-protector-strong -mstack-protector-guard=global uebersetzt:
 * der Canary kommt aus dem GLOBALEN Symbol __stack_chk_guard statt aus
 * TLS (%fs:0x28). Im Kernel ist kein TLS/FS aufgesetzt — der
 * glibc-TLS-Default wuerde Garbage lesen und sofort falsch feuern.
 *
 * GUARD-POLICY: statisch initialisierte Compile-Zeit-Konstante mit
 * Null-Byte im LSB (Terminator-Canary gegen str*-Overruns). BEWUSST
 * KEIN Laufzeit-Re-Seed: bereits aktive Frames tragen den alten Wert
 * auf dem Stack — ein Re-Seed wuerde deren Epilog-Check sprengen.
 * Ziel ist Bug-DETEKTION (lauter Panic statt Silent Corruption), nicht
 * Exploit-Mitigation gegen informierte Angreifer.
 *
 * ISR-Vertraeglichkeit: SSP-Prolog/Epilog ist reine Integer-Arithmetik
 * — kollidiert nicht mit target("general-regs-only") der ISRs. Die
 * asm-Entry-Pfade (.text.boot, globales asm) sind kein C und bleiben
 * uninstrumentiert.
 */
#include "moo_bare_kern.h"

/* Terminator-Canary: Null-Byte im LSB. uintptr_t traegt 32/64-bit. */
uintptr_t __stack_chk_guard = (uintptr_t)0xC0FFEE0DEFACED00ull;

void __stack_chk_fail(void) {
    kern_panic("STACK-PROTECTOR: Canary verletzt (#SSP)");
}

/* Hilfsfunktion mit erzwungener Instrumentierung: lokaler char-Puffer
 * laesst -fstack-protector-strong sie sicher instrumentieren. noinline,
 * damit der Epilog-Check garantiert in DIESER Funktion liegt. Der Body
 * kippt den GLOBALEN Guard zwischen Prolog (alter Wert liegt im
 * Stack-Slot) und Epilog (Vergleich gegen den NEUEN globalen Wert)
 * -> deterministischer Fail -> __stack_chk_fail -> kern_panic. */
__attribute__((noinline))
static void stackprot_provoziere(void) {
    volatile char puffer[16];
    for (int i = 0; i < 16; ++i) puffer[i] = (char)i;
    __stack_chk_guard ^= (uintptr_t)0xA5A5A5A5A5A5A500ull;
    (void)puffer[0];
}

/* moo-Builtin (require_bare, 0-arg): provoziert den Canary-Fail fuer
 * den QEMU-Fail-Smoke (scripts/sp-smoke.sh). Kehrt bei aktivem SSP NIE
 * zurueck. Faellt der Build auf -fno-stack-protector zurueck (cc ohne
 * -mstack-protector-guard=global), kommen wir zurueck und melden das
 * ehrlich — der Smoke wertet das als transparenten Skip. */
MooValue kern_stackprot_selbsttest(void) {
    stackprot_provoziere();
    kern_seriell_text("\n[KERN] SSP-SELBSTTEST: kein Canary-Fail (SSP inaktiv?)\n");
    return moo_none();
}

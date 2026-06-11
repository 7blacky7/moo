/**
 * moo_bare_kern.h — Gemeinsamer Header fuer den kern_*-Namensraum (Plan-010).
 *
 * Kernel-Kern-API fuer den Bare-Metal-Pfad von moo. Wird NUR im
 * --no-stdlib / *-bare-Build mitkompiliert (siehe build.rs ist hier NICHT
 * zustaendig — die moo_bare_*.c werden von der CLI-Kernel-Pipeline, P010-C2,
 * separat mit -ffreestanding -nostdlib uebersetzt).
 *
 * STRENGE (GPT-Review 2026-04-13, Thoughts 501192c2 + b740a19f):
 *   - Harte Trennung hosted vs bare: dieser Header zieht KEINE libc, kein
 *     malloc/printf/stdlib. Nur <stdint.h>/<stdbool.h> (freestanding-erlaubt).
 *   - UB-Policy (Memory ub-arithmetik-policy, Plan-007): absichtlicher Wrap nur
 *     ueber unsigned (uint32_t/uint64_t), KEINE signed shifts, KEIN -fwrapv.
 *   - MooValue ist bit-identisch zur Voll-Runtime (16 Byte { tag, data }), damit
 *     der LLVM-Codegen dieselben { i64, i64 }-Calls erzeugt.
 */
#ifndef MOO_BARE_KERN_H
#define MOO_BARE_KERN_H

#include <stdint.h>
#include <stdbool.h>

/* MOO_BARE markiert den freestanding-Build. moo_bare.c und alle moo_bare_*.c
 * werden mit -DMOO_BARE uebersetzt (P010-C2). Host-Test-Harnesses definieren es
 * NICHT und liefern eigene Stubs. */

/* === MooValue — bit-identisch zu compiler/runtime/moo_runtime.h === */
typedef enum {
    MOO_NUMBER = 0,
    MOO_BOOL   = 2,
    MOO_NONE   = 3,
} BareTag;

typedef struct {
    uint64_t tag;
    uint64_t data;
} MooValue;

/* === Wert-Helper (inline, kein libc) === */
static inline double kern_as_double(MooValue v) {
    double d;
    __builtin_memcpy(&d, &v.data, sizeof(double));
    return d;
}
static inline void kern_set_double(MooValue* v, double d) {
    __builtin_memcpy(&v->data, &d, sizeof(double));
}

/* In moo_bare.c definiert (Konstruktoren der Kern-Runtime). */
MooValue moo_number(double n);
MooValue moo_bool(bool b);
MooValue moo_none(void);

/* Port-I/O + CPU-Primitive (moo_bare.c). Hier nur deklariert, damit die
 * kern_*-Module sie nutzen koennen ohne moo_bare.c zu includen. */
MooValue moo_io_inb(MooValue port);
void     moo_io_outb(MooValue port, MooValue data);
void     moo_cpu_halt(void);
void     moo_cpu_cli(void);
void     moo_cpu_sti(void);

/* C-seitige Port-Helfer (ohne MooValue-Boxing) — in moo_bare_console.c, von
 * allen kern_*-Modulen genutzt. */
uint8_t kern_inb(uint16_t port);
void    kern_outb(uint16_t port, uint8_t value);
// P011-B1: 16/32-bit Port-I/O
uint16_t kern_inw(uint16_t port);
void     kern_outw(uint16_t port, uint16_t value);
uint32_t kern_inl(uint16_t port);
void     kern_outl(uint16_t port, uint32_t value);
MooValue moo_io_inw(MooValue port);
void     moo_io_outw(MooValue port, MooValue data);
MooValue moo_io_inl(MooValue port);
void     moo_io_outl(MooValue port, MooValue data);

/* ======================================================================
 * K2 — Early-Console (moo_bare_console.c)
 * ====================================================================== */

/* Serielle Konsole (16550-UART, COM1 = 0x3F8), 115200 8N1. */
MooValue kern_seriell_init(void);                 /* returns moo_bool(loopback ok) */
void     kern_seriell_zeichen_c(char c);          /* C-Seite */
void     kern_seriell_text(const char* s);        /* C-String, NUL-terminiert */
void     kern_seriell_dez_u64(uint64_t n);        /* dezimal, ohne libc */
void     kern_seriell_hex_u64(uint64_t n);        /* "0x..." 16 Nibbles */

/* moo-Builtin-Wrapper (MooValue-Signaturen, vom Codegen aufgerufen). */
MooValue kern_seriell_zeichen(MooValue ascii);
MooValue kern_seriell_dez(MooValue n);
MooValue kern_seriell_hex(MooValue n);

/* VGA-Textmode (0xB8000, 80x25). */
MooValue kern_vga_init(void);
MooValue kern_vga_farbe(MooValue farbe);          /* Attribut-Byte 0..255 */
MooValue kern_vga_zeichen(MooValue ascii);        /* an Cursor, mit Scroll */
void     kern_vga_text(const char* s);            /* C-Seite */

/* Starke moo_print (ueberschreibt das weak-Stub in moo_bare.c). */
void moo_print(MooValue v);

/* ======================================================================
 * K3 — Bare-Allocator (moo_bare_alloc.c)
 * ====================================================================== */

/* Initialisiert die Heap-Region [start, ende). Idempotent ueberschreibbar. */
MooValue kern_speicher_init(MooValue start, MooValue ende);
MooValue kern_alloc(MooValue groesse);            /* -> Adresse als Zahl, 0 = OOM */
MooValue kern_frei(MooValue adresse);             /* -> moo_bool(ok) */
MooValue kern_speicher_frei(void);                /* freie Bytes */
MooValue kern_speicher_belegt(void);              /* belegte Bytes */
MooValue kern_speicher_peak(void);                /* Peak-belegt (high-water) */

/* C-Seite (vom Boot-Trampolin genutzt). */
void  kern_heap_init_c(uintptr_t start, uintptr_t ende);
void* kern_alloc_c(uint64_t n);
void  kern_frei_c(void* p);

/* ======================================================================
 * K4 — Interrupt-Infrastruktur (moo_bare_idt.c)
 * ====================================================================== */

MooValue kern_idt_init(void);                     /* IDT laden, Default-ISRs */
MooValue kern_pic_remap(MooValue offset1, MooValue offset2);
MooValue kern_pic_maskiere(MooValue irq);
MooValue kern_pic_demaskiere(MooValue irq);
MooValue kern_pic_eoi(MooValue irq);
MooValue kern_timer_init(MooValue hz);            /* PIT Kanal 0, Mode 3 */
MooValue kern_ticks(void);                        /* aktueller Tick-Zaehler */

/* ======================================================================
 * K5 — Boot (moo_bare_boot.c)
 * ====================================================================== */

/* Wird vom asm-Trampolin nach Long-Mode-Switch aufgerufen. Ruft seriell_init,
 * speicher_init (Linker-Symbole) und main (moo-Entry). */
void kern_boot_main(void);

/* Vom moo-Programm via main() definiert (der Codegen erzeugt diese).
 * NUR im bare-Build deklariert: Host-Harnesses (ohne -DMOO_BARE) definieren
 * ihr eigenes hosted int main(void) — die freestanding void-Signatur wuerde
 * dort als widerspruechliche Deklaration kollidieren. */
#ifdef MOO_BARE
void main(void);
#endif

/* Panik-Pfad: seriell-Meldung + halt-Loop. Genutzt von alloc/idt. */
void kern_panic(const char* msg) __attribute__((noreturn));

#endif /* MOO_BARE_KERN_H */

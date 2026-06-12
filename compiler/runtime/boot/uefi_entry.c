/* P011-F1: UEFI-Entry fuer moo-UEFI-Apps (aus E1-PoC reproduzierbar).
 *
 * efi_main(ImageHandle, SystemTable) ist der UEFI-Standard-Entry. Beim
 * Triple x86_64-unknown-windows / -uefi ist die MS-x64-Calling-Convention
 * der Default fuer ALLE Funktionen — kein per-Funktion-CC-Hack noetig.
 *
 * AUSGABE: Wir gehen NICHT ueber SystemTable->ConOut (UTF-16-Methodenaufruf),
 * sondern direkt ueber COM1 (Port-I/O, in UEFI Ring 0 erlaubt) + den QEMU
 * debugcon-Port 0xE9. Letzterer ist der zuverlaessigste Beweiskanal: OVMF
 * initialisiert COM1 selbst, weshalb ein Byte ohne LSR-Polling verloren geht.
 *
 * SSE: Im UEFI-Boot-Services-Environment bereits aktiv (Firmware nutzt es) —
 * anders als beim bare-Stage2-Pfad (D1), wo SSE erst manuell an muss.
 */
typedef unsigned long long u64;
typedef unsigned short u16;
typedef unsigned char u8;

extern void moo_efi_haupt(void);

static void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }

/* COM1 mit LSR-Polling (0x3FD Bit5 = THR leer) — ohne Polling Byte-Verlust. */
static void com1(u8 c) { while (!(inb(0x3FD) & 0x20)) {} outb(0x3F8, c); }

/* Marker auf BEIDE Kanaele: debugcon 0xE9 (QEMU-stdio) + COM1 (-serial). */
static void marker(const char *s) {
    for (const char *p = s; *p; ++p) { outb(0xE9, (u8)*p); com1((u8)*p); }
}

u64 efi_main(void *ImageHandle, void *SystemTable) {
    (void)ImageHandle; (void)SystemTable;
    marker("MOO-UEFI-ENTRY\n");
    moo_efi_haupt();            /* in moo kompilierter Code */
    marker("MOO-UEFI-OK\n");    /* nach moo -> beweist saubere Rueckkehr */
    for (;;) { __asm__ volatile("hlt"); }
    return 0;
}

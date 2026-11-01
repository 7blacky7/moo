/* P7-T9 Testeinstieg (nur Beweis-Harness, kein Produktcode): ruft den SNP-Adapter
 * compiler/runtime/boot/uefi_net.c auf und sendet eine Zeile. Marker auf
 * debugcon 0xE9 und COM1, damit QEMU-Logs den Ablauf zeigen. */
typedef unsigned long long u64;
typedef unsigned char u8;

int moo_uefi_net_init(void *system_table);
int moo_uefi_net_sende(const u8 *text, u64 len);

static void outb(unsigned short p, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static unsigned char inb(unsigned short p) { unsigned char v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static void marker(const char *s) {
    for (const char *p = s; *p; ++p) {
        outb(0xE9, (u8)*p);
        while (!(inb(0x3FD) & 0x20)) {}
        outb(0x3F8, (u8)*p);
    }
}

/* uefi_net.c nutzt kern_netz_status/moo_string_new; der Beweis-Einstieg braucht nur
 * das Symbol fuer den Linker (kein Aufruf). */
typedef struct { unsigned long long tag; unsigned long long data; } MooValueStub;
MooValueStub moo_string_new(const char *chars) { MooValueStub v = {0, 0}; (void)chars; return v; }

static const u8 zeile[] = "MOO-NET-HELLO P7-T9 Zeile 1\n";

u64 efi_main(void *ImageHandle, void *SystemTable) {
    (void)ImageHandle;
    marker("MOO-NET-ENTRY\n");
    if (!moo_uefi_net_init(SystemTable)) { marker("MOO-NET-INIT-FAIL\n"); for (;;) __asm__ volatile("hlt"); }
    marker("MOO-NET-INIT-OK\n");
    if (!moo_uefi_net_sende(zeile, sizeof(zeile) - 1)) { marker("MOO-NET-SEND-FAIL\n"); for (;;) __asm__ volatile("hlt"); }
    marker("MOO-NET-SENT-OK\n");
    for (;;) __asm__ volatile("hlt");
    return 0;
}

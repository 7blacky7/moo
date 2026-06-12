/**
 * moo_bare_console.c — Early-Console fuer den Bare-Metal-Kernel (Plan-010 K2).
 *
 * Zwei Ausgabekanaele, beide ohne libc/malloc/printf:
 *   1. 16550-UART (COM1, 0x3F8) — 115200 8N1, fuer QEMU -serial stdio + echte HW.
 *   2. VGA-Textmode (0xB8000, 80x25) — fuer Bildschirmausgabe.
 *
 * Liefert eine STARKE moo_print (ueberschreibt das weak-Stub in moo_bare.c),
 * sodass `zeige x` im Kernel auf beiden Kanaelen erscheint.
 *
 * UB-Policy (Memory ub-arithmetik-policy): Formatter rechnen in uint64_t,
 * keine signed shifts, kein -fwrapv.
 *
 * x86_64 only fuer die Port-/MMIO-Pfade. aarch64/riscv64: compile-only Stubs.
 */
#include "moo_bare_kern.h"

#define UART_COM1   0x3F8
/* Registeroffsets relativ zur Basis */
#define UART_DATA   0  /* DLAB=0: RX/TX */
#define UART_IER    1  /* DLAB=0: Interrupt Enable; DLAB=1: Divisor high */
#define UART_DLL    0  /* DLAB=1: Divisor low */
#define UART_FCR    2  /* FIFO Control (write) / IIR (read) */
#define UART_LCR    3  /* Line Control */
#define UART_MCR    4  /* Modem Control */
#define UART_LSR    5  /* Line Status */

/* ======================================================================
 * 16550-UART (COM1)
 * ====================================================================== */

#if defined(__x86_64__) || defined(__i386__)

static bool g_uart_bereit = false;

static inline int uart_kann_senden(void) {
    /* LSR Bit 5 (THR empty) */
    return (kern_inb(UART_COM1 + UART_LSR) & 0x20) != 0;
}

MooValue kern_seriell_init(void) {
    /* 115200 baud, 8N1, FIFO an, Loopback-Selbsttest. */
    kern_outb(UART_COM1 + UART_IER, 0x00);   /* Interrupts aus */
    kern_outb(UART_COM1 + UART_LCR, 0x80);   /* DLAB = 1 */
    kern_outb(UART_COM1 + UART_DLL, 0x01);   /* Divisor lo = 1 -> 115200 */
    kern_outb(UART_COM1 + UART_IER, 0x00);   /* Divisor hi = 0 */
    kern_outb(UART_COM1 + UART_LCR, 0x03);   /* DLAB = 0, 8 bits, no parity, 1 stop */
    kern_outb(UART_COM1 + UART_FCR, 0xC7);   /* FIFO an, clear, 14-byte threshold */
    kern_outb(UART_COM1 + UART_MCR, 0x0B);   /* RTS/DSR/OUT2 gesetzt */

    /* Loopback-Selbsttest: 0xAE senden, zurueck erwarten. */
    kern_outb(UART_COM1 + UART_MCR, 0x1E);   /* Loopback an + RTS/OUT1/OUT2 */
    kern_outb(UART_COM1 + UART_DATA, 0xAE);
    uint8_t echo = kern_inb(UART_COM1 + UART_DATA);
    bool ok = (echo == 0xAE);

    /* Normalbetrieb (Loopback aus). */
    kern_outb(UART_COM1 + UART_MCR, 0x0F);   /* DTR/RTS/OUT1/OUT2 */
    g_uart_bereit = ok;
    return moo_bool(ok);
}

void kern_seriell_zeichen_c(char c) {
    if (!g_uart_bereit) return;
    /* CR vor LF fuer saubere Terminal-Darstellung. */
    if (c == '\n') {
        while (!uart_kann_senden()) { }
        kern_outb(UART_COM1 + UART_DATA, '\r');
    }
    unsigned spin = 0;
    while (!uart_kann_senden()) {
        /* Watchdog: nie ewig blockieren, falls UART tot ist. */
        if (++spin > 1000000u) return;
    }
    kern_outb(UART_COM1 + UART_DATA, (uint8_t)c);
}

#elif defined(MOO_BOARD_UART_PL011)

/* ======================================================================
 * PL011-UART (P012-D1) — qemu-virt-aarch64, Basis aus Board-Define.
 * Quellen: ARM PL011 TRM (DDI 0183) + Board-Profil qemu-virt-aarch64
 * (boards.rs: 0x09000000, QEMU hw/arm/virt.c-Memmap). Zugriff strikt
 * volatile MMIO via kern_mmio_* (P012-B3) — KEIN x86-Port-I/O, das ist
 * auf ARM ein no-op-Stub (moo_bare.c).
 * ====================================================================== */

#ifndef MOO_BOARD_UART_BASE
#error "MOO_BOARD_UART_PL011 gesetzt, aber MOO_BOARD_UART_BASE fehlt — Build via --board (P012-C1)"
#endif

/* PL011-Registeroffsets (DDI 0183). */
#define PL011_DR     0x00u  /* Data */
#define PL011_FR     0x18u  /* Flags: Bit3 BUSY, Bit5 TXFF (TX-FIFO voll) */
#define PL011_LCR_H  0x2Cu  /* Bit4 FEN (FIFO), Bits 6:5 WLEN=11 (8 Bit) -> 0x70 = 8N1+FIFO */
#define PL011_CR     0x30u  /* Bit0 UARTEN, Bit8 TXE, Bit9 RXE */
#define PL011_IMSC   0x38u  /* Interrupt-Maske */
#define PL011_ICR    0x44u  /* Interrupt-Clear (write 0x7FF = alle) */

static uintptr_t g_pl011_base = (uintptr_t)MOO_BOARD_UART_BASE;
static bool g_uart_bereit = false;

/* Init auf expliziter Basis (Task-API P012-D1). QEMUs virt-PL011 ist
 * nach Reset bereits sendefaehig; das Init ist trotzdem vollstaendig:
 * disable -> IRQs maskieren/loeschen -> 8N1+FIFO -> UARTEN|TXE|RXE.
 * Baudrate (IBRD/FBRD) bleibt bewusst auf dem QEMU-Default — echte
 * Hardware (H1/Raspi4) braucht clock-abhaengige Divisoren (dann hier
 * ergaenzen, Quelle: DDI 0183 Kap. 3.3.6). */
MooValue kern_seriell_init_addr(uintptr_t base) {
    g_pl011_base = base;
    kern_mmio_write32(base + PL011_CR, 0u);            /* UART aus */
    kern_mmio_write32(base + PL011_IMSC, 0u);          /* IRQs maskieren */
    kern_mmio_write32(base + PL011_ICR, 0x7FFu);       /* pending loeschen */
    kern_mmio_write32(base + PL011_LCR_H, 0x70u);      /* 8N1 + FIFO */
    kern_mmio_write32(base + PL011_CR, (1u << 0) | (1u << 8) | (1u << 9));
    g_uart_bereit = true;
    return moo_bool(true);
}

MooValue kern_seriell_init(void) {
    return kern_seriell_init_addr((uintptr_t)MOO_BOARD_UART_BASE);
}

void kern_seriell_zeichen_c(char c) {
    if (!g_uart_bereit) return;
    /* CR vor LF fuer saubere Terminal-Darstellung (wie 16550-Pfad). */
    if (c == '\n') {
        unsigned s0 = 0;
        while (kern_mmio_read32(g_pl011_base + PL011_FR) & (1u << 5)) {
            if (++s0 > 1000000u) return;   /* Watchdog: nie ewig blockieren */
        }
        kern_mmio_write32(g_pl011_base + PL011_DR, (uint32_t)'\r');
    }
    unsigned spin = 0;
    while (kern_mmio_read32(g_pl011_base + PL011_FR) & (1u << 5)) {  /* TXFF */
        if (++spin > 1000000u) return;
    }
    kern_mmio_write32(g_pl011_base + PL011_DR, (uint32_t)(uint8_t)c);
}

#else  /* aarch64/riscv64 ohne Board-UART: compile-only Stubs */

MooValue kern_seriell_init(void) { return moo_bool(false); }
void kern_seriell_zeichen_c(char c) { (void)c; }

#endif

void kern_seriell_text(const char* s) {
    if (!s) return;
    for (const char* p = s; *p; ++p) kern_seriell_zeichen_c(*p);
}

/* Dezimal-Formatter ohne libc. Wert wird in uint64_t verarbeitet (UB-frei). */
void kern_seriell_dez_u64(uint64_t n) {
    char buf[20];          /* max 20 Stellen fuer 2^64-1 */
    int i = 0;
    if (n == 0) { kern_seriell_zeichen_c('0'); return; }
    while (n > 0 && i < 20) {
        buf[i++] = (char)('0' + (int)(n % 10u));
        n /= 10u;
    }
    while (i > 0) kern_seriell_zeichen_c(buf[--i]);
}

/* Hex-Formatter "0x...." mit 16 Nibbles. Rein unsigned. */
void kern_seriell_hex_u64(uint64_t n) {
    static const char hexd[] = "0123456789abcdef";
    kern_seriell_zeichen_c('0');
    kern_seriell_zeichen_c('x');
    for (int shift = 60; shift >= 0; shift -= 4) {
        uint64_t nib = (n >> (unsigned)shift) & 0xFu;
        kern_seriell_zeichen_c(hexd[nib]);
    }
}

/* ======================================================================
 * VGA-Textmode (0xB8000)
 * ====================================================================== */

#define VGA_BASE   0xB8000
#define VGA_COLS   80
#define VGA_ROWS   25

static volatile uint16_t* const g_vga = (volatile uint16_t*)VGA_BASE;
static int     g_vga_x = 0;
static int     g_vga_y = 0;
static uint8_t g_vga_attr = 0x07;  /* hellgrau auf schwarz */

#if defined(__x86_64__) || defined(__i386__)

static void vga_cursor_aus(void) {
    /* CRTC: Cursor Start Register (0x0A), Bit 5 = Cursor disable. */
    kern_outb(0x3D4, 0x0A);
    kern_outb(0x3D5, 0x20);
}

static void vga_scroll(void) {
    /* Eine Zeile hoch kopieren, letzte Zeile leeren. */
    for (int row = 1; row < VGA_ROWS; ++row) {
        for (int col = 0; col < VGA_COLS; ++col) {
            g_vga[(row - 1) * VGA_COLS + col] = g_vga[row * VGA_COLS + col];
        }
    }
    uint16_t leer = (uint16_t)(' ' | ((uint16_t)g_vga_attr << 8));
    for (int col = 0; col < VGA_COLS; ++col) {
        g_vga[(VGA_ROWS - 1) * VGA_COLS + col] = leer;
    }
    g_vga_y = VGA_ROWS - 1;
}

MooValue kern_vga_init(void) {
    g_vga_attr = 0x07;
    uint16_t leer = (uint16_t)(' ' | ((uint16_t)g_vga_attr << 8));
    for (int i = 0; i < VGA_COLS * VGA_ROWS; ++i) g_vga[i] = leer;
    g_vga_x = 0;
    g_vga_y = 0;
    vga_cursor_aus();
    return moo_none();
}

static void vga_putc(char c) {
    if (c == '\n') {
        g_vga_x = 0;
        if (++g_vga_y >= VGA_ROWS) vga_scroll();
        return;
    }
    if (c == '\r') { g_vga_x = 0; return; }
    g_vga[g_vga_y * VGA_COLS + g_vga_x] =
        (uint16_t)((uint8_t)c | ((uint16_t)g_vga_attr << 8));
    if (++g_vga_x >= VGA_COLS) {
        g_vga_x = 0;
        if (++g_vga_y >= VGA_ROWS) vga_scroll();
    }
}

#else  /* aarch64/riscv64: kein VGA */

MooValue kern_vga_init(void) { return moo_none(); }
static void vga_putc(char c) { (void)c; }

#endif

MooValue kern_vga_farbe(MooValue farbe) {
    g_vga_attr = (uint8_t)kern_as_double(farbe);
    return moo_none();
}

MooValue kern_vga_zeichen(MooValue ascii) {
    vga_putc((char)(uint8_t)kern_as_double(ascii));
    return moo_none();
}

void kern_vga_text(const char* s) {
    if (!s) return;
    for (const char* p = s; *p; ++p) vga_putc(*p);
}

/* ======================================================================
 * moo-Builtin-Wrapper (MooValue-Signaturen, vom Codegen aufgerufen)
 * ====================================================================== */

MooValue kern_seriell_zeichen(MooValue ascii) {
    kern_seriell_zeichen_c((char)(uint8_t)kern_as_double(ascii));
    return moo_none();
}

MooValue kern_seriell_dez(MooValue n) {
    double d = kern_as_double(n);
    if (d < 0) { kern_seriell_zeichen_c('-'); d = -d; }
    kern_seriell_dez_u64((uint64_t)d);
    return moo_none();
}

MooValue kern_seriell_hex(MooValue n) {
    kern_seriell_hex_u64((uint64_t)(int64_t)kern_as_double(n));
    return moo_none();
}

/* ======================================================================
 * Starke moo_print — ueberschreibt das weak-Stub in moo_bare.c.
 * Gibt auf BEIDEN Kanaelen aus (seriell + VGA), mit Zeilenumbruch.
 * ====================================================================== */
void moo_print(MooValue v) {
    if (v.tag == MOO_NUMBER) {
        double d = kern_as_double(v);
        if (d < 0) { kern_seriell_zeichen_c('-'); vga_putc('-'); d = -d; }
        uint64_t n = (uint64_t)d;
        /* dez auf beide Kanaele */
        char buf[20];
        int i = 0;
        if (n == 0) { kern_seriell_zeichen_c('0'); vga_putc('0'); }
        else {
            while (n > 0 && i < 20) { buf[i++] = (char)('0' + (int)(n % 10u)); n /= 10u; }
            while (i > 0) { char c = buf[--i]; kern_seriell_zeichen_c(c); vga_putc(c); }
        }
    } else if (v.tag == MOO_BOOL) {
        const char* s = v.data ? "wahr" : "falsch";
        kern_seriell_text(s);
        kern_vga_text(s);
    } else {
        const char* s = "nichts";
        kern_seriell_text(s);
        kern_vga_text(s);
    }
    kern_seriell_zeichen_c('\n');
    vga_putc('\n');
}

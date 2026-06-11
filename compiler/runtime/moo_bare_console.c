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

#else  /* compile-only Stubs fuer aarch64/riscv64 */

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

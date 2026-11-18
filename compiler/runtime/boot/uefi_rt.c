/* P011-F1: Minimale moo-Runtime-Stubs fuer UEFI/COFF (aus E1-PoC).
 *
 * Diese Stubs decken genau die Symbole ab, die der moo-Codegen im
 * bare-UEFI-Pfad referenziert (Port-I/O-Builtins + MooValue-Boxing).
 * MooValue ist {i64 tag; i64 data} = 16 Byte, double-Bits liegen in data.
 *
 * Mit clang --target=x86_64-unknown-windows -ffreestanding kompilieren,
 * damit die MS-x64-ABI konsistent zum moo-COFF-Objekt ist.
 *
 * P7-T2 (MOO_UEFI_USE_BARE_RUNTIME, Batch 31603): MooValue-/Port-I/O kommen
 * dann aus moo_bare.c. Diese Datei liefert nur noch die Schicht, die
 * moo_surface.c und der Moo-Codegen zusaetzlich brauchen: Heap-Bruecke auf
 * den Bare-Allocator, String-Konstruktor fuer Literale, libm-Minimum,
 * kern_panic und fail-closed-Stubs fuer nicht genutzte Surface-Pfade.
 */
#ifndef MOO_UEFI_USE_BARE_RUNTIME
#include <stdint.h>
typedef struct { int64_t tag; int64_t data; } MooValue;

/* P7-T19: gemeinsamer tag-kundiger Zahlzugriff; kein Zahlwert -> Trap. */
#include "../moo_bare_wert.h"
static double as_num(MooValue v) { return MOO_WERT_ZAHL(v); }

MooValue moo_number(double n) { union { int64_t i; double d; } u; u.d = n; MooValue v = {0, u.i}; return v; }
MooValue moo_none(void) { MooValue v = {3, 0}; return v; }
void moo_retain(MooValue v) { (void)v; }
void moo_release(MooValue v) { (void)v; }

MooValue moo_io_inw(MooValue port) {
    uint16_t p = (uint16_t)as_num(port), val = 0;
    __asm__ volatile("inw %1,%0" : "=a"(val) : "Nd"(p));
    return moo_number((double)val);
}
void moo_io_outw(MooValue port, MooValue data) {
    uint16_t p = (uint16_t)as_num(port), d = (uint16_t)as_num(data);
    __asm__ volatile("outw %0,%1" :: "a"(d), "Nd"(p));
}
MooValue moo_io_inl(MooValue port) {
    uint16_t p = (uint16_t)as_num(port); uint32_t val = 0;
    __asm__ volatile("inl %1,%0" : "=a"(val) : "Nd"(p));
    return moo_number((double)val);
}
void moo_io_outl(MooValue port, MooValue data) {
    uint16_t p = (uint16_t)as_num(port); uint32_t d = (uint32_t)as_num(data);
    __asm__ volatile("outl %0,%1" :: "a"(d), "Nd"(p));
}

#else /* MOO_UEFI_USE_BARE_RUNTIME: MooValue/Port-I/O aus moo_bare.c */
#include <stdint.h>
#include <stddef.h>

#ifdef MOO_UEFI_COLLECTIONS
#include "../moo_runtime.h"
#include <float.h>
#include <limits.h>
#include <stdarg.h>
extern void *kern_realloc_c(void *p, uint64_t n);
#else
typedef struct { uint64_t tag; uint64_t data; } MooValue;   /* = moo_runtime.h */
#define MOO_UEFI_TAG_STRING 1u                                /* MOO_STRING */
/* Layout identisch zu struct MooString (moo_runtime.h). */
typedef struct { int32_t refcount; char *chars; int32_t length; int32_t capacity; } MooUefiString;

extern MooValue moo_none(void);                     /* moo_bare.c */
extern void *memcpy(void *, const void *, size_t); /* moo_freestanding_mem.c */
#endif

extern void *kern_alloc_c(uint64_t n);             /* moo_bare_alloc.c */
extern void kern_frei_c(void *p);

/* --- Heap-Bruecke: libc-Namen auf den Bare-Allocator. Die Arena setzt der
 *     Entry (uefi_entry.c) per kern_heap_init_c aus UEFI AllocatePages. --- */
void *malloc(size_t n) { return n ? kern_alloc_c((uint64_t)n) : (void *)0; }
void free(void *p) { kern_frei_c(p); }

#ifdef MOO_UEFI_COLLECTIONS
void *realloc(void *p, size_t n) { return kern_realloc_c(p, (uint64_t)n); }

void *calloc(size_t count, size_t size) {
    if (size != 0 && count > SIZE_MAX / size) return NULL;
    size_t bytes = count * size;
    void *p = malloc(bytes);
    return p ? memset(p, 0, bytes) : NULL;
}
#else
/* --- String-Konstruktor fuer Moo-Literale (Codegen ruft moo_string_new);
 *     Semantik wie moo_string.c, Speicher aus der Arena, OOM -> none. --- */
MooValue moo_string_new_len(const char *chars, int32_t len) {
    MooValue v = { MOO_UEFI_TAG_STRING, 0 };
    MooUefiString *s = (MooUefiString *)malloc(sizeof *s);
    if (!s) return moo_none();
    if (len < 0) len = 0;
    s->refcount = 1;
    s->length = len;
    s->capacity = len + 1;
    s->chars = (char *)malloc((size_t)len + 1u);
    if (!s->chars) { free(s); return moo_none(); }
    if (len > 0 && chars) memcpy(s->chars, chars, (size_t)len);
    s->chars[len] = '\0';
    v.data = (uint64_t)(uintptr_t)s;
    return v;
}
MooValue moo_string_new(const char *chars) {
    size_t n = 0;
    if (chars) while (chars[n]) ++n;
    return moo_string_new_len(chars, (int32_t)n);
}
#endif

/* --- Panik des Bare-Allocators (moo_bare_kern.h): Meldung auf debugcon
 *     0xE9, dann Halt. Kein Rueckweg in die Firmware. --- */
static void uefi_rt_outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
__attribute__((noreturn)) void kern_panic(const char *msg) {
    static const char kopf[] = "[KERN-PANIK] ";
    const char *p;
    for (p = kopf; *p; ++p) uefi_rt_outb(0xE9, (uint8_t)*p);
    for (p = msg ? msg : ""; *p; ++p) uefi_rt_outb(0xE9, (uint8_t)*p);
    uefi_rt_outb(0xE9, (uint8_t)'\n');
    for (;;) { __asm__ volatile("cli; hlt"); }
}

#ifdef MOO_UEFI_COLLECTIONS
MOO_TLS int moo_error_flag;

/* UEFI hat keinen laufenden Moo-Fehlerhandler. Ein Runtime-Fehler
 * hält die App mit seiner Meldung an. */
void moo_throw(MooValue value) {
    if (value.tag == MOO_ERROR) kern_panic(MV_ERR(value));
    if (value.tag == MOO_STRING) kern_panic(MV_STR(value)->chars);
    kern_panic("Moo-Laufzeitfehler");
}

void moo_uefi_print(const char *text) {
    for (; *text; ++text) uefi_rt_outb(0xE9, (uint8_t)*text);
    uefi_rt_outb(0xE9, (uint8_t)'\n');
}

/* C-Stringoperationen der gemeinsamen Runtime. In der EFI-App läuft
 * die Tokenisierung synchron auf demselben Thread. */
size_t strlen(const char *text) {
    const char *end = text;
    while (*end) ++end;
    return (size_t)(end - text);
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

char *strdup(const char *text) {
    size_t size = strlen(text) + 1;
    return memcpy(moo_alloc(size), text, size);
}

char *strstr(const char *text, const char *find) {
    if (!*find) return (char *)text;
    for (; *text; ++text) {
        const char *a = text, *b = find;
        while (*b && *a == *b) { ++a; ++b; }
        if (!*b) return (char *)text;
    }
    return NULL;
}

static bool trennzeichen(char ch, const char *delim) {
    for (; *delim; ++delim) {
        if (ch == *delim) return true;
    }
    return false;
}

char *strtok(char *text, const char *delim) {
    static char *next;
    if (text) next = text;
    if (!next) return NULL;
    while (*next && trennzeichen(*next, delim)) ++next;
    if (!*next) {
        next = NULL;
        return NULL;
    }
    char *token = next;
    while (*next && !trennzeichen(*next, delim)) ++next;
    if (*next) *next++ = '\0';
    else next = NULL;
    return token;
}

typedef struct {
    char *text;
    size_t size;
    size_t len;
} UefiFormat;

static void format_zeichen(UefiFormat *out, char c) {
    if (out->size && out->len < out->size - 1) out->text[out->len] = c;
    ++out->len;
}

static void format_text(UefiFormat *out, const char *text) {
    for (; *text; ++text) format_zeichen(out, *text);
}

static void format_zahl(UefiFormat *out, uint64_t n) {
    char ziffern[20];
    size_t anzahl = 0;
    do {
        ziffern[anzahl++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (anzahl) format_zeichen(out, ziffern[--anzahl]);
}

static void format_ganzzahl(UefiFormat *out, long long n) {
    if (n < 0) format_zeichen(out, '-');
    format_zahl(out, n < 0 ? 0 - (uint64_t)n : (uint64_t)n);
}

/* %g mit sechs signifikanten Ziffern und dezimaler oder e-Notation. */
static void format_gleit(UefiFormat *out, double n) {
    if (n != n) { format_text(out, "nan"); return; }
    if (n < 0) { format_zeichen(out, '-'); n = -n; }
    if (n > DBL_MAX) { format_text(out, "inf"); return; }
    if (!n) { format_zeichen(out, '0'); return; }

    int exponent = 0;
    while (n >= 10) { n /= 10; ++exponent; }
    while (n < 1) { n *= 10; --exponent; }
    double skaliert = n * 100000.0;
    unsigned gerundet = (unsigned)skaliert;
    double rest = skaliert - gerundet;
    if (rest > 0.5 || (rest == 0.5 && (gerundet & 1u))) ++gerundet;
    if (gerundet == 1000000) { gerundet = 100000; ++exponent; }

    char ziffern[6];
    for (int i = 5; i >= 0; --i) {
        ziffern[i] = (char)('0' + gerundet % 10);
        gerundet /= 10;
    }
    int anzahl = 6;
    while (anzahl > 1 && ziffern[anzahl - 1] == '0') --anzahl;

    bool wissenschaftlich = exponent < -4 || exponent >= 6;
    int komma = wissenschaftlich ? 1 : exponent + 1;
    if (komma <= 0) {
        format_text(out, "0.");
        for (int i = komma; i < 0; ++i) format_zeichen(out, '0');
    }
    int ende = anzahl > komma ? anzahl : komma;
    for (int i = 0; i < ende; ++i) {
        if (i > 0 && i == komma) format_zeichen(out, '.');
        format_zeichen(out, i < anzahl ? ziffern[i] : '0');
    }
    if (wissenschaftlich) {
        format_zeichen(out, 'e');
        format_zeichen(out, exponent < 0 ? '-' : '+');
        unsigned betrag = (unsigned)(exponent < 0 ? -exponent : exponent);
        if (betrag < 10) format_zeichen(out, '0');
        format_zahl(out, betrag);
    }
}

/* Nur die Formate der UEFI-Wertausgabe und Runtime-Diagnosen. */
int snprintf(char *text, size_t size, const char *format, ...) {
    UefiFormat out = {text, size, 0};
    bool gueltig = true;
    va_list args;
    va_start(args, format);
    while (*format && gueltig) {
        char c = *format++;
        if (c != '%') {
            format_zeichen(&out, c);
            continue;
        }
        if (format[0] == 'l' && format[1] == 'l' && format[2] == 'd') {
            format_ganzzahl(&out, va_arg(args, long long));
            format += 3;
            continue;
        }
        switch (*format) {
        case 'd': format_ganzzahl(&out, va_arg(args, int)); break;
        case 'g': format_gleit(&out, va_arg(args, double)); break;
        case 's': format_text(&out, va_arg(args, const char *)); break;
        case '%': format_zeichen(&out, '%'); break;
        default: gueltig = false; continue;
        }
        ++format;
    }
    va_end(args);
    if (size) text[out.len < size ? out.len : size - 1] = '\0';
    return gueltig && out.len <= INT_MAX ? (int)out.len : -1;
}
#endif

/* --- Win64-Stack-Sonde: LLVM ruft __chkstk vor Frames > 4 KiB (RAX = Groesse,
 *     alle Register erhalten, RAX unveraendert zurueck; der Aufrufer macht
 *     danach selbst `sub rsp, rax`). Der UEFI-Stack ist vollstaendig abgebildet
 *     (keine Guard-Pages wie unter Windows), darum genuegt die Rueckkehr. --- */
__asm__(".text\n"
        ".globl __chkstk\n"
        "__chkstk:\n"
        "    ret\n");

/* --- libm-Minimum fuer moo_surface.c (floor/round/fmod). Definiert fuer
 *     endliche Werte mit |x| < 2^63; NaN/Inf und groessere Betraege kommen
 *     unveraendert zurueck, fmod(x, 0) liefert NaN. Kein vollstaendiges libm. --- */
#define UEFI_RT_I64_GRENZE 9.2e18
static int uefi_rt_klein(double x) {
    return x == x && x < UEFI_RT_I64_GRENZE && x > -UEFI_RT_I64_GRENZE;
}
static double uefi_rt_nan(void) {
    union { uint64_t u; double d; } n; n.u = 0x7FF8000000000000ull; return n.d;
}
double floor(double x) {
    long long i;
    if (!uefi_rt_klein(x)) return x;
    i = (long long)x;                       /* Abschneiden Richtung 0 */
    return (x < 0.0 && (double)i != x) ? (double)(i - 1) : (double)i;
}
double round(double x) {
    if (!uefi_rt_klein(x)) return x;
    return x < 0.0 ? -floor(0.5 - x) : floor(x + 0.5);
}
double fmod(double x, double y) {
    double q;
    if (y == 0.0 || x != x || y != y) return uefi_rt_nan();
    if (!uefi_rt_klein(x) || !uefi_rt_klein(y)) return x;
    q = x / y;
    if (!uefi_rt_klein(q)) return x;
    return x - y * (double)(long long)q;
}

#ifndef MOO_UEFI_COLLECTIONS
/* --- Fail-closed-Stubs: moo_surface.c referenziert diese nur in Pfaden, die
 *     das UEFI-Bild nicht nutzt (Glass-Effekt, Frame-Snapshot/Blit,
 *     Roundrect-Stapel). lld-link verlangt die Symbole trotz /opt:ref;
 *     im UEFI-Pfad gibt es keine Dict/List/Frame-Runtime -> none. --- */
MooValue moo_dict_new(void) { return moo_none(); }
MooValue moo_dict_get(MooValue dict, MooValue key) { (void)dict; (void)key; return moo_none(); }
void moo_dict_set(MooValue dict, MooValue key, MooValue value) { (void)dict; (void)key; (void)value; }
MooValue moo_list_get(MooValue list, MooValue index) { (void)list; (void)index; return moo_none(); }
MooValue moo_list_length(MooValue list) { (void)list; return moo_none(); }
MooValue moo_frame_new_take(int width, int height, uint8_t *rgba_pixels_top_left) {
    (void)width; (void)height;
    free(rgba_pixels_top_left);            /* take-Vertrag: Puffer gehoert uns */
    return moo_none();
}

#endif /* !MOO_UEFI_COLLECTIONS */
#endif /* MOO_UEFI_USE_BARE_RUNTIME */

/* MSVC-Float-Marker: lld-link verlangt _fltused sobald Floats vorkommen. */
int _fltused = 0;

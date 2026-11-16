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

typedef struct { uint64_t tag; uint64_t data; } MooValue;   /* = moo_runtime.h */
#define MOO_UEFI_TAG_STRING 1u                                /* MOO_STRING */
/* Layout identisch zu struct MooString (moo_runtime.h). */
typedef struct { int32_t refcount; char *chars; int32_t length; int32_t capacity; } MooUefiString;

extern void *kern_alloc_c(uint64_t n);             /* moo_bare_alloc.c */
extern void kern_frei_c(void *p);
extern MooValue moo_none(void);                     /* moo_bare.c */
extern void *memcpy(void *, const void *, size_t); /* moo_freestanding_mem.c */

/* --- Heap-Bruecke: libc-Namen auf den Bare-Allocator. Die Arena setzt der
 *     Entry (uefi_entry.c) per kern_heap_init_c aus UEFI AllocatePages. --- */
void *malloc(size_t n) { return n ? kern_alloc_c((uint64_t)n) : (void *)0; }
void free(void *p) { kern_frei_c(p); }

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

#endif /* MOO_UEFI_USE_BARE_RUNTIME */

/* MSVC-Float-Marker: lld-link verlangt _fltused sobald Floats vorkommen. */
int _fltused = 0;

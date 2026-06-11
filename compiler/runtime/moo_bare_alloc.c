/**
 * moo_bare_alloc.c — Bare-Metal Allocator fuer den Kernel (Plan-010 K3).
 *
 * Strategie: Bump-Allocator als Basis + Free-List (first-fit) fuer Wiederverwendung.
 *   - kern_speicher_init(start, ende): Heap-Region setzen.
 *   - kern_alloc(n): 16-Byte-aligned, Block-Header {size, magic, naechster}.
 *       Erst Free-List durchsuchen (first-fit, Split bei Ueberhang), sonst Bump.
 *   - kern_frei(addr): Magic-Check, Block in Free-List einhaengen.
 *
 * KEIN malloc, kein libc. Adressrechnung in uintptr_t, Groessen in uint64_t
 * (UB-Policy: kein signed overflow, kein -fwrapv).
 *
 * Host-Test (test_bare_alloc_asan.c) ersetzt die Heap-Region durch eine
 * malloc-Arena und prueft alloc/frei/Split/Coalesce ASan+UBSan-clean.
 */
#include "moo_bare_kern.h"

#define KERN_ALLOC_MAGIC   0x4D4F4F4248454150ULL  /* "MOOBHEAP" */
#define KERN_ALLOC_ALIGN   16u

typedef struct KernBlock {
    uint64_t          magic;     /* KERN_ALLOC_MAGIC, sonst korrupt/invalid */
    uint64_t          size;      /* Nutzgroesse (ohne Header), 16-aligned */
    struct KernBlock* naechster; /* Free-List-Verkettung (nur wenn frei) */
    uint64_t          frei;      /* 1 = in Free-List, 0 = vergeben */
} KernBlock;

static uintptr_t   g_heap_start = 0;
static uintptr_t   g_heap_ende  = 0;
static uintptr_t   g_bump       = 0;   /* naechste freie Bump-Adresse */
static KernBlock*  g_freelist   = 0;   /* Kopf der Free-List */
static uint64_t    g_belegt     = 0;   /* aktuell vergebene Nutzbytes */
static uint64_t    g_peak       = 0;   /* high-water Mark */

static inline uint64_t align_up(uint64_t n, uint64_t a) {
    /* a ist Zweierpotenz. Reine unsigned-Arithmetik. */
    return (n + (a - 1u)) & ~(a - 1u);
}

/* ----------------------------------------------------------------------
 * C-Seite (vom Boot-Trampolin genutzt)
 * ---------------------------------------------------------------------- */
void kern_heap_init_c(uintptr_t start, uintptr_t ende) {
    g_heap_start = align_up((uint64_t)start, KERN_ALLOC_ALIGN);
    g_heap_ende  = ende;
    g_bump       = g_heap_start;
    g_freelist   = 0;
    g_belegt     = 0;
    g_peak       = 0;
}

void* kern_alloc_c(uint64_t n) {
    if (g_heap_start == 0 || n == 0) return 0;
    uint64_t need = align_up(n, KERN_ALLOC_ALIGN);

    /* 1) Free-List first-fit. */
    KernBlock** prev = &g_freelist;
    for (KernBlock* b = g_freelist; b; b = b->naechster) {
        if (b->magic == KERN_ALLOC_MAGIC && b->frei && b->size >= need) {
            /* Optionaler Split, wenn der Rest noch einen Header+Mindestblock traegt. */
            uint64_t rest = b->size - need;
            if (rest >= sizeof(KernBlock) + KERN_ALLOC_ALIGN) {
                uintptr_t split_at = (uintptr_t)b + sizeof(KernBlock) + need;
                KernBlock* nb = (KernBlock*)split_at;
                nb->magic     = KERN_ALLOC_MAGIC;
                nb->size      = rest - sizeof(KernBlock);
                nb->frei      = 1;
                nb->naechster = b->naechster;
                b->size       = need;
                b->naechster  = nb;
            }
            *prev = b->naechster;     /* aus Free-List aushaengen */
            b->frei = 0;
            b->naechster = 0;
            g_belegt += b->size;
            if (g_belegt > g_peak) g_peak = g_belegt;
            return (void*)((uintptr_t)b + sizeof(KernBlock));
        }
        prev = &b->naechster;
    }

    /* 2) Bump aus der noch ungenutzten Region. */
    uintptr_t hdr = g_bump;
    uintptr_t neu = hdr + sizeof(KernBlock) + need;
    if (neu > g_heap_ende) return 0;   /* OOM */
    KernBlock* b = (KernBlock*)hdr;
    b->magic     = KERN_ALLOC_MAGIC;
    b->size      = need;
    b->frei      = 0;
    b->naechster = 0;
    g_bump = neu;
    g_belegt += need;
    if (g_belegt > g_peak) g_peak = g_belegt;
    return (void*)(hdr + sizeof(KernBlock));
}

void kern_frei_c(void* p) {
    if (!p) return;
    KernBlock* b = (KernBlock*)((uintptr_t)p - sizeof(KernBlock));
    if (b->magic != KERN_ALLOC_MAGIC) {
        kern_panic("kern_frei: ungueltiger Block (Magic-Mismatch)");
    }
    if (b->frei) {
        kern_panic("kern_frei: Doppel-free erkannt");
    }
    b->frei = 1;
    b->naechster = g_freelist;
    g_freelist = b;
    if (g_belegt >= b->size) g_belegt -= b->size; else g_belegt = 0;
}

/* ----------------------------------------------------------------------
 * moo-Builtin-Wrapper (MooValue)
 * ---------------------------------------------------------------------- */
MooValue kern_speicher_init(MooValue start, MooValue ende) {
    kern_heap_init_c((uintptr_t)kern_as_double(start),
                     (uintptr_t)kern_as_double(ende));
    return moo_none();
}

MooValue kern_alloc(MooValue groesse) {
    void* p = kern_alloc_c((uint64_t)kern_as_double(groesse));
    return moo_number((double)(uintptr_t)p);
}

MooValue kern_frei(MooValue adresse) {
    uintptr_t a = (uintptr_t)kern_as_double(adresse);
    if (a == 0) return moo_bool(false);
    kern_frei_c((void*)a);
    return moo_bool(true);
}

MooValue kern_speicher_frei(void) {
    /* Noch nicht gebumpte Region + (vereinfacht) nicht aufsummierte Free-List. */
    uint64_t bump_rest = (g_heap_ende > g_bump) ? (uint64_t)(g_heap_ende - g_bump) : 0;
    uint64_t fl = 0;
    for (KernBlock* b = g_freelist; b; b = b->naechster) fl += b->size;
    return moo_number((double)(bump_rest + fl));
}

MooValue kern_speicher_belegt(void) {
    return moo_number((double)g_belegt);
}

MooValue kern_speicher_peak(void) {
    return moo_number((double)g_peak);
}

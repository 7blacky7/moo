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
 * malloc-Arena und prueft alloc/frei/Split sowie harte Grenzfaelle
 * ASan+UBSan-clean. Coalescing ist nicht Teil dieses Allocators.
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

#define KERN_MOO_MAX_EXACT_U64 UINT64_C(9007199254740992) /* 2^53 */

static bool checked_align_up_u64(uint64_t n, uint64_t a, uint64_t* out) {
    uint64_t mask;
    if (!out || a == 0u || (a & (a - 1u)) != 0u) return false;
    mask = a - 1u;
    if (n > UINT64_MAX - mask) return false;
    *out = (n + mask) & ~mask;
    return true;
}

/* Prueft einen Header erst numerisch gegen die bekannte Heap-Region und
 * dereferenziert ihn erst danach. Die reale Objektprovenienz der Region bleibt
 * Vertrag von kern_heap_init_c; fremde free-Pointer werden nie dereferenziert. */
static bool kern_block_layout(uintptr_t header, KernBlock** out_block,
                              uintptr_t* out_ende) {
    uintptr_t payload;
    KernBlock* block;
    if (g_heap_start == 0u || g_bump < g_heap_start ||
        g_bump > g_heap_ende) return false;
    if (header < g_heap_start || header >= g_bump) return false;
    if (((header - g_heap_start) & (KERN_ALLOC_ALIGN - 1u)) != 0u)
        return false;
    if ((uint64_t)(g_bump - header) < (uint64_t)sizeof(KernBlock))
        return false;

    payload = header + sizeof(KernBlock);
    block = (KernBlock*)header;
    if (block->magic != KERN_ALLOC_MAGIC || block->frei > 1u ||
        block->size < KERN_ALLOC_ALIGN ||
        (block->size & (KERN_ALLOC_ALIGN - 1u)) != 0u)
        return false;
    if (block->size > (uint64_t)(g_bump - payload)) return false;

    if (out_block) *out_block = block;
    if (out_ende) *out_ende = payload + (uintptr_t)block->size;
    return true;
}

static KernBlock* kern_find_payload(uintptr_t payload) {
    uintptr_t header = g_heap_start;
    while (header < g_bump) {
        KernBlock* block;
        uintptr_t block_ende;
        if (!kern_block_layout(header, &block, &block_ende))
            kern_panic("kern_frei: Heap-Blockkette korrupt");
        if (header + sizeof(KernBlock) == payload) return block;
        header = block_ende;
    }
    return 0;
}

/* ----------------------------------------------------------------------
 * C-Seite (vom Boot-Trampolin genutzt)
 * ---------------------------------------------------------------------- */
void kern_heap_init_c(uintptr_t start, uintptr_t ende) {
    uint64_t aligned64;
    uintptr_t aligned;

    /* Jeder Fehler deaktiviert den alten Heap fail-closed. */
    g_heap_start = 0u;
    g_heap_ende  = 0u;
    g_bump       = 0u;
    g_freelist   = 0;
    g_belegt     = 0u;
    g_peak       = 0u;

    if (start == 0u || start >= ende ||
        !checked_align_up_u64((uint64_t)start, KERN_ALLOC_ALIGN, &aligned64) ||
        aligned64 > (uint64_t)UINTPTR_MAX)
        return;
    aligned = (uintptr_t)aligned64;
    if (aligned >= ende ||
        (uint64_t)(ende - aligned) <
            (uint64_t)sizeof(KernBlock) + KERN_ALLOC_ALIGN)
        return;

    g_heap_start = aligned;
    g_heap_ende  = ende;
    g_bump       = aligned;
}

void* kern_alloc_c(uint64_t n) {
    uint64_t need;
    KernBlock** prev;
    KernBlock* block;
    uint64_t gesehen = 0u;
    uint64_t max_knoten;

    if (g_heap_start == 0u || n == 0u ||
        !checked_align_up_u64(n, KERN_ALLOC_ALIGN, &need))
        return 0;
    if (g_bump < g_heap_start || g_bump > g_heap_ende) return 0;

    /* 1) Free-List first-fit. Jede Verkettung wird vor Nutzung validiert. */
    max_knoten = (uint64_t)(g_bump - g_heap_start) /
                  ((uint64_t)sizeof(KernBlock) + KERN_ALLOC_ALIGN) + 1u;
    prev = &g_freelist;
    block = g_freelist;
    while (block) {
        KernBlock* next;
        uintptr_t block_ende;
        if (++gesehen > max_knoten ||
            !kern_block_layout((uintptr_t)block, 0, &block_ende) ||
            block->frei != 1u)
            kern_panic("kern_alloc: Free-List korrupt");
        next = block->naechster;

        if (block->size >= need) {
            uint64_t rest = block->size - need;
            if (rest >= (uint64_t)sizeof(KernBlock) + KERN_ALLOC_ALIGN) {
                uintptr_t payload = (uintptr_t)block + sizeof(KernBlock);
                uintptr_t split_at = payload + (uintptr_t)need;
                KernBlock* neu = (KernBlock*)split_at;
                neu->magic     = KERN_ALLOC_MAGIC;
                neu->size      = rest - sizeof(KernBlock);
                neu->frei      = 1u;
                neu->naechster = next;
                block->size    = need;
                block->naechster = neu;
            }
            *prev = block->naechster;
            if (g_belegt > UINT64_MAX - block->size)
                kern_panic("kern_alloc: Belegt-Zaehler overflow");
            block->frei = 0u;
            block->naechster = 0;
            g_belegt += block->size;
            if (g_belegt > g_peak) g_peak = g_belegt;
            return (void*)((uintptr_t)block + sizeof(KernBlock));
        }
        prev = &block->naechster;
        block = next;
    }

    /* 2) Bump: nur Subtraktionen, bis alle Summanden nachweislich passen. */
    {
        uintptr_t header = g_bump;
        uint64_t rest = (uint64_t)(g_heap_ende - header);
        uintptr_t neu;
        if (rest < (uint64_t)sizeof(KernBlock) ||
            need > rest - (uint64_t)sizeof(KernBlock))
            return 0;
        neu = header + sizeof(KernBlock) + (uintptr_t)need;

        block = (KernBlock*)header;
        block->magic     = KERN_ALLOC_MAGIC;
        block->size      = need;
        block->frei      = 0u;
        block->naechster = 0;
        g_bump = neu;
        if (g_belegt > UINT64_MAX - need)
            kern_panic("kern_alloc: Belegt-Zaehler overflow");
        g_belegt += need;
        if (g_belegt > g_peak) g_peak = g_belegt;
        return (void*)(header + sizeof(KernBlock));
    }
}

void kern_frei_c(void* p) {
    uintptr_t payload;
    KernBlock* block;
    if (!p) return;
    payload = (uintptr_t)p;

    /* Noch vor irgendeinem Headerzugriff: aktive Region, Bereich und
     * Payload-Alignment pruefen. Exakte Mitgliedschaft folgt per Blockscan. */
    if (g_heap_start == 0u || g_bump < g_heap_start ||
        g_bump > g_heap_ende || payload < g_heap_start ||
        payload >= g_bump || payload - g_heap_start < sizeof(KernBlock) ||
        (payload & (KERN_ALLOC_ALIGN - 1u)) != 0u)
        kern_panic("kern_frei: Adresse ausserhalb Heap/Alignment");

    block = kern_find_payload(payload);
    if (!block)
        kern_panic("kern_frei: Adresse ist kein Blockanfang");
    if (block->frei)
        kern_panic("kern_frei: Doppel-free erkannt");
    if (g_belegt < block->size)
        kern_panic("kern_frei: Belegt-Zaehler korrupt");

    block->frei = 1u;
    block->naechster = g_freelist;
    g_freelist = block;
    g_belegt -= block->size;
}

/* ----------------------------------------------------------------------
 * moo-Builtin-Wrapper (MooValue)
 * ---------------------------------------------------------------------- */

/* Die Bare-ABI transportiert historische Adressen als Moo-Zahl. IEEE-754
 * repraesentiert nur Integer bis einschliesslich 2^53 exakt. O3 verwendet
 * diesen Pfad fuer Handles ausdruecklich NICHT. */
static bool kern_number_u64(MooValue value, uint64_t* out) {
    double number;
    uint64_t converted;
    if (!out || value.tag != MOO_NUMBER) return false;
    number = kern_as_double(value);
    /* NaN faellt ebenfalls durch !(number >= 0.0); +Inf und zu grosse Werte
     * werden vor der float->int-Konversion abgefangen. */
    if (!(number >= 0.0) || number > (double)KERN_MOO_MAX_EXACT_U64)
        return false;
    converted = (uint64_t)number;
    if ((double)converted != number) return false;
    *out = converted;
    return true;
}

static bool kern_number_uintptr(MooValue value, uintptr_t* out) {
    uint64_t converted;
    if (!out || !kern_number_u64(value, &converted) ||
        converted > (uint64_t)UINTPTR_MAX)
        return false;
    *out = (uintptr_t)converted;
    return true;
}

MooValue kern_speicher_init(MooValue start, MooValue ende) {
    uintptr_t start_addr;
    uintptr_t end_addr;
    if (!kern_number_uintptr(start, &start_addr) ||
        !kern_number_uintptr(ende, &end_addr)) {
        kern_heap_init_c(0u, 0u);
        return moo_none();
    }
    kern_heap_init_c(start_addr, end_addr);
    return moo_none();
}

MooValue kern_alloc(MooValue groesse) {
    uint64_t bytes;
    void* p;
    uintptr_t adresse;
    if (!kern_number_u64(groesse, &bytes) || bytes == 0u)
        return moo_number(0.0);
    p = kern_alloc_c(bytes);
    if (!p) return moo_number(0.0);
    adresse = (uintptr_t)p;
    if ((uint64_t)adresse > KERN_MOO_MAX_EXACT_U64) {
        kern_frei_c(p);
        return moo_number(0.0);
    }
    return moo_number((double)adresse);
}

MooValue kern_frei(MooValue adresse) {
    uintptr_t a;
    if (!kern_number_uintptr(adresse, &a) || a == 0u)
        return moo_bool(false);
    kern_frei_c((void*)a);
    return moo_bool(true);
}

MooValue kern_speicher_frei(void) {
    uint64_t bump_rest =
        (g_heap_ende > g_bump) ? (uint64_t)(g_heap_ende - g_bump) : 0u;
    uint64_t fl = 0u;
    uint64_t gesehen = 0u;
    uint64_t max_knoten =
        g_bump >= g_heap_start
            ? (uint64_t)(g_bump - g_heap_start) /
                  ((uint64_t)sizeof(KernBlock) + KERN_ALLOC_ALIGN) + 1u
            : 0u;
    KernBlock* block = g_freelist;
    while (block) {
        KernBlock* next;
        if (++gesehen > max_knoten ||
            !kern_block_layout((uintptr_t)block, 0, 0) ||
            block->frei != 1u)
            kern_panic("kern_speicher_frei: Free-List korrupt");
        if (fl > UINT64_MAX - block->size)
            kern_panic("kern_speicher_frei: Zaehler overflow");
        fl += block->size;
        next = block->naechster;
        block = next;
    }
    if (bump_rest > UINT64_MAX - fl)
        kern_panic("kern_speicher_frei: Gesamtzaehler overflow");
    return moo_number((double)(bump_rest + fl));
}

MooValue kern_speicher_belegt(void) {
    return moo_number((double)g_belegt);
}

MooValue kern_speicher_peak(void) {
    return moo_number((double)g_peak);
}

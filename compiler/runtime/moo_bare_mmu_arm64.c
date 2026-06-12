/**
 * moo_bare_mmu_arm64.c — ARM64-MMU Identity-Map PoC (P012-D3).
 * Design: Thought ef73972e (P012-D3 DESIGN). Kurzfassung:
 *   EL1, 4K-Granule, T0SZ=25 (39-bit VA, Lookup ab Level 1), EINE
 *   statische L1-Tabelle mit zwei 1-GB-Bloecken:
 *     L1[0] 0x0000_0000 Device-nGnRnE (QEMU-virt-MMIO: GIC/PL011)
 *     L1[1] 0x4000_0000 Normal WB     (RAM, Kernel @ 0x4008_0000)
 *   Kein Demand-Paging, keine L2/L3, keine VM-Abstraktion.
 * UB-Policy: Deskriptoren uint64_t, Adressen uintptr_t, Alignment via
 * __attribute__((aligned(4096))).
 *
 * ECHTE-HW-HINWEIS (H1): Die Tabelle wird mit MMU/Caches AUS
 * geschrieben, der Walker liest spaeter cacheable (TCR.IRGN/ORGN=WB).
 * Unter QEMU/TCG unkritisch; auf echter Hardware VOR dem Enable die
 * Tabelle per dc cvac (clean) in den PoC ergaenzen.
 */
#include "moo_bare_kern.h"

#if defined(__aarch64__)

/* L1: 512 Eintraege x 1 GB = 512 GB VA-Abdeckung (T0SZ=25). */
__attribute__((aligned(4096))) static uint64_t g_l1[512];

/* Block-Deskriptor-Bits (ARM ARM D8, VMSAv8-64 Stage-1):
 * [1:0]=0b01 Block | [4:2] AttrIndx | [7:6] AP=00 (RW EL1) |
 * [9:8] SH | [10] AF=1 (PFLICHT, sonst Access-Fault) |
 * [53] PXN | [54] UXN. */
#define D3_BLOCK      0x1ull
#define D3_ATTR_DEV   (0ull << 2)   /* MAIR Attr0 = Device-nGnRnE */
#define D3_ATTR_NORM  (1ull << 2)   /* MAIR Attr1 = Normal WB */
#define D3_SH_INNER   (3ull << 8)
#define D3_AF         (1ull << 10)
#define D3_PXN        (1ull << 53)
#define D3_UXN        (1ull << 54)

/* MAIR_EL1: Attr0=0x00 Device-nGnRnE, Attr1=0xFF Normal WB RA/WA. */
#define D3_MAIR  0x0000FF00ull
/* TCR_EL1: T0SZ=25 | IRGN0=WB | ORGN0=WB | SH0=inner | TG0=4K | EPD1. */
#define D3_TCR   (25ull | (1ull << 8) | (1ull << 10) | (3ull << 12) | (1ull << 23))

/* RAM-Roundtrip-Zeuge fuer den VA==PA-Beweis (static -> .bss/.data,
 * volatile gegen Wegoptimieren). */
static volatile uint64_t g_mmu_zeuge;

/* Identity-MMU einschalten. Rueckgabe: true wenn der RAM-Roundtrip
 * nach dem Enable den Magic-Wert liefert. UART-Beweis macht der
 * Aufrufer (Marker NACH dem Enable senden). */
bool kern_arm64_mmu_identity_an(void) {
    g_l1[0] = 0x00000000ull | D3_BLOCK | D3_ATTR_DEV  | D3_AF | D3_PXN | D3_UXN;
    g_l1[1] = 0x40000000ull | D3_BLOCK | D3_ATTR_NORM | D3_SH_INNER | D3_AF | D3_UXN;
    /* Rest bleibt 0 (invalid) — .bss ist vom Entry genullt. */

    __asm__ volatile(
        "msr mair_el1, %0\n"
        "msr tcr_el1, %1\n"
        "msr ttbr0_el1, %2\n"
        "dsb ish\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n"
        : : "r"(D3_MAIR), "r"(D3_TCR), "r"((uintptr_t)g_l1) : "memory");

    uint64_t sctlr;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ull << 0)    /* M: MMU an */
           | (1ull << 2)    /* C: D-Cache an (zusammen mit MMU, WB-Attr) */
           | (1ull << 12);  /* I: I-Cache an */
    __asm__ volatile("msr sctlr_el1, %0\nisb\n" : : "r"(sctlr) : "memory");

    /* VA==PA-Beweis: RAM-Roundtrip ueber die Normal-Map. Dass dieser
     * Code ueberhaupt weiterlaeuft, beweist Identity fuer .text (der PC
     * ueberlebt das Enable nur bei VA==PA). */
    g_mmu_zeuge = 0x4D4F4F4D4D553634ull;   /* ASCII 'MOOMMU64' */
    return g_mmu_zeuge == 0x4D4F4F4D4D553634ull;
}

#else
typedef int moo_bare_mmu_arm64_unused;
#endif

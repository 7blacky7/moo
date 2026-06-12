/**
 * moo_bare_gic_arm64.c — GICv2 minimal + EL1-Timer-IRQ (P012-D4).
 * Design: Thought 36a7b8e8. Kurzfassung: VBAR_EL1-Vektoren (16 Slots,
 * IRQ fuer SP0 UND SPx -> ein Stub, Rest wfi-Hang), GICv2-Init mit
 * seriellem Schritt-Dump, CNTP-Timer 10 ms, Handler zaehlt g_arm64_ticks.
 * NUR Timer-IRQ enable/eoi — KEINE generische Interrupt-Architektur.
 * Basen aus C1-Board-Defines (qemu-virt: dist 0x08000000, cpu 0x08010000;
 * Smoke erzwingt -M virt,gic-version=2).
 */
#include "moo_bare_kern.h"

#if defined(__aarch64__) && defined(MOO_BOARD_GIC_DIST_BASE) && defined(MOO_BOARD_GIC_CPU_BASE)

#define GICD_BASE        ((uintptr_t)MOO_BOARD_GIC_DIST_BASE)
#define GICC_BASE        ((uintptr_t)MOO_BOARD_GIC_CPU_BASE)
#define GICD_CTLR        0x000u
#define GICD_ISENABLER0  0x100u   /* SGI/PPI-Bank (INTID 0-31) */
#define GICC_CTLR        0x000u
#define GICC_PMR         0x004u
#define GICC_IAR         0x00Cu
#define GICC_EOIR        0x010u

static volatile uint64_t g_arm64_ticks;
static uint32_t g_tval_intervall;

/* Vektor-Tabelle (2048-aligned, 16 Slots a 0x80) + IRQ-Stub.
 * Slot 1 liegt direkt nach .align 11 (Offset 0x000), vor jedem weiteren
 * Slot ein .align 7. IRQ-Slots 0x080 (SP0) und 0x280 (SPx) -> Stub
 * (robust gegen SPSel); alle anderen -> wfi-Hang (Smoke-Timeout deckt
 * Fehlrouting auf). Stub sichert Caller-saved x0-x18 + x29/x30 — der
 * C-Handler MUSS deshalb general-regs-only sein (kein FP-Save). */
__asm__(
    ".section .text.vectors, \"ax\"\n"
    ".align 11\n"
    ".global arm64_vektoren\n"
    "arm64_vektoren:\n"
    "    b arm64_hang\n"          /* 0x000 Sync SP0 */
    ".align 7\n"
    "    b arm64_irq_stub\n"      /* 0x080 IRQ  SP0 */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x100 FIQ  SP0 */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x180 SErr SP0 */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x200 Sync SPx */
    ".align 7\n"
    "    b arm64_irq_stub\n"      /* 0x280 IRQ  SPx */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x300 FIQ  SPx */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x380 SErr SPx */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x400 Lower-EL A64 Sync */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x480 Lower-EL A64 IRQ */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x500 Lower-EL A64 FIQ */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x580 Lower-EL A64 SErr */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x600 Lower-EL A32 Sync */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x680 Lower-EL A32 IRQ */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x700 Lower-EL A32 FIQ */
    ".align 7\n"
    "    b arm64_hang\n"          /* 0x780 Lower-EL A32 SErr */
    ".text\n"
    "arm64_hang:\n"
    "    wfi\n"
    "    b arm64_hang\n"
    "arm64_irq_stub:\n"
    "    stp x0, x1,   [sp, #-16]!\n"
    "    stp x2, x3,   [sp, #-16]!\n"
    "    stp x4, x5,   [sp, #-16]!\n"
    "    stp x6, x7,   [sp, #-16]!\n"
    "    stp x8, x9,   [sp, #-16]!\n"
    "    stp x10, x11, [sp, #-16]!\n"
    "    stp x12, x13, [sp, #-16]!\n"
    "    stp x14, x15, [sp, #-16]!\n"
    "    stp x16, x17, [sp, #-16]!\n"
    "    stp x18, x29, [sp, #-16]!\n"
    "    stp x30, xzr, [sp, #-16]!\n"
    "    bl arm64_irq_handler\n"
    "    ldp x30, xzr, [sp], #16\n"
    "    ldp x18, x29, [sp], #16\n"
    "    ldp x16, x17, [sp], #16\n"
    "    ldp x14, x15, [sp], #16\n"
    "    ldp x12, x13, [sp], #16\n"
    "    ldp x10, x11, [sp], #16\n"
    "    ldp x8, x9,   [sp], #16\n"
    "    ldp x6, x7,   [sp], #16\n"
    "    ldp x4, x5,   [sp], #16\n"
    "    ldp x2, x3,   [sp], #16\n"
    "    ldp x0, x1,   [sp], #16\n"
    "    eret\n"
);

/* IRQ-Handler: NUR Integer-Pfade (general-regs-only — der Stub sichert
 * keine FP-Register; Muster x86-ISRs aus P012-A3). INTID 30 (CNTP) oder
 * 27 (CNTV) -> Tick + Timer nachladen; immer EOI. */
__attribute__((target("general-regs-only")))
void arm64_irq_handler(void);
__attribute__((target("general-regs-only")))
void arm64_irq_handler(void) {
    uint32_t iar = kern_mmio_read32(GICC_BASE + GICC_IAR);
    uint32_t intid = iar & 0x3FFu;
    if (intid == 30u || intid == 27u) {
        g_arm64_ticks++;
        __asm__ volatile("msr cntp_tval_el0, %0" : : "r"((uint64_t)g_tval_intervall));
    }
    kern_mmio_write32(GICC_BASE + GICC_EOIR, iar);
}

uint64_t kern_arm64_gic_ticks(void) { return g_arm64_ticks; }

extern char arm64_vektoren[];

void kern_arm64_gic_timer_start(void) {
    __asm__ volatile("msr vbar_el1, %0\nisb\n" : : "r"((uintptr_t)arm64_vektoren));
    kern_seriell_text("GIC: vbar gesetzt\n");
    kern_mmio_write32(GICD_BASE + GICD_CTLR, 1u);
    kern_seriell_text("GIC: distributor an\n");
    kern_mmio_write32(GICD_BASE + GICD_ISENABLER0, (1u << 30) | (1u << 27));
    kern_seriell_text("GIC: ppi 30+27 enabled\n");
    kern_mmio_write32(GICC_BASE + GICC_PMR, 0xFFu);
    kern_mmio_write32(GICC_BASE + GICC_CTLR, 1u);
    kern_seriell_text("GIC: cpu-interface an (pmr 0xff)\n");
    g_tval_intervall = (uint32_t)(kern_arm64_timer_freq() / 100u);  /* 10 ms */
    __asm__ volatile("msr cntp_tval_el0, %0" : : "r"((uint64_t)g_tval_intervall));
    __asm__ volatile("msr cntp_ctl_el0, %0"  : : "r"(1ull));        /* ENABLE, IMASK=0 */
    kern_seriell_text("GIC: cntp-timer 10ms an\n");
    __asm__ volatile("msr daifclr, #2");                            /* IRQs an */
}

void kern_arm64_gic_timer_stop(void) {
    __asm__ volatile("msr cntp_ctl_el0, %0" : : "r"(0ull));
    __asm__ volatile("msr daifset, #2");
}

#else
typedef int moo_bare_gic_arm64_unused;
#endif

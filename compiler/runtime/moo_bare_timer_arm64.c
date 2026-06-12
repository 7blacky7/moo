/**
 * moo_bare_timer_arm64.c — ARM Generic Timer, Stufe 1: Counter-Polling
 * (P012-D2). KEIN Interruptbetrieb — der kommt als eigener Task nach dem
 * GIC (P012-D4); hier nur lesbare Frequenz + monotoner Counter.
 *
 * EL-Kontext (dokumentiert): qemu-system-aarch64 -M virt -kernel startet
 * den Kernel in EL1. CNTFRQ_EL0/CNTPCT_EL0 sind von EL1 immer lesbar
 * (CNTKCTL_EL1 gated nur EL0-Zugriffe). Das isb vor CNTPCT verhindert,
 * dass der Read spekulativ/out-of-order vorgezogen wird (ARM ARM D11.2,
 * uebliche Lese-Sequenz).
 *
 * Arch-gated (__aarch64__), NICHT board-gated: Architektur-Register,
 * kein MMIO — auf x86/riscv kompiliert die TU leer.
 */
#include "moo_bare_kern.h"

#if defined(__aarch64__)

uint64_t kern_arm64_timer_freq(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

uint64_t kern_arm64_timer_counter(void) {
    uint64_t v;
    __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v) :: "memory");
    return v;
}

#else
typedef int moo_bare_timer_arm64_unused;
#endif

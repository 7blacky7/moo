/**
 * moo_bare_entry_arm64.c — ARM64-Entry fuer qemu-virt (P012-D5).
 *
 * Muster analog moo_bare_boot.c (x86): globales asm + C-Boot-Funktion in
 * einer C-Datei — KEIN separates .S (runtime/boot/*.S ist die x86-Schiene
 * und wird im aarch64-Build nie kompiliert, P012-B4).
 * Nur aktiv im aarch64-PL011-Board-Build; sonst compile-only leer.
 *
 * Ablauf: qemu-system-aarch64 -M virt -kernel laedt das ELF an die
 * Link-Adresse (Board qemu-virt-aarch64: 0x40080000) und springt mit
 * x0 = DTB-Pointer auf _start (EL1, MMU/Caches aus — ohne MMU sind alle
 * Zugriffe Device-Memory, PL011-MMIO funktioniert direkt).
 * _start: DTB sichern -> BSS nullen (Loader-Verhalten nicht vorausgesetzt)
 * -> Stack setzen -> arm64_boot_main (C).
 * Der DTB-Pointer wird NUR gesichert/geloggt — kein Parser (P012-C2).
 *
 * OFFEN (dokumentiert, P012-E1): kern_boot_main-Gleichstand mit x86
 * (Heap-Init via Linker-Symbole) — hier bewusst direkter main()-Aufruf
 * ohne Heap; der D5-Smoke braucht keinen Allocator.
 */
#include "moo_bare_kern.h"

#if defined(__aarch64__) && defined(MOO_BOARD_UART_PL011)

__asm__(
    ".section .text.boot, \"ax\"\n"
    ".global _start\n"
    "_start:\n"
    "    mov x19, x0\n"                 /* DTB-Pointer sichern */
    /* FP/SIMD freischalten: CPACR_EL1.FPEN = 0b11 (Bits 20/21).
     * moo-Codegen + MooValue-Wrapper nutzen double — ohne FPEN trappt
     * die ERSTE FP-Instruktion, und ohne VBAR_EL1-Vektoren haengt die
     * CPU still. Exakt analog x86: CR4.OSFXSR im 32-bit-Trampolin
     * (moo_bare_boot.c, dort via qemu -d int verifiziert). */
    "    mrs x1, cpacr_el1\n"
    "    orr x1, x1, #(3 << 20)\n"
    "    msr cpacr_el1, x1\n"
    "    isb\n"
    /* BSS nullen — Schleife nutzt nur Register, kein Stack. */
    "    ldr x1, =__bss_start\n"
    "    ldr x2, =__bss_end\n"
    "1:  cmp x1, x2\n"
    "    b.hs 2f\n"
    "    str xzr, [x1], #8\n"
    "    b 1b\n"
    "2:  ldr x1, =g_arm64_stack_top\n"
    "    mov sp, x1\n"
    "    mov x0, x19\n"                 /* DTB als Argument an C */
    "    bl arm64_boot_main\n"
    "3:  wfi\n"
    "    b 3b\n"
    ".section .bss\n"
    ".align 4\n"                        /* 16-Byte-Alignment (AAPCS64-SP) */
    "g_arm64_stack:\n"
    "    .skip 16384\n"                 /* 16 KB Kernel-Stack */
    ".global g_arm64_stack_top\n"
    "g_arm64_stack_top:\n"
    ".section .text\n"
);

void arm64_boot_main(uint64_t dtb);
void arm64_boot_main(uint64_t dtb) {
    kern_seriell_init();                       /* PL011, Base aus Board-Define (D1) */
    kern_seriell_text("MOO-ARM64-ENTRY-OK\n");
    kern_seriell_text("MOO-ARM64-UART-OK\n");
    kern_seriell_text("DTB: ");
    kern_seriell_hex_u64(dtb);                 /* nur loggen/durchreichen (C2) */
    kern_seriell_zeichen_c('\n');
    /* P012-D2: Generic-Timer Stufe 1 — Frequenz + monotoner Counter.
     * Der Steigt-Beweis (c2 > c1) laeuft IM Kernel: OK/FAIL-Marker. */
    kern_seriell_text("TMR-FREQ: ");
    kern_seriell_dez_u64(kern_arm64_timer_freq());
    kern_seriell_zeichen_c('\n');
    {
        uint64_t c1 = kern_arm64_timer_counter();
        for (volatile unsigned i = 0; i < 100000u; ++i) { }   /* Busy-Spanne */
        uint64_t c2 = kern_arm64_timer_counter();
        kern_seriell_text("TMR-C1: ");
        kern_seriell_dez_u64(c1);
        kern_seriell_text("\nTMR-C2: ");
        kern_seriell_dez_u64(c2);
        kern_seriell_zeichen_c('\n');
        kern_seriell_text(c2 > c1 ? "MOO-ARM64-TIMER-OK\n"
                                  : "MOO-ARM64-TIMER-FAIL\n");
    }
    /* P012-D3: Identity-MMU + Caches an (Design ef73972e). Der Marker
     * NACH dem Enable laeuft ueber PL011 = Device-Map-Beweis; der
     * Rueckgabewert ist der RAM-Roundtrip (VA==PA). Dass dieser Code
     * weiterlaeuft, beweist Identity fuer .text. main() laeuft danach
     * MIT aktiver MMU. */
    kern_seriell_text(kern_arm64_mmu_identity_an() ? "MOO-ARM64-MMU-OK\n"
                                                   : "MOO-ARM64-MMU-FAIL\n");
#if defined(MOO_BOARD_GIC_DIST_BASE) && defined(MOO_BOARD_GIC_CPU_BASE)
    /* P012-D4: GICv2 + Timer-IRQ (Design 36a7b8e8). wfi-Warten auf 3
     * IRQ-Ticks mit 1s-Counter-TIMEOUT (D2-Polling) gegen Endlos-Hang,
     * danach ehrlicher OK/FAIL-Marker. Timer + IRQs werden VOR main()
     * wieder abgeschaltet — moo-Code laeuft ungestoert. */
    kern_arm64_gic_timer_start();
    {
        uint64_t start = kern_arm64_timer_counter();
        uint64_t freq  = kern_arm64_timer_freq();
        while (kern_arm64_gic_ticks() < 3u) {
            __asm__ volatile("wfi");
            if (kern_arm64_timer_counter() - start > freq) break;
        }
        kern_seriell_text("GIC-TICKS: ");
        kern_seriell_dez_u64(kern_arm64_gic_ticks());
        kern_seriell_zeichen_c('\n');
        kern_seriell_text(kern_arm64_gic_ticks() >= 3u ? "MOO-ARM64-GIC-OK\n"
                                                       : "MOO-ARM64-GIC-FAIL\n");
    }
    kern_arm64_gic_timer_stop();
#endif
    main();                                    /* moo-Entry */
    for (;;) moo_cpu_halt();
}

#else
/* Nicht-ARM64 oder ohne PL011-Board: leere TU vermeiden. */
typedef int moo_bare_entry_arm64_unused;
#endif

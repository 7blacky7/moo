/**
 * moo_bare_boot.c — Boot-Pfad fuer den x86_64 moo-Kernel (Plan-010 K5).
 *
 * Ablauf:
 *   GRUB/QEMU (Multiboot2) -> _start (.code32) -> Identity-Paging (erste 1 GB,
 *   2-MB-Pages) -> Long-Mode (PAE+LME+PG) -> GDT64 -> .code64 ->
 *   kern_stack -> kern_boot_main (C) -> seriell/heap init -> main (moo) -> hlt.
 *
 * Tabellen (PML4/PDPT/PD), Stack und GDT liegen als aligned Daten in diesem
 * Objekt. Kein dynamischer Speicher, kein libc.
 *
 * Reproduzierbar (GPT-Luecke 3): Header + Trampolin sind statisch, der Build
 * laeuft ueber die feste CLI-Pipeline (P010-C2).
 */
#include "moo_bare_kern.h"

/* ======================================================================
 * Multiboot2-Header (vom Linker an den Anfang gelegt, Section .multiboot2)
 * ====================================================================== */
#define MB2_MAGIC        0xE85250D6u
#define MB2_ARCH_I386    0u
#define MB2_HEADER_LEN   16u   /* nur magic+arch+len+checksum, dann end-tag */

struct __attribute__((packed)) Mb2Header {
    uint32_t magic;
    uint32_t architecture;
    uint32_t header_length;
    uint32_t checksum;
    /* end-tag */
    uint16_t end_type;
    uint16_t end_flags;
    uint32_t end_size;
};

__attribute__((section(".multiboot2"), used, aligned(8)))
const struct Mb2Header g_mb2_header = {
    .magic         = MB2_MAGIC,
    .architecture  = MB2_ARCH_I386,
    .header_length = sizeof(struct Mb2Header),
    /* Checksum so, dass magic+arch+len+checksum == 0 (mod 2^32). */
    .checksum      = (uint32_t)(0u - (MB2_MAGIC + MB2_ARCH_I386 + (uint32_t)sizeof(struct Mb2Header))),
    .end_type      = 0,
    .end_flags     = 0,
    .end_size      = 8,
};

#if defined(__x86_64__)

/* ======================================================================
 * Paging-Tabellen + Stack (.bss, 4-KB-aligned)
 * ====================================================================== */
__attribute__((aligned(4096), used)) static uint64_t g_pml4[512];
__attribute__((aligned(4096), used)) static uint64_t g_pdpt[512];
__attribute__((aligned(4096), used)) static uint64_t g_pd[512];

__attribute__((aligned(16), used)) static uint8_t g_stack[16384];  /* 16 KB Kernel-Stack */

/* GDT64: null, Code (0x08), Data (0x10). */
__attribute__((aligned(16), used)) static uint64_t g_gdt[3] = {
    0x0000000000000000ULL,                 /* null */
    0x00AF9A000000FFFFULL,                 /* Code: present, ring0, exec/read, L=1 */
    0x00AF92000000FFFFULL,                 /* Data: present, ring0, read/write */
};
struct __attribute__((packed)) GdtPtr { uint16_t limit; uint64_t base; };
/* Statisch initialisiert: &g_gdt ist ein Link-Zeit-konstanter Ausdruck, den
 * der asm-Pfad (laeuft VOR jedem C-Code) per lgdt direkt nutzen kann. */
__attribute__((used)) static struct GdtPtr g_gdt_ptr = {
    .limit = (uint16_t)(sizeof(g_gdt) - 1),
    .base  = (uint64_t)(uintptr_t)g_gdt,
};

/* Linker-Symbole fuer die Heap-Region (aus linker.ld). */
extern uint8_t kern_heap_start[];
extern uint8_t kern_heap_end[];

/* ======================================================================
 * _start — 32-bit Einstieg vom Bootloader.
 *
 * Wir verlassen uns auf GCC/Clang, das die Funktion als naked-aehnliches
 * globales asm an Section .text.boot platziert. Da inline-asm in einer
 * C-Funktion keinen garantierten Frame-freien Einstieg bietet, nutzen wir
 * ein dediziertes globales asm-Blockstueck.
 * ====================================================================== */
__asm__(
    ".section .text.boot\n"
    ".code32\n"
    ".global _start\n"
    "_start:\n"
    "    cli\n"
    "    cmp $0x36d76289, %eax\n"        /* Multiboot2-Bootloader-Magic in EAX? */
    "    jne .Lhang32\n"
    /* PD: 512 Eintraege a 2 MB = erste 1 GB, Present+RW+PS(7. Bit). */
    "    mov $g_pd, %edi\n"
    "    xor %ecx, %ecx\n"
    ".Lfill_pd:\n"
    "    mov %ecx, %eax\n"
    "    shl $21, %eax\n"                /* index * 2MB */
    "    or  $0x83, %eax\n"             /* Present | RW | PS */
    "    mov %eax, (%edi)\n"
    "    movl $0, 4(%edi)\n"            /* obere 32 Bit = 0 */
    "    add $8, %edi\n"
    "    inc %ecx\n"
    "    cmp $512, %ecx\n"
    "    jne .Lfill_pd\n"
    /* PDPT[0] -> PD, PML4[0] -> PDPT (Present|RW). */
    "    mov $g_pd, %eax\n"
    "    or  $0x03, %eax\n"
    "    mov %eax, g_pdpt\n"
    "    movl $0, g_pdpt+4\n"
    "    mov $g_pdpt, %eax\n"
    "    or  $0x03, %eax\n"
    "    mov %eax, g_pml4\n"
    "    movl $0, g_pml4+4\n"
    /* CR3 = PML4 */
    "    mov $g_pml4, %eax\n"
    "    mov %eax, %cr3\n"
    /* CR4: PAE (Bit 5) + OSFXSR (Bit 9) + OSXMMEXCPT (Bit 10).
     * OSFXSR ist PFLICHT: moo-Codegen + kern_*-Wrapper nutzen SSE2
     * (double/MooValue). Ohne OSFXSR ist die erste SSE-Instruktion #UD —
     * vor kern_idt_init gibt es keine IDT -> Triple-Fault.
     * (Verifiziert via qemu -d int: 0x6 -> 0xd -> 0x8 -> CPU Reset.) */
    "    mov %cr4, %eax\n"
    "    or  $0x620, %eax\n"
    "    mov %eax, %cr4\n"
    /* EFER.LME (MSR 0xC0000080, Bit 8) */
    "    mov $0xC0000080, %ecx\n"
    "    rdmsr\n"
    "    or  $0x100, %eax\n"
    "    wrmsr\n"
    /* CR0: PG (Bit 31) | PE (Bit 0) | MP (Bit 1); EM (Bit 2) loeschen.
     * EM=0/MP=1 gehoert zum SSE-Enable (siehe CR4 oben). */
    "    mov %cr0, %eax\n"
    "    and $0xFFFFFFFB, %eax\n"
    "    or  $0x80000003, %eax\n"
    "    mov %eax, %cr0\n"
    /* GDT64 laden + far-jump nach 64-bit. */
    "    lgdt g_gdt_ptr\n"
    "    ljmp $0x08, $.Llong_mode\n"
    ".Lhang32:\n"
    "    hlt\n"
    "    jmp .Lhang32\n"
    ".code64\n"
    ".Llong_mode:\n"
    "    mov $0x10, %ax\n"
    "    mov %ax, %ds\n"
    "    mov %ax, %es\n"
    "    mov %ax, %ss\n"
    "    mov %ax, %fs\n"
    "    mov %ax, %gs\n"
    "    lea g_stack+16384, %rsp\n"
    "    call kern_boot_main\n"
    ".Lhang64:\n"
    "    cli\n"
    "    hlt\n"
    "    jmp .Lhang64\n"
);

/* ======================================================================
 * kern_boot_main — erster C-Code im Long-Mode.
 * ====================================================================== */
void kern_boot_main(void) {
    kern_seriell_init();
    kern_heap_init_c((uintptr_t)kern_heap_start, (uintptr_t)kern_heap_end);
    main();                       /* moo-Entry */
    for (;;) moo_cpu_halt();
}

#else  /* Nicht-x86_64: nur kern_boot_main-Stub, kein Trampolin */

void kern_boot_main(void) {
    kern_seriell_init();
    main();
    for (;;) moo_cpu_halt();
}

#endif

/* ======================================================================
 * kern_panic — gemeinsam (von alloc/idt referenziert).
 * ====================================================================== */
void kern_panic(const char* msg) {
    kern_seriell_text("\n[KERN-PANIK] ");
    kern_seriell_text(msg ? msg : "(ohne Meldung)");
    kern_seriell_text("\n");
    moo_cpu_cli();
    for (;;) moo_cpu_halt();
}

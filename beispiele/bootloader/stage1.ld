/* P011-C1: Stage1-Linker-Template — Boot-Sektor-Layout (ORG 0x7C00).
 *
 * Das BIOS laedt den ersten Sektor (512 Bytes) nach 0x7C00 und springt
 * dorthin (16-bit real mode). Dieses Script mappt AUSSCHLIESSLICH die
 * Input-Section .boot_s1 — alles andere (moo-Runtime, .text, .data, ...)
 * wird hart DISCARDED, damit nur der asm-Stage1-Code ins Sector-Image
 * gelangt (--emit sector gated dann auf <=510 Bytes + 0x55AA).
 *
 * EHRLICHE GRENZE (Roadmap P011): Stage1-Code bleibt mittelfristig ein
 * toolchain-verwaltetes asm-Template (compiler/runtime/boot/, P011-C2) —
 * LLVM x86-16 ist ein Sonderweg. Dieses Script macht den Emit-Pfad
 * generisch, nicht den Codegen 16-bit-faehig.
 *
 * Nutzung: moo-compiler compile <datei> --no-stdlib --target x86_64-bare \
 *              --linker_script beispiele/bootloader/stage1.ld \
 *              --emit sector -o stage1.img
 */
ENTRY(_start_s1)
SECTIONS
{
    . = 0x7C00;

    .boot_s1 : {
        KEEP(*(.boot_s1))
        KEEP(*(.boot_s1.*))
    }

    /DISCARD/ : {
        *(.text*) *(.rodata*) *(.data*) *(.bss*)
        *(.eh_frame*) *(.note*) *(.comment)
        *(.multiboot2*) *(.boot) *(.debug*)
    }
}

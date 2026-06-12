/* linker-arm64-virt.ld — P012-D5: qemu-virt-aarch64 Kernel-Link.
 * Load-/Link-Adresse 0x40080000 aus dem Board-Profil qemu-virt-aarch64
 * (boards.rs: RAM ab 0x40000000 + 0x80000 Text-Offset-Konvention).
 * KEIN Raspi-0x80000-Default — Raspi4 bekommt mit H1 ein eigenes Script.
 * __bss_start/__bss_end werden vom Entry (moo_bare_entry_arm64.c) zum
 * Nullen der BSS genutzt. */
ENTRY(_start)

SECTIONS
{
    . = 0x40080000;

    .text : {
        KEEP(*(.text.boot))      /* _start zuerst (ELF-Entry zeigt hierher) */
        *(.text .text.*)
    }

    .rodata : { *(.rodata .rodata.*) }

    .data : { *(.data .data.*) }

    . = ALIGN(8);
    __bss_start = .;
    .bss : { *(.bss .bss.*) *(COMMON) }
    . = ALIGN(8);
    __bss_end = .;

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

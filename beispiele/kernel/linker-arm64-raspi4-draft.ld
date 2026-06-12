/* linker-arm64-raspi4-draft.ld — P012-H1: Raspberry Pi 4 DRAFT.
 * STATUS: UNVERIFIZIERT — kein Gate, kein Smoke; echte Hardware-Tests
 * (User + serielles Kabel) stehen aus. Struktur identisch zu
 * linker-arm64-virt.ld, Link-Adresse 0x80000 = Firmware-Load von
 * kernel8.img (arm_64bit=1).
 * OFFENE HW-PUNKTE (siehe P012-H1-Thought): EL2->EL1-Drop im Entry
 * (Firmware startet EL2!), PL011-Baudrate-Divisoren, raw-Image via
 * objcopy (Firmware laedt KEIN ELF), dc cvac vor MMU-Enable. */
ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    .text : {
        KEEP(*(.text.boot))
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

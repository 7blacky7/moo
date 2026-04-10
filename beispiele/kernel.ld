/* Minimales Linker-Script fuer einen x86 Kernel */
ENTRY(_start)

SECTIONS
{
    . = 0x100000;  /* 1 MB — typische Kernel-Startadresse */

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
        *(COMMON)
    }
}

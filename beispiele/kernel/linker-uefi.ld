/* linker-uefi.ld — P7-T5: Moo-Kern fuer den Moo-UEFI-Loader (beispiele/uefi/moo_lader.moos).
 *
 * Kein Multiboot: der Kern liegt virtuell ab 0xFFFFFFFF80000000 (obere
 * Haelfte, passend zu -mcmodel=kernel der C-Runtime). Der Loader legt alle
 * PT_LOAD in freie physische Seiten beliebiger Lage, bildet sie per eigener
 * Seitentabelle hier ab und springt im 64-Bit-Modus nach ENTRY mit der
 * Boot-Info (physisch, identisch abgebildet) in rdi (SysV). Der Kern setzt
 * seinen Stack selbst (kern_stapel_ende). Befund P7-T5: eine feste physische
 * Adresse (16 MiB) war in OVMF belegt.
 *
 * Verwendung:
 *   moo compile beispiele/kernel/uefi_kern.moos --no-stdlib --target x86_64-bare \
 *       --kernel --linker-script beispiele/kernel/linker-uefi.ld -o kern.elf
 */
ENTRY(moo_kern_start)

SECTIONS
{
    /* Zuerst verwerfen: der GRUB-Pfad (moo_bare_boot.c) gehoert nicht hinein. */
    /DISCARD/ :
    {
        *(.multiboot2)
        *(.text.boot)
        *(.boot_s1*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.note .note.*)
        *(.comment)
    }

    . = 0xFFFFFFFF80000000;

    .text ALIGN(4K) :
    {
        *(.text .text.*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)
    }

    .data ALIGN(4K) :
    {
        *(.data .data.*)
    }

    .bss ALIGN(4K) :
    {
        *(COMMON)
        *(.bss .bss.*)
        . = ALIGN(16);
        kern_stapel = .;
        . += 64K;
        kern_stapel_ende = .;
    }

    /* Nur fuer den (hier ungenutzten) Bare-Allocator der C-Runtime. */
    . = ALIGN(4K);
    kern_heap_start = .;
    kern_heap_end = .;
}

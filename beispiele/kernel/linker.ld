/* linker.ld — Multiboot2 x86_64 Kernel-Linkerscript (Plan-010 K5).
 *
 * Legt den Multiboot2-Header an den Anfang (muss in den ersten 32 KB der
 * Datei liegen, damit GRUB ihn findet) und definiert Heap-Symbole fuer den
 * Bare-Allocator (kern_speicher_init).
 *
 * Verwendung (ueber die One-Shot-Pipeline, P010-C2):
 *   moo compile beispiele/kernel/hallo_kern.moo --target x86_64-bare --kernel
 */
ENTRY(_start)

SECTIONS
{
    /* 1 MB — klassische Kernel-Ladeadresse (unter dem PCI-Loch). */
    . = 1M;

    /* Multiboot2-Header ZUERST, dann Boot-Text. */
    .boot ALIGN(4K) :
    {
        KEEP(*(.multiboot2))
        *(.text.boot)
    }

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
    }

    /* Heap-Region fuer den Bare-Allocator: ab Ende der .bss bis 64 MB. */
    . = ALIGN(4K);
    kern_heap_start = .;
    . = 64M;
    kern_heap_end = .;

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.note .note.*)
        *(.comment)
    }
}

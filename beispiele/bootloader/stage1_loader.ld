/* P011-F1: Linker-Script fuer Stage1 (16-bit Boot-Sektor, ORG 0x7C00). */
ENTRY(_start)
SECTIONS {
  . = 0x7C00;
  .boot : { stage1_loader.o(.boot) stage1_loader.o(.boot_gdt) }
  /DISCARD/ : { *(.eh_frame .note.* .comment) }
}

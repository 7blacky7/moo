/* P011-F1: Linker-Script fuer Stage2 (32-bit, geladen nach 0x8000).
 *
 * KRITISCH: .s2text (mit _s2_start) MUSS als allererstes bei 0x8000 liegen,
 * damit der far-jump aus Stage1 (ljmp 0x08:0x8000) im Entry landet. Wir
 * matchen die SECTION global (*(.s2text)) statt nach Objektnamen — letzteres
 * ist fragil (Basename muss exakt passen) und hat _s2_start faelschlich
 * hinter .text einsortiert. Der explizite KEEP + erste Position garantiert
 * die richtige Entry-Adresse unabhaengig vom Objekt-Dateinamen. */
ENTRY(_s2_start)
SECTIONS {
  . = 0x8000;
  .text : {
    KEEP(*(.s2text))
    *(.text .text.*)
  }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.s2data) *(.data .data.*) }
  .bss : { *(.s2bss) *(.bss .bss.* COMMON) }
  /DISCARD/ : { *(.eh_frame .note.* .comment) }
}

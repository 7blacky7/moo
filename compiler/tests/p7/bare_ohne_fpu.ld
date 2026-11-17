/* P7-T21: mit clang -E -P -x c und denselben Targetflags vorverarbeiten. */
ENTRY(_start)
#if defined(__arm__)
MEMORY {
    FLASH (rx) : ORIGIN = 0, LENGTH = 256K
    RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}
#else
MEMORY {
    FLASH (rx) : ORIGIN = 0x80000000, LENGTH = 16M
    RAM (rwx) : ORIGIN = 0x81000000, LENGTH = 16M
}
#endif
SECTIONS {
    .text : { KEEP(*(.vectors)) KEEP(*(.text.start)) *(.text*) *(.rodata*) } > FLASH
#if defined(__arm__)
    .data : ALIGN(8) { __data_start = .; *(.data*) *(.sdata*) . = ALIGN(8); __data_end = .; } > RAM AT> FLASH
#else
    .data : ALIGN(8) { __data_start = .; *(.data*) *(.sdata*) . = ALIGN(8); __data_end = .; } > RAM
#endif
    __data_load = LOADADDR(.data);
    .bss (NOLOAD) : ALIGN(8) { __bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(8); __bss_end = .; } > RAM
    __stack = ORIGIN(RAM) + LENGTH(RAM);
    /DISCARD/ : { *(.eh_frame*) *(.comment*) }
}

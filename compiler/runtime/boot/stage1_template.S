/* P011-C2: Minimales Stage1-Template — toolchain-verwaltetes asm.
 *
 * EHRLICHE GRENZE (Roadmap P011): Stage1 (16-bit real mode) bleibt
 * mittelfristig asm — LLVM x86-16 ist ein Sonderweg. Dieses Template wird
 * von build_kernel automatisch mitkompiliert; das Kernel-Linker-Script
 * DISCARDed .boot_s1, das Stage1-Script (beispiele/bootloader/stage1.ld)
 * KEEPt ausschliesslich .boot_s1.
 *
 * SERIAL-KLAERUNG (empirisch, P011-C2): BIOS int 0x10 schreibt nur auf
 * VGA — auf `qemu -serial stdio` erscheint davon NICHTS. Der Smoke-Marker
 * geht deshalb direkt an COM1 (0x3F8): Mini-UART-Init (115200 8N1) +
 * LSR-Polling (Bit 5 = THR leer) + Zeichenschleife.
 *
 * Ablauf: cli, Segmente/Stack nullen, COM1 init, 'MOO-BOOT-S1-OK' senden,
 * dann hlt-Schleife (QEMU-Smoke beendet via timeout, rc 124 = erwartet).
 */
    .section .boot_s1, "ax"
    .code16
    .global _start_s1
_start_s1:
    cli
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7C00, %sp

    /* COM1 init: DLAB an, Divisor 1 (115200), 8N1, DLAB aus */
    movw    $0x3FB, %dx
    movb    $0x80, %al
    outb    %al, %dx
    movw    $0x3F8, %dx
    movb    $1, %al
    outb    %al, %dx
    movw    $0x3F9, %dx
    xorb    %al, %al
    outb    %al, %dx
    movw    $0x3FB, %dx
    movb    $0x03, %al
    outb    %al, %dx

    movw    $s1_msg, %si
sende_zeichen:
    lodsb
    testb   %al, %al
    jz      fertig
    /* LSR-Polling: warten bis Transmitter Holding Register leer (Bit 5) */
warte_thr:
    movw    $0x3FD, %dx
    movb    %al, %ah
    inb     %dx, %al
    testb   $0x20, %al
    movb    %ah, %al
    jz      warte_thr
    movw    $0x3F8, %dx
    outb    %al, %dx
    jmp     sende_zeichen
fertig:
    hlt
    jmp     fertig

s1_msg:
    .asciz  "MOO-BOOT-S1-OK\r\n"

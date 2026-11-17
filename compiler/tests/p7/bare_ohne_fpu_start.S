/* P7-T21: nur Stack/Datenstart; Rechnen und Pruefen bleiben Moo/C.
 * Als Assembler mit C-Praeprozessor fuer Cortex-M3 oder RV64IMAC bauen. */
#if defined(__arm__)
.syntax unified
.thumb
.section .vectors,"a",%progbits
.word __stack
.word _start
.section .text.start,"ax",%progbits
.global _start
.thumb_func
_start:
    ldr r0, =__data_load
    ldr r1, =__data_start
    ldr r2, =__data_end
1:  cmp r1, r2
    bhs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:  ldr r1, =__bss_start
    ldr r2, =__bss_end
    movs r3, #0
3:  cmp r1, r2
    bhs 4f
    str r3, [r1], #4
    b 3b
4:  bl test_main
5:  b 5b
#elif defined(__riscv)
.section .text.start,"ax",@progbits
.global _start
.option norelax
_start:
    la sp, __stack
    la t0, __bss_start
    la t1, __bss_end
1:  bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:  call test_main
3:  j 3b
#else
#error Nicht unterstuetztes Testboard
#endif

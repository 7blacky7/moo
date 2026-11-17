/* P7-T21: Ergebnisadapter, keine Ersatz-Arithmetik. Der echte Moo-Code und
 * moo_bare.c werden getrennt fuer dieselbe CPU uebersetzt und gelinkt.
 * QEMU v11.0.1 hw/arm/stellaris.c bzw. hw/riscv/virt.c belegen die Karten.
 * ARM-Ausgabe/Exit: Standard-Semihosting; RV: UART + SiFive-Testgeraet. */
#include <stdint.h>
typedef struct { uint64_t tag, data; } MooValue;
extern int main(void);
static unsigned anzahl;
static const double erwartet[] = {
    252747, 5, 15, 10, -14, 26, -4, -126373.5, 2997, -1.5, 21, 22, 777
};
static void ausgabe(const char *text) {
#if defined(__arm__)
    register uint32_t op __asm__("r0") = 4;
    register const char *arg __asm__("r1") = text;
    __asm__ volatile("bkpt 0xab" : "+r"(op), "+r"(arg) : : "memory");
#elif defined(__riscv)
    volatile uint8_t *uart = (volatile uint8_t *)(uintptr_t)0x10000000;
    while (*text) *uart = (uint8_t)*text++;
#else
#error Nicht unterstuetztes Testboard
#endif
}
__attribute__((noreturn)) static void ende(unsigned fehler) {
    ausgabe(fehler ? "MOO-OHNE-FPU FAIL\n" : "MOO-OHNE-FPU PASS 13\n");
#if defined(__arm__)
    uint32_t grund[2] = { 0x20026, fehler };
    register uint32_t op __asm__("r0") = 0x20;
    register uint32_t *arg __asm__("r1") = grund;
    __asm__ volatile("bkpt 0xab" : "+r"(op), "+r"(arg) : : "memory");
#elif defined(__riscv)
    *(volatile uint32_t *)(uintptr_t)0x100000 = fehler ? 0x13333 : 0x5555;
#endif
    for (;;) __asm__ volatile("" : : : "memory");
}
void moo_print(MooValue wert) {
    uint64_t bits;
    if (anzahl >= sizeof(erwartet) / sizeof(erwartet[0])) ende(1);
    __builtin_memcpy(&bits, &erwartet[anzahl], sizeof(bits));
    if (wert.tag != 0 || wert.data != bits) ende(1);
    ++anzahl;
}
void test_main(void) {
    main();
    ende(anzahl != sizeof(erwartet) / sizeof(erwartet[0]));
}

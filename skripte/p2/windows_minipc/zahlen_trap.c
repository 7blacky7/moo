/* P2-T73: Fehlervertrag der C-Exporte auf Windows x64 (ADR §11/§13).
 * Division durch 0 in einem @einstieg-Export endet per llvm.trap (ud2):
 * Windows beendet den Prozess mit STATUS_ILLEGAL_INSTRUCTION (0xC000001D),
 * ein Exitcode != 0. 'NICHT ERREICHT' darf nie erscheinen. */
#include <stdint.h>
#include <stdio.h>

extern int32_t moo_win_div(int32_t a, int32_t b);

int main(void) {
    volatile int32_t null = 0;
    puts("vorher");
    fflush(stdout);
    int32_t ergebnis = moo_win_div(7, null);
    printf("NICHT ERREICHT %ld\n", (long)ergebnis);
    return 0;
}

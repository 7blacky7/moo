/* P2-T21/T57 Batch E (Plan f4655d20 E-5): fehlbarer Rueckruf im runtime-freien
 * Objekt -> schwacher Hook trappt im Rueckruf (kein Fortlauf, kein erfundener
 * Wert an den C-Aufrufer). */
#include <stdint.h>
#include <stdio.h>

int32_t moo_win_rr_teile(int32_t n);

int main(void) {
    printf("vor\n");
    fflush(stdout);
    int32_t r = moo_win_rr_teile(5);
    printf("NICHT ERREICHT %d\n", (int)r);
    return 0;
}

/* P2-T66: ruft die hosted-Linux-Exporte aus probe_hosted.moos (SysV). */
#include <stdint.h>
#include <stdio.h>

typedef struct { uint32_t a, b; } Paar;
uint32_t hosted_summe(const uint32_t *p, uint64_t n);
uint32_t hosted_tausche(Paar *p);
uint8_t *hosted_fuellen(uint8_t *p, uint64_t n);
int32_t hosted_klein(int32_t a, int32_t b, int32_t c, int32_t d);

int main(void) {
    uint32_t w[5] = {1, 2, 3, 4, 5};
    Paar p = {7, 9};
    uint8_t b[8] = {0};
    int fehler = 0;
    fehler += hosted_summe(w, 5) != 15;
    fehler += hosted_tausche(&p) != 9 || p.a != 9 || p.b != 7;
    /* Ein-Aufruf-extern-C mit Zeigern: Rueckgabe ist der uebergebene Zeiger. */
    fehler += hosted_fuellen(b, sizeof b) != b;
    for (size_t i = 0; i < sizeof b; i++) fehler += b[i] != 42;
    /* 8/16-Bit-Roundtrips ueber extern C (signext/zeroext an der Aufrufstelle). */
    fehler += hosted_klein(-5, 200, -300, 60000) != 59895;
    printf("%s hosted Zeiger+Strukturwert+extern-C-Zeiger ueber C-Aufrufer\n", fehler ? "FAIL" : "PASS");
    return fehler;
}

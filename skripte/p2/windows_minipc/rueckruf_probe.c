/* P2-T21/T57 Batch E (Plan f4655d20 E-5): C-Harness fuer rueckruf_probe.moos
 * auf Windows x64. Erwartungen identisch zu compiler/tests/p2/rueckruf_sicher. */
#include <stdint.h>
#include <stdio.h>

uint32_t moo_win_rr_sortiere(uint64_t adr, uint64_t n);
int32_t moo_win_rr_besuche(int32_t n);
int32_t moo_win_rr_melde(int32_t n);
int64_t moo_win_rr_tausche(int32_t a, int64_t b);
int32_t moo_win_rr_merke(void);
int32_t moo_win_rr_loese_aus(int32_t x);
int32_t moo_win_rr_thread(int32_t x);

static int fehler;
static int faelle;

static void pruefe(const char *was, int64_t ist, int64_t soll) {
    faelle++;
    if (ist != soll) {
        fehler++;
        printf("FEHLER %s: %lld statt %lld\n", was, (long long)ist, (long long)soll);
    }
}

int main(void) {
    int32_t werte[7] = { 5, -3, INT32_MAX, -3, 0, INT32_MIN, 9 };
    const int32_t soll[7] = { INT32_MIN, -3, -3, 0, 5, 9, INT32_MAX };
    pruefe("qsort", moo_win_rr_sortiere((uint64_t)(uintptr_t)werte, 7), 1);
    for (int i = 0; i < 7; i++) pruefe("qsort-element", werte[i], soll[i]);
    pruefe("user_data", moo_win_rr_besuche(4), 40610);
    pruefe("void", moo_win_rr_melde(3), 305);
    pruefe("struktur", moo_win_rr_tausche(7, 21), 8000042);
    pruefe("merke", moo_win_rr_merke(), 1);
    pruefe("loese_aus", moo_win_rr_loese_aus(21), 42);
    pruefe("fremdthread", moo_win_rr_thread(5), 10006);
    if (fehler != 0) {
        printf("MOO_WIN_RUECKRUF_FEHLER %d/%d\n", fehler, faelle);
        return 1;
    }
    printf("MOO_WIN_RUECKRUF_OK %d/%d\n", faelle, faelle);
    return 0;
}

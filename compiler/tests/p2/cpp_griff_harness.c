/* P2-T23/T53 (Plan 36aca1f2 D1): C-Harness mit derselben Aufruffolge wie
 * compiler/tests/p2/cpp_griff.moos gegen den C++-Adapter (m1_cpp_adapter.cpp),
 * unter ASan/UBSan/LSan (skripte/p2/cpp_adapter/test.sh griff-sanitize). */
#include "m1_cpp_adapter.h"
#include <stdio.h>
#include <string.h>

static int fehler;

static void pruefe(int ok, const char *was) {
    if (!ok) { fehler++; printf("FEHLER %s\n", was); }
}

int main(void) {
    static int32_t werte[5000];
    for (int i = 0; i < 5000; i++) werte[i] = i + 1;
    const char *name = "Z\xc3\xa4hler Eins";
    uint8_t puffer[128];

    P2Zaehler *z = p2_zaehler_neu();
    pruefe(z != NULL, "neu");
    pruefe(p2_zaehler_lebende() == 1, "lebende 1");
    pruefe(p2_zaehler_name_setzen(z, (const uint8_t *)name, strlen(name)) == 0, "name setzen");
    pruefe(p2_zaehler_addiere(z, werte, 10) == 0, "addiere 10");
    pruefe(p2_zaehler_summe(z) == 55, "summe 55");
    uint64_t n = p2_zaehler_name(z, puffer, 64);
    pruefe(n == strlen(name) && memcmp(puffer, name, n) == 0, "name lesen");
    pruefe(p2_zaehler_addiere(z, werte, 5000) == -1, "obergrenze");
    n = p2_letzter_fehler(puffer, sizeof(puffer));
    pruefe(n == strlen("zu viele Werte (hoechstens 4096)")
           && memcmp(puffer, "zu viele Werte (hoechstens 4096)", n) == 0, "letzter fehler");
    pruefe(p2_zaehler_summe(z) == 55, "summe nach fehler");
    void *zahl = p2_cpp_create(7);
    pruefe(zahl != NULL && p2_cpp_value(zahl) == 7, "zweites objekt");
    pruefe(p2_zaehler_frei(z) == 0, "frei");
    pruefe(p2_cpp_destroy(zahl) == 0, "zahl frei");
    pruefe(p2_zaehler_lebende() == 0, "lebende 0");
    if (fehler) return 1;
    printf("PASS C-Harness Griff-Aufruffolge (ASan/UBSan/LSan)\n");
    return 0;
}

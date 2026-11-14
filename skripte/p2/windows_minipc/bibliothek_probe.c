/* P2-T43: C-Harness fuer bibliothek_probe.moos auf echtem Windows x64.
 * Prueft Moo -> eigene statische Bibliothek und Moo -> eigene DLL gegen die
 * erwarteten Werte und dass die DLL wirklich als Modul geladen ist. */
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

int32_t moo_win_lib_statisch(int32_t wert);
int32_t moo_win_lib_dll(int32_t wert);

int main(void) {
    static const int32_t werte[] = {0, 1, -1, 7, 41, -1000, 123456, INT32_MAX, INT32_MIN};
    unsigned fehler = 0;
    for (size_t i = 0; i < sizeof werte / sizeof werte[0]; ++i) {
        int32_t w = werte[i];
        int32_t s = (int32_t)((uint32_t)w * 3u);
        int32_t d = (int32_t)((uint32_t)w + 7u);
        int32_t ms = moo_win_lib_statisch(w);
        int32_t md = moo_win_lib_dll(w);
        if (ms != s || md != d) {
            printf("FEHLER wert=%ld statisch=%ld/%ld dll=%ld/%ld\n",
                   (long)w, (long)ms, (long)s, (long)md, (long)d);
            ++fehler;
        }
    }
    if (GetModuleHandleA("p2_win_dll.dll") == NULL) {
        printf("FEHLER p2_win_dll.dll ist nicht geladen\n");
        ++fehler;
    }
    printf("BIBLIOTHEK faelle=%u fehler=%u\n", (unsigned)(sizeof werte / sizeof werte[0]), fehler);
    return fehler == 0 ? 0 : 1;
}

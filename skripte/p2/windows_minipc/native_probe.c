/* Echte Windows-Laufzeit-Gegenprobe. Der Berechnungscode ist Moo,
 * dieses C-Harness prueft nur unabhaengig das Win64-ABI und den Exitcode. */
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

extern int32_t moo_win_plus42(int32_t wert);
extern int32_t moo_win_quadrat(int32_t wert);
extern int32_t moo_win_kette(int32_t wert);
extern uint32_t moo_win_api_pid(void);

static int fehler = 0;

static void pruefe(const char *name, int32_t erhalten, int32_t erwartet) {
    if (erhalten != erwartet) {
        fprintf(stderr, "FAIL %s: %ld statt %ld\n",
                name, (long)erhalten, (long)erwartet);
        fehler++;
    }
}

int main(void) {
    pruefe("plus42-null", moo_win_plus42(0), 42);
    pruefe("plus42-positiv", moo_win_plus42(100), 142);
    pruefe("plus42-negativ", moo_win_plus42(-12), 30);
    pruefe("quadrat-null", moo_win_quadrat(0), 0);
    pruefe("quadrat-negativ", moo_win_quadrat(-5), 25);
    pruefe("kette", moo_win_kette(4), 62);
    pruefe("win32-api-pid", (int32_t)moo_win_api_pid(), (int32_t)GetCurrentProcessId());
    if (fehler != 0) {
        fprintf(stderr, "MOO_WIN64_NATIVE_FAIL: %d Fehler\n", fehler);
        return 1;
    }
    puts("MOO_WIN64_NATIVE_OK: 7/7");
    return 0;
}

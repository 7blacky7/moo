/* P2-T84: C-Zwischenschicht, die die Moo-Exporte aus t84_export_abbruch.moos
 * ruft (Moo -> C -> Moo). T84_WAHL waehlt den Pfad: teile (Pure-Export),
 * paar (Struktur-Export ueber den C-ABI-Thunk), intern (Export ruft eine
 * Moo-interne fehlbare Funktion). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int64_t a, b; } Paar;
int64_t t84_teile(int64_t a, int64_t b);
int64_t t84_paar(Paar p);
int64_t t84_ueber_intern(int64_t a, int64_t b);

int64_t t84_rufe(int64_t a, int64_t b) {
    const char *wahl = getenv("T84_WAHL");
    if (wahl && strcmp(wahl, "paar") == 0) {
        Paar p = { a, b };
        return t84_paar(p);
    }
    if (wahl && strcmp(wahl, "intern") == 0) return t84_ueber_intern(a, b);
    return t84_teile(a, b);
}

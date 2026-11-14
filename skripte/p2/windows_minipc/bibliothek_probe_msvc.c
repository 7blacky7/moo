/* P2-T43: CRT-freier MSVC-ABI-Einstieg fuer bibliothek_probe.moos auf echtem
 * Windows x64. Gelinkt mit lld-link ohne -l: die Bibliotheken p2_win_statisch.lib
 * und p2_win_dll.lib kommen ausschliesslich aus der .drectve des Moo-Objekts
 * (/DEFAULTLIB, P2-T43 Batch 32999). Nur kernel32 wird explizit gelinkt. */
#include <stdint.h>

int32_t moo_win_lib_statisch(int32_t wert);
int32_t moo_win_lib_dll(int32_t wert);

__declspec(dllimport) void __stdcall ExitProcess(unsigned code);
__declspec(dllimport) void *__stdcall GetModuleHandleA(const char *name);
__declspec(dllimport) void *__stdcall GetStdHandle(unsigned long handle);
__declspec(dllimport) int __stdcall WriteFile(void *datei, const void *daten, unsigned long laenge,
                                              unsigned long *geschrieben, void *overlapped);

static void schreibe(const char *text) {
    unsigned long laenge = 0, geschrieben = 0;
    while (text[laenge]) ++laenge;
    WriteFile(GetStdHandle((unsigned long)-11), text, laenge, &geschrieben, 0);
}

void moo_msvc_start(void) {
    static const int32_t werte[] = {0, 1, -1, 7, 41, -1000, 123456, INT32_MAX, INT32_MIN};
    unsigned fehler = 0;
    for (unsigned i = 0; i < sizeof werte / sizeof werte[0]; ++i) {
        int32_t w = werte[i];
        if (moo_win_lib_statisch(w) != (int32_t)((uint32_t)w * 3u)) ++fehler;
        if (moo_win_lib_dll(w) != (int32_t)((uint32_t)w + 7u)) ++fehler;
    }
    if (!GetModuleHandleA("p2_win_dll.dll")) ++fehler;
    schreibe(fehler ? "MOO_WIN64_MSVC_BIBLIOTHEK_FEHLER\r\n"
                    : "MOO_WIN64_MSVC_BIBLIOTHEK_OK: 9/9 statisch+DLL\r\n");
    ExitProcess(fehler ? 1u : 0u);
}

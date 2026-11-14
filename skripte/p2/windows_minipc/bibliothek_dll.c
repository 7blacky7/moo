/* P2-T43: eigene DLL p2_win_dll.dll mit Importbibliothek libp2_win_dll.dll.a. */
#include <stdint.h>

__declspec(dllexport) int32_t p2_win_dll_plus7(int32_t wert) {
    return (int32_t)((uint32_t)wert + 7u);
}

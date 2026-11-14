/* P2-T43: eigene statische Bibliothek (Archiv libp2_win_statisch.a). */
#include <stdint.h>

int32_t p2_win_statisch_mal3(int32_t wert) {
    /* Unsigned-Wrap ist gewollt: definierte Semantik auch bei Ueberlauf. */
    return (int32_t)((uint32_t)wert * 3u);
}

#include <stdint.h>

struct P2FontInfo {
    const uint8_t *glyphen;
    uint32_t breite;
};

const uint8_t p2_font_daten[2] = {3, 7};
static const uint8_t p2_glyphen[1] = {5};
const struct P2FontInfo p2_font_info = {p2_glyphen, 19};

extern uint32_t p2_t129_leseprobe(void);

int main(void) {
    return p2_t129_leseprobe() == 31 ? 0 : 1;
}

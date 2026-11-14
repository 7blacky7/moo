#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint16_t klein;
    uint8_t flagge;
} Innen;

typedef struct __attribute__((packed)) {
    uint8_t marke;
    Innen kern;
    uint32_t werte[3];
} Paket;

_Static_assert(sizeof(Innen) == 4 && _Alignof(Innen) == 2, "Innen");
_Static_assert(offsetof(Innen, klein) == 0 && offsetof(Innen, flagge) == 2, "Innen offsets");
_Static_assert(sizeof(Paket) == 17 && _Alignof(Paket) == 1, "Paket");
_Static_assert(offsetof(Paket, marke) == 0 && offsetof(Paket, kern) == 1 &&
               offsetof(Paket, werte) == 5, "Paket offsets");

int main(void) {
    return printf("%zu %zu %zu %zu\n%zu %zu %zu %zu %zu\n",
        sizeof(Innen), _Alignof(Innen), offsetof(Innen, klein), offsetof(Innen, flagge),
        sizeof(Paket), _Alignof(Paket), offsetof(Paket, marke),
        offsetof(Paket, kern), offsetof(Paket, werte)) < 0 ? 1 : 0;
}

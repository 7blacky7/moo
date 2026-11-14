#include <cstddef>
#include <cstdint>
#include <cstdio>
struct P2PointerTabelleCpp {
    std::uint32_t status;
    std::uint16_t* daten;
    std::uint64_t (*ausgabe)(std::uint8_t*, std::uint16_t*);
};
extern "C" std::size_t p2_pointer_layout_size();
extern "C" std::size_t p2_pointer_layout_align();
extern "C" std::size_t p2_pointer_layout_status();
extern "C" std::size_t p2_pointer_layout_daten();
extern "C" std::size_t p2_pointer_layout_ausgabe();
int main() {
    if (p2_pointer_layout_size() != sizeof(P2PointerTabelleCpp) ||
        p2_pointer_layout_align() != alignof(P2PointerTabelleCpp) ||
        p2_pointer_layout_status() != offsetof(P2PointerTabelleCpp, status) ||
        p2_pointer_layout_daten() != offsetof(P2PointerTabelleCpp, daten) ||
        p2_pointer_layout_ausgabe() != offsetof(P2PointerTabelleCpp, ausgabe)) return 1;
    std::printf("C/C++ pointer field layout PASS: size=%zu align=%zu data=%zu function=%zu\n",
        p2_pointer_layout_size(), p2_pointer_layout_align(), p2_pointer_layout_daten(), p2_pointer_layout_ausgabe());
}

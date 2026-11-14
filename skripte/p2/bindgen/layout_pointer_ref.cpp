#include <cstddef>
#include <cstdio>

extern "C" std::size_t p2_pointer_layout_size(void);
extern "C" std::size_t p2_pointer_layout_align(void);
extern "C" std::size_t p2_pointer_layout_status(void);
extern "C" std::size_t p2_pointer_layout_daten(void);
extern "C" std::size_t p2_pointer_layout_ausgabe(void);

int main() {
    return std::printf("%zu %zu %zu %zu %zu\n",
        p2_pointer_layout_size(), p2_pointer_layout_align(),
        p2_pointer_layout_status(), p2_pointer_layout_daten(),
        p2_pointer_layout_ausgabe()) < 0 ? 1 : 0;
}

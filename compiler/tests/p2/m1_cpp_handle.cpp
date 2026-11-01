#include <cstdint>
#include <cstdio>
extern "C" std::int32_t p2_handle_runde(std::int32_t) asm("__moo_native.p2_handle_runde");
int main() {
    if (p2_handle_runde(42) != 42) return 1;
    if (p2_handle_runde(-1) != -1) return 2;
    std::puts("PASS Moo/C++ handle ownership");
}

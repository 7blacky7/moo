#include <cstdint>
#include <cstdio>
extern "C" void p2_indirekt_void(void (*)(std::int32_t), std::int32_t) asm("__moo_native.p2_indirekt_void");
static std::int32_t observed;
extern "C" void record(std::int32_t value) { observed = value; }
int main() {
    p2_indirekt_void(record, -12345);
    if (observed != -12345) return 1;
    p2_indirekt_void(record, 67890);
    if (observed != 67890) return 2;
    std::puts("PASS indirekter void-Aufruf");
}

#include <cstdint>
#include <cstdio>
#include <limits>
#define REG_DECL(B, T) \
extern "C" void w1c_##B(T*, T) asm("__moo_native.p126_w1c_" #B); \
extern "C" void write0_##B(T*, T, T) asm("__moo_native.p126_write0_" #B)
REG_DECL(8, std::uint8_t);
REG_DECL(16, std::uint16_t);
REG_DECL(32, std::uint32_t);
REG_DECL(64, std::uint64_t);
extern "C" void normal(std::uint8_t*, std::uint8_t) asm("__moo_native.p126_normal");
extern "C" void sys_w1c(std::uint8_t*, std::uint8_t) asm("__moo_native.p126_sys_w1c");
extern "C" void sys_write0(std::uint8_t*, std::uint8_t, std::uint8_t) asm("__moo_native.p126_sys_write0");
extern "C" void feld_w1c(std::uint64_t, std::uint8_t) asm("__moo_native.p126_feld_w1c");
extern "C" void feld_write0(std::uint64_t, std::uint8_t, std::uint8_t) asm("__moo_native.p126_feld_write0");
static unsigned cases;
template<class T> static bool probe(void (*ack)(T*, T), void (*zero)(T*, T, T)) {
    const T all = std::numeric_limits<T>::max();
    const T before = static_cast<T>(UINT64_C(0xD6A509E712345609));
    for (unsigned bit = 0; bit < sizeof(T) * 8; ++bit) {
        const T mask = static_cast<T>(T(1) << bit);
        T bus = static_cast<T>(~mask);
        ack(&bus, mask);
        if (bus != mask) return false;
        bus = static_cast<T>(~before);
        zero(&bus, before, mask);
        if (bus != static_cast<T>(before & static_cast<T>(~mask))) return false;
        cases += 2;
    }
    T bus = all;
    ack(&bus, 0);
    if (bus != 0) return false;
    zero(&bus, all, all);
    if (bus != 0) return false;
    zero(&bus, before, 0);
    if (bus != before) return false;
    cases += 3;
    return true;
}
int main() {
    if (!probe(w1c_8, write0_8) || !probe(w1c_16, write0_16)
        || !probe(w1c_32, write0_32) || !probe(w1c_64, write0_64)
        || !probe(sys_w1c, sys_write0)) return 1;
    std::uint8_t bus = 0;
    normal(&bus, 0x81);
    if (bus != 0x81) return 1;
    const auto adr = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&bus));
    feld_w1c(adr, 0x04);
    if (bus != 0x04) return 1;
    bus = 0xF0;  // Die Quelle ist der explizite Snapshot 09, kein versteckter Registerread.
    feld_write0(adr, 0x09, 0x01);
    if (bus != 0x08) return 1;  // IT8613 CTRL[0] quittiert, Taktbit3 erhalten.
    std::printf("PASS Registerwrites: %u C++-Orakelpruefungen\n", cases + 3);
}

// P10-T6: nur Bytes kopieren, 16-/32-Bit-Code niemals im Hosted-Prozess ausfuehren.
#include <cstdint>
#include <cstdio>
#include <cstring>
extern "C" std::uint64_t anfang() asm("__moo_native.probe_trampolin_anfang");
extern "C" std::uint64_t ende() asm("__moo_native.probe_trampolin_ende");
int main(int argc, char **argv) {
    const auto a = anfang(), e = ende();
    const unsigned char erwartung[] = {0x66, 0x89, 0xc3, 0x89, 0xc3, 0xc3};
    if (argc != 2 || e < a || e - a != sizeof(erwartung)) return 1;
    const auto *bytes = reinterpret_cast<const void *>(static_cast<std::uintptr_t>(a));
    if (std::memcmp(bytes, erwartung, sizeof(erwartung)) != 0) return 2;
    auto *datei = std::fopen(argv[1], "wb");
    if (!datei) return 3;
    const bool geschrieben = std::fwrite(bytes, 1, sizeof(erwartung), datei) == sizeof(erwartung);
    if (std::fclose(datei) != 0 || !geschrieben) return 4;
    std::puts("PASS Trampolin: sektion_ende - sektion_anfang = 6, Bytes 66 89 c3 89 c3 c3");
}

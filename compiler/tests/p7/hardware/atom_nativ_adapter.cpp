// P2-T114 Phase B: C++-Probe fuer die @nativ-Atomics aus atom_nativ.moos.
// Vier Threads zaehlen je 200000 ueber atom_addieren; ohne echte Atomics
// gingen Inkremente verloren.
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>
extern "C" std::uint64_t atom_zaehle(std::uint64_t*, std::uint64_t) asm("__moo_native.atom_zaehle");
extern "C" std::uint64_t atom_lies(std::uint64_t*) asm("__moo_native.atom_lies");
extern "C" void atom_setze(std::uint32_t*, std::uint32_t) asm("__moo_native.atom_setze");
extern "C" std::uint32_t atom_tausch(std::uint32_t*, std::uint32_t) asm("__moo_native.atom_tausch");
extern "C" bool atom_cas(std::uint32_t*, std::uint32_t, std::uint32_t) asm("__moo_native.atom_cas");
int main() {
    alignas(8) std::uint64_t zaehler = 0;
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&] { for (int i = 0; i < 200000; ++i) atom_zaehle(&zaehler, 1); });
    for (auto& t : ts) t.join();
    if (atom_lies(&zaehler) != 800000u) { std::printf("zaehler=%llu\n", (unsigned long long)zaehler); return 1; }
    alignas(4) std::uint32_t w = 0;
    atom_setze(&w, 5);
    if (atom_tausch(&w, 7) != 5u || w != 7u) return 2;
    if (!atom_cas(&w, 7, 9) || w != 9u) return 3;
    if (atom_cas(&w, 7, 11) || w != 9u) return 4;
    std::puts("PASS atom_laden/speichern/addieren/tauschen/vergleichen_und_tauschen");
}

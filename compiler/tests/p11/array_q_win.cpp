#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

extern "C" std::uint64_t array_q_probe_alle();
extern "C" void simd_orakel_32_auto(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" void simd_orakel_32_gfni128(const std::uint8_t*, const std::uint8_t*, std::uint8_t*);
extern "C" std::uint64_t array_q_bench(std::uint32_t);
extern "C" std::uint64_t array_q_bench_pruefsumme();
static std::vector<std::uint8_t> captured;

// Test-FFI: dieselbe Moo-Probe schreibt ihr Orakelprotokoll in diesen Puffer.
extern "C" std::int64_t __wrap_write(int fd, const std::uint8_t* bytes, std::uint64_t length) {
    if (fd != 1 || length > 4096) return -1;
    captured.insert(captured.end(), bytes, bytes + length);
    return static_cast<std::int64_t>(length);
}
struct QZeit { std::int64_t sekunden, nano; };
static_assert(sizeof(QZeit) == 16);
extern "C" int __wrap_clock_gettime(int art, QZeit* out) {
    LARGE_INTEGER frequency, now;
    if (art != 1 || !QueryPerformanceFrequency(&frequency) || !QueryPerformanceCounter(&now)) return -1;
    out->sekunden = now.QuadPart / frequency.QuadPart;
    out->nano = (now.QuadPart % frequency.QuadPart) * 1000000000LL / frequency.QuadPart;
    return 0;
}
// Unabhängige carry-less Multiplikation mit anschließender Polynomdivision.
static std::uint8_t mul(unsigned a, unsigned b) {
    unsigned result = 0;
    for (unsigned i = 0; i < 8; ++i) if (b & (1u << i)) result ^= a << i;
    for (int bit = 14; bit >= 8; --bit) if (result & (1u << bit)) result ^= 0x11du << (bit - 8);
    return static_cast<std::uint8_t>(result);
}
static void parity(std::vector<std::uint8_t>& expected, unsigned count, unsigned length, bool changed) {
    std::vector<std::uint8_t> p(length), q(length);
    for (unsigned source = 0; source < count; ++source) {
        unsigned slot = (source * 5 + 3) % 16;
        std::uint8_t coefficient = 1;
        for (unsigned j = 0; j < slot; ++j) coefficient = mul(coefficient, 2);
        for (unsigned i = 0; i < length; ++i) {
            unsigned value = (slot * 37 + i * 13 + (i / 7) * 19 + 17) % 256;
            if (changed && source == count / 2) value ^= 0xa5;
            p[i] ^= static_cast<std::uint8_t>(value);
            q[i] ^= mul(value, coefficient);
        }
    }
    expected.insert(expected.end(), p.begin(), p.end());
    expected.insert(expected.end(), q.begin(), q.end());
}
int main() {
    __builtin_cpu_init();
    const bool gfni = __builtin_cpu_supports("gfni");
    const bool avx2 = __builtin_cpu_supports("avx2");
    std::printf("CPU GFNI=%d AVX2=%d\n", gfni, avx2);
    if (!gfni || !avx2) return 10;
    if (!SetProcessAffinityMask(GetCurrentProcess(), DWORD_PTR(1) << 2)) return 11;
    for (auto function : {simd_orakel_32_auto, simd_orakel_32_gfni128}) {
        for (unsigned a = 0; a < 256; ++a) {
            for (unsigned first = 0; first < 256; first += 32) {
                std::uint8_t left[32], right[32], out[64];
                for (unsigned lane = 0; lane < 32; ++lane) {
                    left[lane] = static_cast<std::uint8_t>(a);
                    right[lane] = static_cast<std::uint8_t>(first + lane);
                }
                function(left, right, out);
                for (unsigned lane = 0; lane < 32; ++lane)
                    if (out[lane] != mul(a, first + lane)) return 15;
            }
        }
    }
    std::printf("PASS GFNI128/256: je65536 Bytepaare gegen Polynomdivision\n");
    if (array_q_probe_alle() != 0) return 12;
    std::vector<std::uint8_t> expected;
    for (unsigned mode = 0; mode < 3; ++mode)
        for (unsigned count : {4u, 14u})
            for (unsigned length : {0u, 1u, 15u, 16u, 31u, 32u, 33u, 97u, 4096u}) {
                parity(expected, count, length, false);
                parity(expected, count, length, true);
            }
    if (captured != expected) return 13;
    std::printf("PASS Q Windows:54 Seiten/4239 Ausfaelle, GFNI/scalar/table/Orakel bytegleich, Bytes=%zu\n", captured.size());
    std::uint64_t checksum = 0;
    for (std::uint32_t mode = 0; mode < 3; ++mode) {
        std::vector<std::uint64_t> samples;
        for (unsigned i = 0; i < 3; ++i) {
            samples.push_back(array_q_bench(mode));
            auto actual = array_q_bench_pruefsumme();
            if (!checksum) checksum = actual;
            if (!actual || actual != checksum) return 14;
        }
        std::sort(samples.begin(), samples.end());
        double mib = (65521.0 * 4096 / 1048576) / (samples[1] / 1e9);
        std::printf("BENCH CPU2 mode=%u ns=%llu MiB_s=%.3f checksum=%llu\n",
            mode, static_cast<unsigned long long>(samples[1]), mib, static_cast<unsigned long long>(checksum));
    }
    return 0;
}

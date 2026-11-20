#include <cstdint>
#include <limits>
// Unabhaengige C++-Gegenstelle: zeigt, ob Adapter C wirklich betreten.
// Kein C++-Fehler darf die C-ABI passieren.
static std::uint64_t aufrufe;
extern "C" std::int64_t defer_orakel_wert(std::int64_t x) noexcept {
    ++aufrufe;
    return x == -1 ? INT64_C(9007199254740993) : x;
}
extern "C" std::uint64_t defer_orakel_anzahl() noexcept { return aufrufe; }
static_assert(sizeof(std::int64_t) == 8);
static_assert(std::numeric_limits<double>::digits == 53);

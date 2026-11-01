#include <cstdint>
#include <cstdio>
#include <cstring>
struct MooValue { std::uint64_t tag; std::uint64_t data; };
extern "C" void __real_moo_print(MooValue);
extern "C" MooValue moo_string_new(const char *);
extern "C" MooValue __real_moo_string_new(const char *);
extern "C" MooValue __wrap_moo_string_new(const char *text) {
    if (std::strcmp(text, "FALSCH_INNEN") == 0) std::fprintf(stderr, "FORBIDDEN_PRODUCER\n");
    return __real_moo_string_new(text);
}
extern "C" void moo_throw(MooValue);
extern "C" void __wrap_moo_print(MooValue value) {
    static bool injected = false;
    if (!injected) {
        injected = true;
        std::fprintf(stderr, "PRINT_INJECTED\n");
        MooValue error = moo_string_new("PRINT_FEHLER");
        moo_throw(error);  // Konsumiert den +1-Fehlerwert.
        return;
    }
    __real_moo_print(value);
}

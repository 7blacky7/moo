#include "m1_cpp_adapter.h"
#include <stdexcept>
#include <vector>
#include <limits>
#include <atomic>
#include <cstring>
#include <string>

namespace {
class Number {
public:
    explicit Number(int32_t value) : value_(value) {
        if (value < 0) throw std::invalid_argument("negative value");
    }
    int32_t value() const { return value_; }
private:
    int32_t value_;
};
}

extern "C" void *p2_cpp_create(int32_t value) {
    try { return new Number(value); }
    catch (...) { return nullptr; }
}
extern "C" int32_t p2_cpp_value(void *handle) {
    if (!handle) return -1;
    try { return static_cast<Number *>(handle)->value(); }
    catch (...) { return -1; }
}
extern "C" int32_t p2_cpp_destroy(void *handle) {
    if (!handle) return -1;
    try { delete static_cast<Number *>(handle); return 0; }
    catch (...) { return -1; }
}
extern "C" int32_t p2_cpp_example(int32_t value) {
    void *handle = p2_cpp_create(value);
    if (!handle) return -1;
    int32_t result = p2_cpp_value(handle);
    return p2_cpp_destroy(handle) == 0 ? result : -1;
}
extern "C" int64_t p2_cpp_vector_sum(const int32_t *values, uint32_t length) {
    try {
        if (length > 4096 || (!values && length != 0))
            throw std::invalid_argument("invalid vector input");
        std::vector<int32_t> copy;
        if (length != 0) copy.assign(values, values + length);
        int64_t sum = 0;
        for (int32_t value : copy) sum += value;
        return sum;
    } catch (...) { return std::numeric_limits<int64_t>::min(); }
}

/* P2-T23/T53 (Plan 36aca1f2 C1): opake Klasse P2Zaehler. */
struct P2Zaehler {
    std::vector<int32_t> werte;
    std::string name;
};

namespace {
std::atomic<int64_t> zaehler_lebend{0};
thread_local std::string letzter_fehler;

void merke(const char *text) {
    try { letzter_fehler = text; } catch (...) {}
}

uint64_t kopiere(const std::string &quelle, uint8_t *ziel, uint64_t kapazitaet) {
    uint64_t n = quelle.size() < kapazitaet ? quelle.size() : kapazitaet;
    if (ziel && n != 0) std::memcpy(ziel, quelle.data(), static_cast<size_t>(n));
    return quelle.size();
}
}

extern "C" P2Zaehler *p2_zaehler_neu(void) {
    try {
        P2Zaehler *z = new P2Zaehler();
        ++zaehler_lebend;
        return z;
    } catch (const std::exception &e) { merke(e.what()); return nullptr; }
    catch (...) { merke("unbekannte C++-Ausnahme"); return nullptr; }
}

extern "C" int32_t p2_zaehler_frei(P2Zaehler *zaehler) {
    if (!zaehler) { merke("kein Zaehler"); return -1; }
    delete zaehler;
    --zaehler_lebend;
    return 0;
}

extern "C" int32_t p2_zaehler_addiere(P2Zaehler *zaehler, const int32_t *werte, uint64_t anzahl) {
    try {
        if (!zaehler) throw std::invalid_argument("kein Zaehler");
        if (!werte && anzahl != 0) throw std::invalid_argument("keine Werte");
        if (anzahl > 4096 || zaehler->werte.size() + anzahl > 4096)
            throw std::length_error("zu viele Werte (hoechstens 4096)");
        zaehler->werte.insert(zaehler->werte.end(), werte, werte + anzahl);
        return 0;
    } catch (const std::exception &e) { merke(e.what()); return -1; }
    catch (...) { merke("unbekannte C++-Ausnahme"); return -1; }
}

extern "C" int64_t p2_zaehler_summe(const P2Zaehler *zaehler) {
    if (!zaehler) { merke("kein Zaehler"); return 0; }
    int64_t summe = 0;
    for (int32_t wert : zaehler->werte) summe += wert;
    return summe;
}

extern "C" int32_t p2_zaehler_name_setzen(P2Zaehler *zaehler, const uint8_t *text, uint64_t laenge) {
    try {
        if (!zaehler) throw std::invalid_argument("kein Zaehler");
        if (!text && laenge != 0) throw std::invalid_argument("kein Text");
        zaehler->name.assign(reinterpret_cast<const char *>(text), static_cast<size_t>(laenge));
        return 0;
    } catch (const std::exception &e) { merke(e.what()); return -1; }
    catch (...) { merke("unbekannte C++-Ausnahme"); return -1; }
}

extern "C" uint64_t p2_zaehler_name(const P2Zaehler *zaehler, uint8_t *ziel, uint64_t kapazitaet) {
    if (!zaehler) { merke("kein Zaehler"); return 0; }
    return kopiere(zaehler->name, ziel, kapazitaet);
}

extern "C" uint64_t p2_letzter_fehler(uint8_t *ziel, uint64_t kapazitaet) {
    return kopiere(letzter_fehler, ziel, kapazitaet);
}

extern "C" int64_t p2_zaehler_lebende(void) {
    return zaehler_lebend.load();
}

// P2-T49 C5: Strukturwert by value (kein Besitz, keine Exception moeglich).
extern "C" P2Punkt p2_punkt_neu(float x, float y, int32_t gewicht) {
    return P2Punkt{x, y, gewicht};
}

extern "C" P2Punkt p2_punkt_skaliert(P2Punkt punkt, float faktor) {
    return P2Punkt{punkt.x * faktor, punkt.y * faktor, punkt.gewicht + 1};
}

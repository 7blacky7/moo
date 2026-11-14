#include "cpp_adapter.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

class MooP2Values {
public:
    MooP2Values(const int32_t *data, size_t len) {
        if (len != 0) values_.assign(data, data + len);
    }
    int64_t sum() const {
        int64_t total = 0;
        for (int32_t value : values_) {
            if (value == -1) throw std::runtime_error("test exception");
            total += value;
        }
        return total;
    }
private:
    std::vector<int32_t> values_;
};

struct MooP2CppHandle { MooP2Values values; };

extern "C" int32_t moo_p2_cpp_create(const int32_t *data, size_t len, MooP2CppHandle **out) {
    if (!out) return MOO_P2_CPP_INVALID;
    *out = nullptr;
    // This small adapter limits count so sum of int32_t values fits int64_t.
    if ((!data && len != 0) || len > 1024) return MOO_P2_CPP_INVALID;
    try {
        *out = new MooP2CppHandle{MooP2Values(data, len)};
        return MOO_P2_CPP_OK;
    } catch (...) {
        return MOO_P2_CPP_EXCEPTION;
    }
}

extern "C" int32_t moo_p2_cpp_sum(const MooP2CppHandle *handle, int64_t *out) {
    if (!handle || !out) return MOO_P2_CPP_INVALID;
    try {
        const int64_t result = handle->values.sum();
        *out = result;
        return MOO_P2_CPP_OK;
    } catch (...) {
        return MOO_P2_CPP_EXCEPTION;
    }
}

extern "C" int32_t moo_p2_cpp_destroy(MooP2CppHandle **handle) {
    if (!handle || !*handle) return MOO_P2_CPP_INVALID;
    try {
        delete *handle;
        *handle = nullptr;
        return MOO_P2_CPP_OK;
    } catch (...) {
        return MOO_P2_CPP_EXCEPTION;
    }
}

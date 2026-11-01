#include "m1_cpp_adapter.h"
#include <stdexcept>
#include <vector>
#include <limits>

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

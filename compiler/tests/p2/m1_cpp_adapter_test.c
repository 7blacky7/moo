#include "m1_cpp_adapter.h"
#include <assert.h>
#include <stdint.h>
int main(void) {
    void *handle = p2_cpp_create(42);
    assert(handle != 0);
    assert(p2_cpp_value(handle) == 42);
    assert(p2_cpp_destroy(handle) == 0);
    assert(p2_cpp_create(-1) == 0);
    assert(p2_cpp_value(0) == -1);
    assert(p2_cpp_destroy(0) == -1);
    assert(p2_cpp_example(42) == 42);
    assert(p2_cpp_example(-1) == -1);
    assert(p2_cpp_example(INT32_MAX) == INT32_MAX);
    const int32_t elements[] = { 20, -3, INT32_MAX };
    assert(p2_cpp_vector_sum(elements, 3) == (int64_t)INT32_MAX + 17);
    assert(p2_cpp_vector_sum(0, 0) == 0);
    assert(p2_cpp_vector_sum(0, 1) == INT64_MIN);
    assert(p2_cpp_vector_sum(elements, 4097) == INT64_MIN);
    return 0;
}

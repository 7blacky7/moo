#ifndef P2_M1_CPP_ADAPTER_H
#define P2_M1_CPP_ADAPTER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The handle is opaque. A null handle or a caught C++ exception returns -1. */
void *p2_cpp_create(int32_t value);
int32_t p2_cpp_value(void *handle);
int32_t p2_cpp_destroy(void *handle);
int32_t p2_cpp_example(int32_t value);
/* Sum copies up to 4096 elements; null is valid only for length zero. */
int64_t p2_cpp_vector_sum(const int32_t *values, uint32_t length);
#ifdef __cplusplus
}
#endif
#endif

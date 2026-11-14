#ifndef MOO_P2_CPP_ADAPTER_H
#define MOO_P2_CPP_ADAPTER_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MooP2CppHandle MooP2CppHandle;
enum MooP2CppStatus {
    MOO_P2_CPP_OK = 0,
    MOO_P2_CPP_INVALID = 1,
    MOO_P2_CPP_EXCEPTION = 2
};

/* Input is borrowed only for this call and copied; the returned handle is owned.
 * On error *out is NULL. Destroy on the same adapter/allocator side. */
int32_t moo_p2_cpp_create(const int32_t *data, size_t len, MooP2CppHandle **out);
/* handle is borrowed; result is written only on success. */
int32_t moo_p2_cpp_sum(const MooP2CppHandle *handle, int64_t *out);
/* Consumes *handle on success, sets it to NULL; NULL handle is invalid. */
int32_t moo_p2_cpp_destroy(MooP2CppHandle **handle);

#ifdef __cplusplus
}
#endif
#endif

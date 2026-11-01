#include "../../runtime/moo_buffer.h"
#include <assert.h>
#include <stdint.h>
#include <stddef.h>

void *__real_calloc(size_t, size_t);
static int fail_calloc;
void *__wrap_calloc(size_t count, size_t size) {
    if (fail_calloc) return NULL;
    return __real_calloc(count, size);
}
int main(void) {
    MooBuffer *buffer = NULL, *view = NULL;
    int32_t first = 17, second = 42, result = 0;
    assert(moo_buffer_new_scalar(MOO_BUFFER_I32, 1, &buffer) == MOO_BUFFER_OK);
    assert(moo_buffer_set_scalar(buffer, MOO_BUFFER_I32, 0, &first) == MOO_BUFFER_OK);
    fail_calloc = 1;
    assert(moo_buffer_append_scalar(buffer, MOO_BUFFER_I32, &second) == MOO_BUFFER_ALLOC);
    fail_calloc = 0;
    assert(moo_buffer_len(buffer) == 1);
    assert(moo_buffer_get_scalar(buffer, MOO_BUFFER_I32, 0, &result) == MOO_BUFFER_OK && result == 17);
    assert(moo_buffer_append_scalar(buffer, MOO_BUFFER_I32, &second) == MOO_BUFFER_OK);
    assert(moo_buffer_get_scalar(buffer, MOO_BUFFER_I32, 1, &result) == MOO_BUFFER_OK && result == 42);
    assert(moo_buffer_append_scalar(buffer, MOO_BUFFER_I32, &first) == MOO_BUFFER_OK);
    fail_calloc = 1;
    /* Fourth element fits in existing capacity even with allocation disabled. */
    assert(moo_buffer_append_scalar(buffer, MOO_BUFFER_I32, &second) == MOO_BUFFER_OK);
    assert(moo_buffer_append_scalar(buffer, MOO_BUFFER_I32, &first) == MOO_BUFFER_ALLOC);
    fail_calloc = 0;
    assert(moo_buffer_len(buffer) == 4);
    assert(moo_buffer_get_scalar(buffer, MOO_BUFFER_I32, 3, &result) == MOO_BUFFER_OK && result == 42);
    assert(moo_buffer_view(buffer, 0, 1, &view) == MOO_BUFFER_OK);
    assert(moo_buffer_append_scalar(buffer, MOO_BUFFER_I32, &second) == MOO_BUFFER_PINNED);
    assert(moo_buffer_append_scalar(view, MOO_BUFFER_I32, &second) == MOO_BUFFER_INVALID);
    moo_buffer_drop(view);
    moo_buffer_drop(buffer);
    return 0;
}

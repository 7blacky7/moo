#include "../../runtime/moo_runtime.h"
#include "../../runtime/moo_defer_error.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static int fail_malloc, fail_strdup, malloc_calls, strdup_calls;
void *__real_malloc(size_t);
char *__real_strdup(const char *);
void *__wrap_malloc(size_t n) {
    ++malloc_calls;
    return fail_malloc ? NULL : __real_malloc(n);
}
char *__wrap_strdup(const char *s) {
    ++strdup_calls;
    return fail_strdup ? NULL : __real_strdup(s);
}
#define UNUSED_FREE(name) void name(void *ptr) { (void)ptr; abort(); }
UNUSED_FREE(moo_socket_free)
UNUSED_FREE(moo_thread_free)
UNUSED_FREE(moo_channel_free)
UNUSED_FREE(moo_db_free)
UNUSED_FREE(moo_db_stmt_free)
UNUSED_FREE(moo_web_free)
UNUSED_FREE(moo_frame_free)
UNUSED_FREE(moo_gif_handle_free)
UNUSED_FREE(moo_video_handle_free)
UNUSED_FREE(moo_tensor_free)
UNUSED_FREE(moo_kamera_free)
UNUSED_FREE(moo_mikro_free)
UNUSED_FREE(moo_buffer_free)
UNUSED_FREE(moo_struct_box_free)
void moo_print(MooValue value) { (void)value; abort(); }

int main(void) {
    assert(moo_defer_oom_value(NULL) == MOO_DEFER_INVALID);
    assert(moo_defer_status_value(MOO_DEFER_ALLOC, NULL) == MOO_DEFER_INVALID);
    MooValue untouched = {MOO_BOOL, 1};
    assert(moo_defer_status_value(MOO_DEFER_OK, &untouched) == MOO_DEFER_INVALID);
    assert(untouched.tag == MOO_BOOL && untouched.data == 1);
    assert(moo_defer_status_value(MOO_DEFER_EMPTY, &untouched) == MOO_DEFER_INVALID);
    assert(untouched.tag == MOO_BOOL && untouched.data == 1);
    fail_malloc = 1;
    assert(moo_error("other").tag == MOO_NONE);
    fail_malloc = 0;
    fail_strdup = 1;
    assert(moo_error("other").tag == MOO_NONE);

    MooValue candidate = {MOO_NONE, 0};
    fail_malloc = 1;
    int old_malloc = malloc_calls, old_strdup = strdup_calls;
    assert(moo_defer_oom_value(&candidate) == MOO_DEFER_OK && candidate.tag == MOO_ERROR);
    moo_try_enter();
    moo_throw(candidate);
    assert(moo_try_check());
    MooValue retained = moo_get_error();
    MooValue transferred = moo_try_take_error();
    assert(retained.tag == MOO_ERROR && transferred.data == candidate.data);
    assert(strcmp(MV_ERR(retained), "Defer: Speicher voll") == 0);
    moo_try_leave();
    moo_release(retained);
    moo_release(transferred);
    assert(MV_ERROR(candidate)->refcount == 0);
    assert(malloc_calls == old_malloc && strdup_calls == old_strdup);
    /* Constructing a sentinel must not replace an earlier `wirf nichts`. */
    moo_try_enter();
    moo_throw(moo_none());
    MooValue later_error = {MOO_NONE, 0};
    assert(moo_defer_status_value(MOO_DEFER_OVERFLOW, &later_error) == MOO_DEFER_OK);
    assert(moo_try_check());
    MooValue earlier = moo_try_take_error();
    assert(earlier.tag == MOO_NONE && !moo_try_check());
    moo_try_leave();
    moo_release(earlier);
    moo_release(later_error);
    const int statuses[] = {MOO_DEFER_ALLOC, MOO_DEFER_INVALID, MOO_DEFER_OVERFLOW,
                            MOO_DEFER_UNKNOWN_ACTION, 12345};
    const char *messages[] = {"Defer: Speicher voll", "Defer: ungueltiger Zustand",
                              "Defer: Kapazitaet uebergelaufen", "Defer: unbekannte Aktion",
                              "Defer: unbekannter Status"};
    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        MooValue status_error = {MOO_NONE, 0};
        assert(moo_defer_status_value(statuses[i], &status_error) == MOO_DEFER_OK);
        assert(status_error.tag == MOO_ERROR && strcmp(MV_ERR(status_error), messages[i]) == 0);
        moo_try_enter();
        moo_throw(status_error);
        assert(moo_try_check());
        MooValue taken = moo_try_take_error();
        moo_try_leave();
        moo_retain(taken);
        moo_release(taken);
        moo_release(taken);
        assert(MV_ERROR(status_error)->refcount == 0);
    }
    assert(malloc_calls == old_malloc && strdup_calls == old_strdup);
    fail_malloc = fail_strdup = 0;
    return 0;
}

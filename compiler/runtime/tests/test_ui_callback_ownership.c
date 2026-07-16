#include "../moo_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef MooValue (*MooTestFn0)(void);

static unsigned checks;
static unsigned failures;
static unsigned func_frees;
static unsigned return_frees;
static int alive_during_callback;
static MooValue callback_slot;
static MooValue timer_slot;
static unsigned timer_calls;
static unsigned replacement_calls;
static unsigned timer_alive_during_callback;
static unsigned timer_destroy_calls;
static unsigned after_destroy_noops;

#define CHECK(cond, label) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL: %s\n", (label)); \
    } \
} while (0)

/* Current borrowed dispatch: deliberately weak so B1 runs and ASan
 * demonstrates the reentrant UAF. B2 must provide a strong internal helper
 * with this non-test-specific name and use it at real dispatch sites. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
MooValue moo_ui_callback_call0_owned(MooValue callback) {
    return moo_func_call_0(callback);
}

MooValue moo_none(void) {
    MooValue value = { MOO_NONE, 0u };
    return value;
}

static MooValue test_heap_new(void) {
    struct MooString* heap = (struct MooString*)calloc(1u, sizeof(*heap));
    MooValue value = { MOO_STRING, 0u };
    if (!heap) {
        fprintf(stderr, "allocation failed\n");
        exit(2);
    }
    heap->refcount = 1;
    moo_val_set_ptr(&value, heap);
    return value;
}

static MooValue test_func_new(MooTestFn0 callback) {
    MooFunc* func = (MooFunc*)calloc(1u, sizeof(*func));
    MooValue value = { MOO_FUNC, 0u };
    if (!func) {
        fprintf(stderr, "allocation failed\n");
        exit(2);
    }
    _Static_assert(sizeof(callback) == sizeof(func->fn_ptr),
                   "function pointer storage mismatch");
    func->refcount = 1;
    memcpy(&func->fn_ptr, &callback, sizeof(callback));
    func->arity = 0;
    moo_val_set_ptr(&value, func);
    return value;
}

void moo_retain(MooValue value) {
    if (value.tag == MOO_FUNC) {
        MV_FUNC(value)->refcount++;
    } else if (value.tag == MOO_STRING) {
        MV_STR(value)->refcount++;
    }
}

void moo_release(MooValue value) {
    if (value.tag == MOO_FUNC) {
        MooFunc* func = MV_FUNC(value);
        func->refcount--;
        if (func->refcount == 0) {
            func_frees++;
            free(func);
        }
    } else if (value.tag == MOO_STRING) {
        struct MooString* heap = MV_STR(value);
        heap->refcount--;
        if (heap->refcount == 0) {
            return_frees++;
            free(heap);
        }
    }
}

MooValue moo_func_call_0(MooValue callback) {
    MooTestFn0 fn = NULL;
    MooFunc* func = MV_FUNC(callback);
    memcpy(&fn, &func->fn_ptr, sizeof(fn));
    return fn();
}

static MooValue remove_during_dispatch(void) {
    MooValue in_flight = callback_slot;
    moo_release(callback_slot);
    callback_slot = moo_none();

    /* The backend must hold an in-flight +1 until the callback returns. */
    alive_during_callback = MV_FUNC(in_flight)->refcount == 1;
    return test_heap_new();
}

static MooValue replacement_timer_callback(void) {
    replacement_calls++;
    return test_heap_new();
}

static void timer_destroy(void) {
    if (timer_slot.tag == MOO_FUNC) {
        moo_release(timer_slot);
    }
    timer_slot = moo_none();
    timer_destroy_calls++;
}

static void timer_tick_if_bound(void) {
    MooValue result;
    if (timer_slot.tag != MOO_FUNC) return;
    result = moo_ui_callback_call0_owned(timer_slot);
    moo_release(result);
}

static MooValue remove_reenter_destroy_timer(void) {
    MooValue in_flight = timer_slot;
    timer_calls++;

    /* Model self-removal of the owning timer slot. The helper must keep this
     * callback alive while a replacement is installed and dispatched once. */
    timer_destroy();
    if (MV_FUNC(in_flight)->refcount == 1) {
        timer_alive_during_callback++;
    }

    timer_slot = test_func_new(replacement_timer_callback);
    timer_tick_if_bound();
    timer_destroy();
    return test_heap_new();
}

int main(void) {
    MooValue result;

    callback_slot = test_func_new(remove_during_dispatch);
    result = moo_ui_callback_call0_owned(callback_slot);
    CHECK(callback_slot.tag == MOO_NONE, "callback removed owning slot");
    CHECK(alive_during_callback, "callback alive until return");
    CHECK(func_frees == 1u, "callback released exactly once after return");
    CHECK(return_frees == 0u, "owning return not pre-released");
    moo_release(result);
    CHECK(return_frees == 1u, "owning return released exactly once");

    timer_slot = moo_none();
    for (unsigned i = 0u; i < 1000u; i++) {
        unsigned calls_before_destroyed_tick;
        unsigned replacements_before_destroyed_tick;

        timer_slot = test_func_new(remove_reenter_destroy_timer);
        timer_tick_if_bound();
        calls_before_destroyed_tick = timer_calls;
        replacements_before_destroyed_tick = replacement_calls;

        /* Destroyed timers are inert: a later host tick must dispatch nothing. */
        timer_tick_if_bound();
        if (timer_calls == calls_before_destroyed_tick &&
            replacement_calls == replacements_before_destroyed_tick) {
            after_destroy_noops++;
        }
    }

    CHECK(timer_slot.tag == MOO_NONE, "timer slot empty after destroy");
    CHECK(timer_calls == 1000u, "timer callback exactly once per cycle");
    CHECK(replacement_calls == 1000u,
          "replacement dispatched exactly once per reentrant cycle");
    CHECK(timer_alive_during_callback == 1000u,
          "self-removed timer callback alive until return");
    CHECK(timer_destroy_calls == 2000u,
          "old and replacement timer owners destroyed exactly once");
    CHECK(after_destroy_noops == 1000u,
          "no callback after timer destroy");
    CHECK(func_frees == 2001u,
          "exact function frees across 1000 timer cycles");
    CHECK(return_frees == 2001u,
          "exact owning-return frees across 1000 timer cycles");

    if (failures != 0u) {
        fprintf(stderr,
                "P016-M1 CALLBACK OWNERSHIP RED: checks=%u failures=%u\n",
                checks, failures);
        return 1;
    }
    printf("P016-M1 CALLBACK OWNERSHIP GREEN: checks=%u\n", checks);
    return 0;
}

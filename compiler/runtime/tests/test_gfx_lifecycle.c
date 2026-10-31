/* Registry tests deliberately use opaque, non-dereferenced window addresses. */
#include <assert.h>
#include <stdio.h>
#include "../moo_3d.c"
static int errors, closed;
MooValue moo_string_new(const char* s) { (void)s; return (MooValue){0}; }
void moo_throw(MooValue v) { (void)v; ++errors; }
static MooValue handle(int tag, void* ptr) {
    MooValue v = {0}; v.tag = tag; moo_val_set_ptr(&v, ptr); return v;
}
static void close_context(void* ctx) {
    assert(ctx == &closed);
    assert(!g_ctx && !g_backend);
    ++closed;
}
int main(void) {
    int a, b, context_a, context_b;
    MooValue wa = handle(MOO_WINDOW_HYBRID, &a);
    MooValue wb = handle(MOO_WINDOW_HYBRID, &b);
    Moo3DBackend backend = {0}; backend.close = close_context;
    assert(!moo_3d_active_window(wa));
    moo_3d_attach_external(&backend, &context_a);
    assert(!moo_3d_active_window(wa));
    moo_3d_register_window(wa, &context_b);
    assert(!moo_3d_active_window(wa));
    moo_3d_register_window(wa, &context_a);
    assert(moo_3d_active_window(wa));
    assert(!moo_3d_active_window(wb));
    assert(!moo_3d_active_window(handle(MOO_WINDOW3D, &a)));
    assert(!moo_3d_active_window(handle(MOO_NONE, &a)));
    assert(!moo_3d_active_window(handle(MOO_WINDOW_HYBRID, NULL)));
    moo_3d_attach_external(&backend, &context_b);
    assert(!moo_3d_active_window(wa));
    moo_3d_register_window(wb, &context_b);
    moo_3d_detach_external(&context_a);
    moo_3d_detach_external(NULL);
    assert(moo_3d_active_window(wb));
    moo_3d_close(handle(MOO_WINDOW3D, &a));
    moo_3d_close(wb);
    assert(closed == 0 && moo_3d_active_window(wb));
    moo_3d_detach_external(&context_b);
    assert(!moo_3d_active_window(wb));
    moo_3d_render_time_live(wb);
    assert(errors == 1);
    moo_3d_attach_external(&backend, &closed);
    MooValue pure = handle(MOO_WINDOW3D, &closed);
    moo_3d_register_window(pure, &closed);
    assert(moo_3d_active_window(pure));
    moo_3d_close(handle(MOO_WINDOW3D, &a));
    moo_3d_close(handle(MOO_WINDOW_HYBRID, &closed));
    assert(closed == 0 && moo_3d_active_window(pure));
    moo_3d_close(pure);
    assert(closed == 1 && !moo_3d_active_window(pure));
    moo_3d_close(pure);
    assert(closed == 1);
    puts("GFX-LIFECYCLE registry PASS");
    return 0;
}

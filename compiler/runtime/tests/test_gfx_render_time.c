/* GFX-TIME numeric/dispatch gate; no GL context. Compile with function/data
 * sections and --gc-sections so unrelated dispatcher paths need no backend. */
#include <assert.h>
#include <stdio.h>
#include "../moo_3d.c"

static int errors, calls, fixed_mode;
static float observed;
MooValue moo_string_new(const char* text) { (void)text; return (MooValue){0}; }
void moo_throw(MooValue error) { (void)error; ++errors; }
static MooValue number(double value) {
    MooValue v = {0}; v.tag = MOO_NUMBER; moo_val_set_double(&v, value); return v;
}
static void record_time(void* ctx, int fixed, float seconds) {
    assert(ctx == &calls);
    ++calls; fixed_mode = fixed; observed = seconds;
}
int main(void) {
    MooValue win = {0};
    Moo3DBackend supported = {0}, unsupported = {0};
    supported.set_render_time = record_time;
    moo_3d_attach_external(&supported, &calls);
    const double valid[] = {0.0, 1.25, 2.5, (double)FLT_MAX};
    for (size_t i = 0; i < sizeof(valid)/sizeof(valid[0]); ++i) {
        int before = calls;
        moo_3d_render_time(win, number(valid[i]));
        assert(calls == before + 1 && errors == 0);
        assert(fixed_mode == 1 && observed == (float)valid[i]);
    }
    const double invalid[] = {-1.0, NAN, INFINITY, -INFINITY, (double)FLT_MAX * 2.0};
    for (size_t i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        int before = calls, old_errors = errors;
        moo_3d_render_time(win, number(invalid[i]));
        assert(errors == old_errors + 1 && calls == before);
    }
    int before = calls, old_errors = errors;
    moo_3d_render_time(win, (MooValue){.tag = MOO_NONE});
    assert(errors == old_errors + 1 && calls == before);
    moo_3d_render_time_live(win);
    assert(calls == before + 1 && fixed_mode == 0);
    moo_3d_attach_external(&unsupported, &calls);
    before = calls; old_errors = errors;
    moo_3d_render_time(win, number(1.25));
    moo_3d_render_time_live(win);
    assert(errors == old_errors + 2 && calls == before);
    moo_3d_attach_external(NULL, NULL);
    old_errors = errors;
    moo_3d_render_time(win, number(0.0));
    moo_3d_render_time_live(win);
    assert(errors == old_errors + 2 && calls == before);
    puts("GFX-TIME numeric/dispatch PASS");
    return 0;
}

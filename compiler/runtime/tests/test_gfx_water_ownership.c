/* CPU ownership gate: REAL dict/list/string/refcount code, no GL/window creation.
 * Only string conversion, unrelated resource destructors and backend are stubs. */
#include <assert.h>
#include <stdio.h>
#include "../moo_3d.c"
static int errors, backend_ok = 1, calls;
MOO_TLS int moo_error_flag = 0;
MooValue moo_error(const char* msg) { fputs(msg,stderr); abort(); }
MooValue moo_none(void) { return (MooValue){.tag=MOO_NONE}; }
MooValue moo_number(double x) { MooValue v={.tag=MOO_NUMBER}; memcpy(&v.data,&x,sizeof(x)); return v; }
MooValue moo_bool(bool x) { return (MooValue){.tag=MOO_BOOL,.data=x}; }
MooValue moo_to_string(MooValue x) { assert(x.tag==MOO_STRING); return x; }
void moo_throw(MooValue x) { ++errors; moo_release(x); }
#define UNUSED_FREE(name) void name(void* p) { (void)p; abort(); }
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
static int material(void* ctx, const MooWaterSettings* s) {
    assert(ctx==&calls && s);
    ++calls;
    assert(s->ior>1.33f && s->ior<1.34f);
    return backend_ok;
}
static void set(MooValue d, const char* key, MooValue v) {
    moo_dict_set(d,moo_string_new(key),v);
}
static MooValue parameters(void) {
    MooValue d=moo_dict_new(), a=moo_list_new(3);
    moo_list_append(a,moo_number(.055));
    moo_list_append(a,moo_number(.022));
    moo_list_append(a,moo_number(.012));
    set(d,"ior",moo_number(1.333));
    set(d,"absorption",a);
    set(d,"tint",moo_string_new("#195D69"));
    set(d,"sky_horizon",moo_number(0x8099AA));
    set(d,"sky_zenith",moo_string_new("#336699"));
    set(d,"max_distance",moo_number(80));
    return d;
}
int main(void) {
    Moo3DBackend backend={0};
    backend.set_water=material;
    MooValue win={.tag=MOO_WINDOW3D};
    moo_val_set_ptr(&win,&backend);
    moo_3d_attach_external(&backend,&calls);
    moo_3d_register_window(win,&calls);
#ifdef WATER_REPRO_OLD_KEY_RELEASE
    MooValue d=parameters(), key=moo_string_new("ior");
    MooValue v=moo_dict_get(d,key);
    moo_release(key); /* OLD bug: lookup has consumed key; ASan MUST fail. */
    moo_release(v); moo_release(d);
#else
    for (int i=0;i<10000;++i) {
        MooValue d=parameters();
        int before=errors;
        backend_ok=1;
        moo_3d_water_material(win,d);
        assert(errors==before);
        /* A second call with the SAME dictionary tests retained field lifetime. */
        moo_3d_water_material(win,d);
        assert(errors==before);
        backend_ok=0;
        moo_3d_water_material(win,d);
        assert(errors==before+1);
        set(d,"tint",moo_string_new("#12GG56"));
        moo_3d_water_material(win,d);
        assert(errors==before+2);
        moo_release(d);
        d=moo_dict_new();
        moo_3d_water_material(win,d);
        assert(errors==before+3);
        moo_release(d);
    }
    moo_3d_detach_external(&calls);
    assert(calls==30000 && errors==30000);
    puts("GFX-WATER real-container ownership 50000 calls PASS");
#endif
    return 0;
}

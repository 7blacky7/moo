/* Owning test containers isolate wrapper/dispatch; Moo fixture covers real containers. */
#include <assert.h>
#include <stdio.h>
#include "../moo_3d.c"
#include "../moo_test_api.c"
typedef struct { size_t n; MooValue keys[6], values[6]; } InfoTestDict;
static int errors, calls;
static char renderer_text[] = "owned-copy";
static MooValue ptr(uint64_t tag, void* p) {
    MooValue v = {0}; v.tag = tag; moo_val_set_ptr(&v, p); return v;
}
MooValue moo_bool(bool b) { MooValue v = {0}; v.tag=MOO_BOOL; v.data=b; return v; }
MooValue moo_none(void) { return (MooValue){.tag=MOO_NONE}; }
MooValue moo_string_new(const char* text) {
    MooString* p=malloc(sizeof(*p)); assert(p);
    p->refcount=1; p->length=(int32_t)strlen(text); p->capacity=p->length+1;
    p->chars=malloc((size_t)p->capacity); assert(p->chars); strcpy(p->chars,text);
    return ptr(MOO_STRING,p);
}
MooValue moo_dict_new(void) {
    void* p=calloc(1,sizeof(InfoTestDict)); assert(p); return ptr(MOO_DICT,p);
}
void moo_dict_set(MooValue dict, MooValue key, MooValue value) {
    InfoTestDict* d=moo_val_as_ptr(dict); assert(d->n<6);
    d->keys[d->n]=key; d->values[d->n++]=value;
}
static void destroy(MooValue v) {
    if(v.tag==MOO_STRING){ MooString* p=MV_STR(v); free(p->chars); free(p); }
    if(v.tag==MOO_DICT){ InfoTestDict* d=moo_val_as_ptr(v);
        for(size_t i=0;i<d->n;++i){destroy(d->keys[i]);destroy(d->values[i]);} free(d); }
}
void moo_throw(MooValue v){ ++errors; destroy(v); }
static MooValue get(MooValue v,const char* key) {
    InfoTestDict* d=moo_val_as_ptr(v);
    for(size_t i=0;i<d->n;++i) if(!strcmp(MV_STR(d->keys[i])->chars,key))return d->values[i];
    assert(0); return moo_none();
}
static int info(void* ctx,MooRendererInfo* out) {
    assert(ctx==&calls); ++calls;
    *out=(MooRendererInfo){"test","TestAPI","vendor",renderer_text,"version"};
    return 1;
}
int main(void) {
    Moo3DBackend backend={0}; int opaque;
    MooValue win=ptr(MOO_WINDOW3D,&opaque);
    assert(moo_test_renderer_info(win).tag==MOO_NONE && errors==1);
    moo_3d_attach_external(&backend,&calls);
    moo_3d_register_window(win,&calls);
    MooValue unknown=moo_test_renderer_info(win);
    assert(!MV_BOOL(get(unknown,"available")) && calls==0);
    const char* fields[]={"backend","api","vendor","renderer","version"};
    for(size_t i=0;i<5;++i)assert(!strcmp(MV_STR(get(unknown,fields[i]))->chars,"unknown"));
    destroy(unknown);
    backend.renderer_info=info;
    MooValue copied=moo_test_renderer_info(win);
    assert(MV_BOOL(get(copied,"available")) && calls==1);
    renderer_text[0]='X';
    assert(!strcmp(MV_STR(get(copied,"renderer"))->chars,"owned-copy"));
    for(int i=0;i<10000;++i)destroy(moo_test_renderer_info(win));
    moo_3d_detach_external(&calls);
    assert(moo_test_renderer_info(win).tag==MOO_NONE && errors==2);
    assert(moo_test_renderer_info(ptr(MOO_WINDOW_HYBRID,&opaque)).tag==MOO_NONE && errors==3);
    assert(moo_test_renderer_info(ptr(MOO_WINDOW,&opaque)).tag==MOO_NONE && errors==4);
    assert(!strcmp(MV_STR(get(copied,"renderer"))->chars,"owned-copy"));
    destroy(copied);
    puts("GFX-RENDERINFO wrapper/unsupported/copy/ownership PASS");
    return 0;
}

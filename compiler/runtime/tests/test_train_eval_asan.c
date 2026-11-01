#include "../moo_train_eval.h"
#include <stdio.h>
int moo_error_flag=0;
MooValue moo_last_error;
int moo_try_depth=0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue e) { moo_release(e); moo_error_flag=1; }
void moo_socket_free(void *p) { (void)p; }
void moo_thread_free(void *p) { (void)p; }
void moo_channel_free(void *p) { (void)p; }
void moo_db_free(void *p) { (void)p; }
void moo_db_stmt_free(void *p) { (void)p; }
void moo_window_free(void *p) { (void)p; }
void moo_web_free(void *p) { (void)p; }
void moo_voxel_free(void *p) { (void)p; }
void moo_frame_free(void *p) { (void)p; }
void moo_gif_handle_free(void *p) { (void)p; }
void moo_video_handle_free(void *p) { (void)p; }
static MooValue layers;
static void put(MooValue d,const char *k,MooValue v) { moo_dict_set(d,moo_string_new(k),v); }
static MooValue get(MooValue d,const char *k) { return moo_dict_get(d,moo_string_new(k)); }
/* These isolate scoring/ownership; real checkpoint and hash are covered by the Moo API test. */
MooValue moo_file_read(MooValue path) { moo_release(path); return moo_string_new("checkpoint"); }
MooValue moo_sha256(MooValue input) {
    (void)input;
    return moo_string_new("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
}
MooValue moo_nn_ckpt_laden(MooValue path,MooValue expected) {
    (void)path; (void)expected; return moo_dict_new();
}
static MooValue forward(MooValue x) {
    MooValue out=moo_nn_vorwaerts(layers,x); moo_release(x); return out;
}
static MooValue factory(MooValue state) {
    moo_release(state); return moo_func_new((void*)forward,1,"forward");
}
static MooValue encoder(MooValue text) {
    moo_release(text);
    MooValue l=moo_list_new(3);
    moo_list_append(l,moo_number(0)); moo_list_append(l,moo_number(1)); moo_list_append(l,moo_number(2));
    MooValue out=moo_tensor_aus_liste(l); moo_release(l); return out;
}
static MooValue decoder(MooValue ids) {
    bool first=ids.tag==MOO_LIST && MV_LIST(ids)->length==1;
    moo_release(ids); return moo_string_new(first ? "a" : "abc");
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d\\n",__LINE__); return 1; } } while (0)
int main(void) {
    layers=moo_list_new(2);
    moo_list_append(layers,moo_nn_schicht_embedding(moo_number(3),moo_number(3),moo_number(1)));
    MooValue act=moo_string_new("keine");
    moo_list_append(layers,moo_nn_schicht_dicht(moo_number(3),moo_number(3),act,moo_number(2)));
    moo_release(act);
    MooValue data=moo_dict_new(); put(data,"text",moo_string_new("abc")); put(data,"split",moo_string_new("val"));
    MooValue config=moo_dict_new();
    put(config,"tokenizer",moo_string_new("tokenizer"));
    put(config,"encoder",moo_func_new((void*)encoder,1,"encoder"));
    put(config,"decoder",moo_func_new((void*)decoder,1,"decoder"));
    put(config,"checkpoint_pfad",moo_string_new("mock"));
    put(config,"arch_version",moo_string_new("v1"));
    put(config,"sequenzlaenge",moo_number(4)); put(config,"vokab",moo_number(3)); put(config,"pad_id",moo_number(0));
    MooValue fn=moo_func_new((void*)factory,1,"factory");
    for (int i=0;i<3;i++) {
        if (i==1) moo_ag_aus(); else moo_ag_an();
        MooValue r=moo_train_eval_modell(fn,data,config);
        CHECK(!moo_error_flag && r.tag==MOO_DICT);
        CHECK(moo_ag_ist_an()==(i!=1));
        MooValue bytes=get(r,"bytes"), cps=get(r,"codepoints"), loss=get(r,"nll");
        CHECK(MV_NUM(bytes)==2 && MV_NUM(cps)==2 && MV_NUM(loss)>0);
        moo_release(bytes); moo_release(cps); moo_release(loss);
        MooValue list=moo_list_new(1); moo_retain(r); moo_list_append(list,r);
        MooValue best=moo_train_eval_waehlen(list);
        CHECK(!moo_error_flag && best.tag==MOO_DICT);
        moo_release(best); moo_release(list); moo_release(r);
    }
    put(data,"split",moo_string_new("training"));
    MooValue bad=moo_train_eval_modell(fn,data,config);
    CHECK(moo_error_flag && bad.tag==MOO_NONE); moo_error_flag=0; moo_release(bad);
    MooValue wrong=moo_func_new((void*)factory,0,"wrong");
    bad=moo_train_eval_modell(wrong,data,config);
    CHECK(moo_error_flag && bad.tag==MOO_NONE); moo_error_flag=0; moo_release(bad); moo_release(wrong);
    moo_release(fn); moo_release(data); moo_release(config); moo_release(layers); moo_ag_reset();
    puts("P5 EVAL ASAN OK"); return 0;
}

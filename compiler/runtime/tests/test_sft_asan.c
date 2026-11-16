#include "../moo_sft.h"
#include <stdio.h>
MOO_TLS int moo_error_flag = 0;
MOO_TLS MooValue moo_last_error;
MOO_TLS int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue e) { moo_release(e); moo_error_flag = 1; }
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
static MooValue forward(MooValue x) {
    MooValue out = moo_nn_vorwaerts(layers, x);
    moo_release(x); /* Moo-Callbacks konsumieren Argumentreferenzen. */
    return out;
}
static void put(MooValue d, const char *k, MooValue v) { moo_dict_set(d, moo_string_new(k), v); }
static MooValue get(MooValue d, const char *k) { return moo_dict_get(d, moo_string_new(k)); }
static MooValue nums(int a, int b, int c, int d) {
    MooValue l = moo_list_new(4);
    moo_list_append(l, moo_number(a)); moo_list_append(l, moo_number(b));
    moo_list_append(l, moo_number(c)); moo_list_append(l, moo_number(d));
    return l;
}
static MooValue encoder(MooValue text) {
    bool prompt = text.tag == MOO_STRING && !strcmp(MV_STR(text)->chars, "p");
    int32_t shape[1] = { prompt ? 2 : 6 };
    MooTensor *t = moo_tensor_raw(1, shape);
    if (!t) { moo_release(text); return moo_none(); }
    for (int i = 0; i < shape[0]; i++) t->data[i] = prompt ? (float)i : 2.0f;
    MooValue out; out.tag = MOO_TENSOR; moo_val_set_ptr(&out, t);
    moo_release(text);
    return out;
}
static MooValue window(int offset) {
    MooValue e = moo_dict_new();
    if (offset == 0) {
        put(e, "input_ids", nums(0,1,2,2)); put(e, "target_ids", nums(1,2,2,2));
        put(e, "loss_mask", nums(0,1,1,1)); put(e, "nutzlaenge", moo_number(4));
        put(e, "ziel_start", moo_number(1));
    } else if (offset == 2) {
        put(e, "input_ids", nums(2,2,2,2)); put(e, "target_ids", nums(2,2,2,2));
        put(e, "loss_mask", nums(0,0,1,1)); put(e, "nutzlaenge", moo_number(4));
        put(e, "ziel_start", moo_number(2));
    } else {
        put(e, "input_ids", nums(2,2,2,0)); put(e, "target_ids", nums(2,2,2,0));
        put(e, "loss_mask", nums(0,0,1,0)); put(e, "nutzlaenge", moo_number(3));
        put(e, "ziel_start", moo_number(2));
    }
    put(e, "fenster_offset", moo_number(offset)); put(e, "prompt", moo_string_new("p"));
    put(e, "akzeptiert", moo_bool(true)); put(e, "aufgabe", moo_string_new("a"));
    put(e, "original", moo_string_new("o")); put(e, "korrektur", moo_string_new("z"));
    put(e, "begruendung", moo_string_new("belegt"));
    return e;
}
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d\n", __LINE__); return 1; } } while (0)
int main(void) {
    layers = moo_list_new(2);
    moo_list_append(layers, moo_nn_schicht_embedding(moo_number(4), moo_number(4), moo_number(17)));
    MooValue act = moo_string_new("keine");
    moo_list_append(layers, moo_nn_schicht_dicht(moo_number(4), moo_number(4), act, moo_number(19)));
    moo_release(act);
    MooValue params = moo_nn_parameter(layers);
    MooValue e = moo_dict_new();
    put(e, "input_ids", nums(0,1,2,0)); put(e, "target_ids", nums(1,2,2,0));
    put(e, "loss_mask", nums(0,1,1,0));
    put(e, "nutzlaenge", moo_number(3)); put(e, "ziel_start", moo_number(1));
    MooValue es = moo_list_new(1); moo_retain(e); moo_list_append(es,e);
    MooValue d = moo_dict_new(); put(d, "tokenizer_id", moo_string_new("test")); put(d, "beispiele", es);
    MooValue cfg = moo_dict_new(); put(cfg,"tokenizer_id",moo_string_new("test"));
    put(cfg,"sequenzlaenge",moo_number(4)); put(cfg,"vokab",moo_number(4));
    moo_retain(params); put(cfg,"params",params);
    put(cfg,"optimierer",moo_string_new("sgd")); put(cfg,"rate",moo_number(0.1)); put(cfg,"schritte",moo_number(30));
    MooValue fn = moo_func_new((void*)forward, 1, "forward");
    MooValue result = moo_train_sft(fn,d,cfg);
    CHECK(!moo_error_flag && result.tag == MOO_DICT);
    MooValue loss = get(result,"verluste");
    CHECK(loss.tag == MOO_LIST && MV_LIST(loss)->length == 30);
    CHECK(MV_NUM(MV_LIST(loss)->items[29]) < MV_NUM(MV_LIST(loss)->items[0]));
    moo_release(loss); moo_release(result);
    MooValue bad = moo_func_new((void*)forward, 0, "bad");
    result = moo_train_sft(bad,d,cfg);
    CHECK(moo_error_flag && result.tag == MOO_NONE);
    moo_error_flag = 0; moo_release(result); moo_release(bad);
    put(e,"loss_mask",nums(1,1,1,0));
    result = moo_train_sft(fn,d,cfg);
    CHECK(moo_error_flag && result.tag == MOO_NONE);
    moo_error_flag = 0; moo_release(result);
    MooValue windows = moo_list_new(3);
    MooValue first = window(0);
    moo_retain(first); moo_list_append(windows, first);
    moo_list_append(windows, window(2)); moo_list_append(windows, window(4));
    put(d, "beispiele", windows);
    put(cfg, "encoder", moo_func_new((void*)encoder, 1, "encoder"));
    put(cfg, "schritte", moo_number(3));
    result = moo_train_korrekturen(fn, d, cfg);
    CHECK(!moo_error_flag && result.tag == MOO_DICT);
    moo_release(result);
    put(first, "fenster_offset", moo_number(2));
    result = moo_train_korrekturen(fn, d, cfg);
    CHECK(moo_error_flag && result.tag == MOO_NONE);
    moo_error_flag = 0; moo_release(result); moo_release(first);
    moo_release(fn); moo_release(e); moo_release(d); moo_release(cfg);
    moo_release(params); moo_release(layers); moo_ag_reset();
    puts("P5 SFT ASAN OK");
    return 0;
}

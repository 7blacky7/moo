#include "../moo_distill.h"
#include "../moo_dpo.h"
#include <math.h>
#include <float.h>
#include <stdio.h>
MOO_TLS int moo_error_flag = 0;
MOO_TLS MooValue moo_last_error;
MOO_TLS int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue e) { moo_release(e); moo_error_flag = 1; }
void moo_socket_free(void *p) {(void)p;} void moo_thread_free(void *p) {(void)p;}
void moo_channel_free(void *p) {(void)p;} void moo_db_free(void *p) {(void)p;}
void moo_db_stmt_free(void *p) {(void)p;} void moo_window_free(void *p) {(void)p;}
void moo_web_free(void *p) {(void)p;} void moo_voxel_free(void *p) {(void)p;}
void moo_frame_free(void *p) {(void)p;} void moo_gif_handle_free(void *p) {(void)p;}
void moo_video_handle_free(void *p) {(void)p;}
static MooValue tensor(int ndim, int32_t *shape, const float *values) {
    MooTensor *t = moo_tensor_raw(ndim, shape);
    for (int64_t i = 0; i < t->size; ++i) t->data[i] = values[i];
    MooValue v; v.tag = MOO_TENSOR; moo_val_set_ptr(&v, t); return v;
}
static double loss(MooValue s, MooValue t, MooValue m, double temp) {
    MooValue v = moo_distill_kl(s,t,m,temp);
    if (v.tag != MOO_TENSOR) return NAN;
    moo_tensor_f32_sichern(MV_TENSOR(v));
    double x = MV_TENSOR(v)->data[0]; moo_release(v); return x;
}
static MooValue parameter;
static MooValue forward(MooValue x) { moo_release(x); moo_retain(parameter); return parameter; }
static void put(MooValue d, const char *k, MooValue v) { moo_dict_set(d, moo_string_new(k), v); }
static MooValue get(MooValue d, const char *k) { return moo_dict_get(d, moo_string_new(k)); }
static MooValue pair(int a, int b) {
    MooValue v=moo_list_new(2); moo_list_append(v,moo_number(a)); moo_list_append(v,moo_number(b)); return v;
}
static int trainer(MooValue s, MooValue t) {
    parameter=s; moo_release(moo_tensor_mit_gradient(t));
    MooValue e=moo_dict_new(); put(e,"input_ids",pair(0,1)); put(e,"target_ids",pair(1,2));
    put(e,"loss_mask",pair(1,1)); moo_retain(t); put(e,"lehrer_logits",t);
    MooValue es=moo_list_new(1); moo_retain(e); moo_list_append(es,e);
    MooValue d=moo_dict_new(); put(d,"tokenizer_id",moo_string_new("test")); put(d,"lehrer_tokenizer_id",moo_string_new("test")); put(d,"beispiele",es);
    MooValue cfg=moo_dict_new(); put(cfg,"tokenizer_id",moo_string_new("test"));
    put(cfg,"sequenzlaenge",moo_number(2)); put(cfg,"vokab",moo_number(3));
    put(cfg,"schritte",moo_number(20)); put(cfg,"temperatur",moo_number(2)); put(cfg,"alpha",moo_number(1));
    MooValue params=moo_list_new(1); moo_retain(s); moo_list_append(params,s); put(cfg,"params",params);
    put(cfg,"optimierer",moo_string_new("sgd")); put(cfg,"rate",moo_number(0.2)); put(cfg,"momentum",moo_number(0));
    MooValue fn=moo_func_new((void*)forward,1,"forward");
    float before=MV_TENSOR(s)->data[0]; MooValue result=moo_train_distillation(fn,d,cfg);
    if (moo_error_flag || result.tag!=MOO_DICT) return 10;
    MooValue h=get(result,"verluste");
    moo_tensor_f32_sichern(MV_TENSOR(s));
    if (h.tag!=MOO_LIST || MV_LIST(h)->length!=20 || MV_NUM(MV_LIST(h)->items[19])>=MV_NUM(MV_LIST(h)->items[0]) ||
        MV_TENSOR(s)->data[0]==before || MV_TENSOR(t)->grad) return 11;
    moo_release(h); moo_release(result);
    put(e,"lehrer_logits",moo_none()); result=moo_train_distillation(fn,d,cfg);
    if (!moo_error_flag || result.tag!=MOO_NONE) return 12;
    moo_error_flag=0; moo_release(result);
    moo_release(fn); moo_release(e); moo_release(d); moo_release(cfg); moo_ag_reset();
    return 0;
}
static double dpo_value(MooValue c, MooValue r) {
    MooValue v=moo_dpo_loss(c,r,-2,-3,0.7);
    if (v.tag!=MOO_TENSOR) return NAN;
    moo_tensor_f32_sichern(MV_TENSOR(v)); double x=MV_TENSOR(v)->data[0]; moo_release(v); return x;
}
static int dpo_test(void) {
    int32_t shape[1]={1}; float c0[]={-1.2f}, r0[]={-2.4f};
    MooValue c=tensor(1,shape,c0), r=tensor(1,shape,r0);
    moo_release(moo_tensor_mit_gradient(c)); moo_release(moo_tensor_mit_gradient(r)); moo_ag_an();
    MooValue v=moo_dpo_loss(c,r,-2,-3,0.7);
    if (v.tag!=MOO_TENSOR) return 20;
    moo_tensor_f32_sichern(MV_TENSOR(v)); double actual=MV_TENSOR(v)->data[0];
    double expected=log1p(exp(-0.7*(1.2-1.0)));
    moo_release(moo_tensor_rueckwaerts(v));
    moo_tensor_grad_sichern(MV_TENSOR(c)); moo_tensor_grad_sichern(MV_TENSOR(r));
    double gc=MV_TENSOR(c)->grad[0], gr=MV_TENSOR(r)->grad[0];
    moo_release(v); moo_ag_reset(); moo_ag_aus();
    moo_tensor_f32_sichern(MV_TENSOR(c)); moo_tensor_f32_sichern(MV_TENSOR(r));
    MV_TENSOR(c)->data[0]=c0[0]+0.001f; MV_TENSOR(c)->valid=MOO_V_DATA; double plus=dpo_value(c,r);
    MV_TENSOR(c)->data[0]=c0[0]-0.001f; MV_TENSOR(c)->valid=MOO_V_DATA; double minus=dpo_value(c,r);
    MV_TENSOR(c)->data[0]=c0[0]; MV_TENSOR(c)->valid=MOO_V_DATA;
    MV_TENSOR(r)->data[0]=r0[0]+0.001f; MV_TENSOR(r)->valid=MOO_V_DATA; double rplus=dpo_value(c,r);
    MV_TENSOR(r)->data[0]=r0[0]-0.001f; MV_TENSOR(r)->valid=MOO_V_DATA; double rminus=dpo_value(c,r);
    MV_TENSOR(r)->data[0]=r0[0]; MV_TENSOR(r)->valid=MOO_V_DATA;
    if (fabs(actual-expected)>1e-5 || gc>=0 || gr<=0 || fabs(gc+gr)>1e-5 ||
        fabs(gc-(plus-minus)/0.002)>0.001 || fabs(gr-(rplus-rminus)/0.002)>0.001) return 21;
    MV_TENSOR(c)->data[0]=1000; MV_TENSOR(c)->valid=MOO_V_DATA;
    if (!isfinite(dpo_value(c,r))) return 22;
    MooValue bad=moo_dpo_loss(c,r,-2,-3,1e-320);
    if (!moo_error_flag || bad.tag!=MOO_NONE) return 23;
    moo_error_flag=0; moo_release(bad);
    MV_TENSOR(c)->data[0]=FLT_MAX; MV_TENSOR(c)->valid=MOO_V_DATA;
    bad=moo_dpo_loss(c,r,-2,-3,0.7);
    if (!moo_error_flag || bad.tag!=MOO_NONE) return 24;
    moo_error_flag=0; moo_release(bad); moo_release(c); moo_release(r);
    printf("DPOGRAD %.7g %.7g\n",gc,gr);
    return 0;
}

static MooValue pol_layers, ref_layers;
static MooValue pol_forward(MooValue x) { MooValue out=moo_nn_vorwaerts(pol_layers,x); moo_release(x); return out; }
static MooValue ref_forward(MooValue x) { MooValue out=moo_nn_vorwaerts(ref_layers,x); moo_release(x); return out; }
static MooValue dpo_sample(int final) {
    MooValue e=moo_dict_new(); put(e,"input_ids",pair(0,1)); put(e,"target_ids",pair(1,final)); put(e,"loss_mask",pair(0,1)); return e;
}
static MooValue make_layers(void) {
    MooValue l=moo_list_new(2);
    moo_list_append(l,moo_nn_schicht_embedding(moo_number(3),moo_number(3),moo_number(17)));
    MooValue act=moo_string_new("keine");
    moo_list_append(l,moo_nn_schicht_dicht(moo_number(3),moo_number(3),act,moo_number(19)));
    moo_release(act); return l;
}
static int dpo_trainer_test(void) {
    pol_layers=make_layers(); ref_layers=make_layers();
    MooValue pp=moo_nn_parameter(pol_layers), rp=moo_nn_parameter(ref_layers);
    MooTensor *p=MV_TENSOR(MV_LIST(pp)->items[0]), *refp=MV_TENSOR(MV_LIST(rp)->items[0]);
    moo_tensor_f32_sichern(p); moo_tensor_f32_sichern(refp);
    float p0=p->data[3], r0=refp->data[0];
    float frozen[3][16];
    if(MV_LIST(rp)->length!=3) return 33;
    for(int i=0;i<3;i++) {
        MooTensor *rt=MV_TENSOR(MV_LIST(rp)->items[i]); moo_tensor_f32_sichern(rt);
        if(rt->size>16) return 34;
        for(int64_t j=0;j<rt->size;j++) frozen[i][j]=rt->data[j];
    }
    MooValue e=moo_dict_new(); put(e,"gewaehlt",dpo_sample(2)); put(e,"verworfen",dpo_sample(0));
    MooValue es=moo_list_new(1); moo_list_append(es,e);
    MooValue data=moo_dict_new(); put(data,"tokenizer_id",moo_string_new("test")); put(data,"beispiele",es);
    MooValue cfg=moo_dict_new(); put(cfg,"tokenizer_id",moo_string_new("test"));
    put(cfg,"sequenzlaenge",moo_number(2)); put(cfg,"vokab",moo_number(3)); put(cfg,"schritte",moo_number(20));
    put(cfg,"beta",moo_number(0.7)); put(cfg,"optimierer",moo_string_new("sgd"));
    put(cfg,"rate",moo_number(0.2)); put(cfg,"momentum",moo_number(0));
    moo_retain(pp); put(cfg,"params",pp); moo_retain(rp); put(cfg,"referenz_params",rp);
    put(cfg,"referenz_forward",moo_func_new((void*)ref_forward,1,"reference"));
    MooValue fn=moo_func_new((void*)pol_forward,1,"policy");
    MooValue out=moo_train_praeferenzen(fn,data,cfg);
    if(moo_error_flag || out.tag!=MOO_DICT) return 30;
    MooValue h=get(out,"verluste");
    moo_tensor_f32_sichern(p); moo_tensor_f32_sichern(refp);
    if(h.tag!=MOO_LIST || MV_LIST(h)->length!=20 ||
       MV_NUM(MV_LIST(h)->items[19])>=MV_NUM(MV_LIST(h)->items[0]) ||
       p->data[3]==p0 || refp->data[0]!=r0 || refp->grad) return 31;
    for(int i=0;i<3;i++) {
        MooTensor *rt=MV_TENSOR(MV_LIST(rp)->items[i]); moo_tensor_f32_sichern(rt);
        if(rt->grad) return 35;
        for(int64_t j=0;j<rt->size;j++) if(rt->data[j]!=frozen[i][j]) return 36;
    }
    moo_release(h); moo_release(out);
    moo_retain(pp); put(cfg,"referenz_params",pp);
    out=moo_train_praeferenzen(fn,data,cfg);
    if(!moo_error_flag || out.tag!=MOO_NONE) return 32;
    moo_error_flag=0; moo_release(out); moo_release(fn); moo_release(data); moo_release(cfg);
    moo_release(pp); moo_release(rp); moo_release(pol_layers); moo_release(ref_layers); moo_ag_reset();
    puts("DPO training: 20 steps, loss decreases, reference frozen, alias rejected");
    return 0;
}

static double net_ce(MooValue layers, MooValue x, MooValue y, MooValue m) {
    MooValue logits=moo_nn_vorwaerts(layers,x);
    MooValue loss=moo_nn_kreuzentropie_maskiert(logits,y,m);
    moo_tensor_f32_sichern(MV_TENSOR(loss)); double value=MV_TENSOR(loss)->data[0];
    moo_release(logits); moo_release(loss); return value;
}
static int net_ce_grad_test(void) {
    MooValue layers=moo_list_new(2);
    moo_list_append(layers,moo_nn_schicht_embedding(moo_number(4),moo_number(4),moo_number(17)));
    MooValue act=moo_string_new("keine");
    moo_list_append(layers,moo_nn_schicht_dicht(moo_number(4),moo_number(4),act,moo_number(19))); moo_release(act);
    MooValue params=moo_nn_parameter(layers);
    int32_t shape[1]={4}; float xv[]={0,1,2,0}, yv[]={1,2,3,0}, mv[]={0,0,1,0};
    MooValue x=tensor(1,shape,xv),y=tensor(1,shape,yv),m=tensor(1,shape,mv);
    MooTensor *p=MV_TENSOR(MV_LIST(params)->items[MV_LIST(params)->length-1]);
    moo_ag_an(); MooValue out=moo_nn_vorwaerts(layers,x), l=moo_nn_kreuzentropie_maskiert(out,y,m);
    moo_release(moo_tensor_rueckwaerts(l)); moo_tensor_grad_sichern(p); double grad=p->grad[3];
    moo_release(out); moo_release(l); moo_ag_reset(); moo_ag_aus(); moo_tensor_f32_sichern(p);
    float old=p->data[3]; p->data[3]=old+0.001f; p->valid=MOO_V_DATA; double plus=net_ce(layers,x,y,m);
    p->data[3]=old-0.001f; p->valid=MOO_V_DATA; double minus=net_ce(layers,x,y,m);
    p->data[3]=old; p->valid=MOO_V_DATA;
    fprintf(stderr,"NET_GRAD %.8g FD %.8g\n",grad,(plus-minus)/0.002);
    moo_release(x); moo_release(y); moo_release(m); moo_release(params); moo_release(layers);
    return fabs(grad-(plus-minus)/0.002)>0.002 ? 40 : 0;
}

static MooValue broadcast_op(int op,MooValue a,MooValue b) {
    switch(op) {case 0:return moo_tensor_add(a,b); case 1:return moo_tensor_sub(a,b);
        case 2:return moo_tensor_mul(a,b); default:return moo_tensor_div(a,b);}
}
static double broadcast_loss(int op,MooValue a,MooValue b) {
    MooValue out=broadcast_op(op,a,b), sum=moo_tensor_summe(out,moo_number(-1));
    moo_tensor_f32_sichern(MV_TENSOR(sum)); double v=MV_TENSOR(sum)->data[0];
    moo_release(out); moo_release(sum); return v;
}
static int broadcast_test(void) {
    int checks=0;
    for(int kind=0;kind<4;kind++) for(int op=0;op<4;op++) {
        int32_t ashape[2]={kind==3 ? 3:4,4};
        int32_t bshape[2]={kind==0 ? 4 : kind==3 ? 3 : 1,kind==0 || kind==3 ? 1:4};
        int bdim=kind==2 ? 1:2;
        if(bdim==1) bshape[0]=4;
        float av[16],bv[4]; for(int i=0;i<16;i++) av[i]=0.2f+(float)i*0.1f;
        for(int i=0;i<4;i++) bv[i]=1.1f+(float)i*0.2f;
        MooValue a=tensor(2,ashape,av),b=tensor(bdim,bshape,bv);
        moo_release(moo_tensor_mit_gradient(a)); moo_release(moo_tensor_mit_gradient(b)); moo_ag_an();
        MooValue out=broadcast_op(op,a,b);
        moo_tensor_f32_sichern(MV_TENSOR(out));
        for(int i=0;i<ashape[0];i++) for(int j=0;j<ashape[1];j++) {
            double x=av[i*4+j], y=bv[(kind==0 || kind==3) ? i : j];
            double expected=op==0 ? x+y : op==1 ? x-y : op==2 ? x*y : x/y;
            if(fabs(MV_TENSOR(out)->data[i*4+j]-expected)>1e-5) return 50+kind;
        }
        MooValue sum=moo_tensor_summe(out,moo_number(-1)); moo_release(moo_tensor_rueckwaerts(sum));
        moo_tensor_grad_sichern(MV_TENSOR(a)); moo_tensor_grad_sichern(MV_TENSOR(b));
        double ga=MV_TENSOR(a)->grad[0],gb=MV_TENSOR(b)->grad[0];
        moo_release(out); moo_release(sum); moo_ag_reset(); moo_ag_aus();
        moo_tensor_f32_sichern(MV_TENSOR(a)); moo_tensor_f32_sichern(MV_TENSOR(b));
        MV_TENSOR(a)->data[0]=av[0]+0.001f; MV_TENSOR(a)->valid=MOO_V_DATA; double ap=broadcast_loss(op,a,b);
        MV_TENSOR(a)->data[0]=av[0]-0.001f; MV_TENSOR(a)->valid=MOO_V_DATA; double am=broadcast_loss(op,a,b);
        MV_TENSOR(a)->data[0]=av[0]; MV_TENSOR(a)->valid=MOO_V_DATA;
        MV_TENSOR(b)->data[0]=bv[0]+0.001f; MV_TENSOR(b)->valid=MOO_V_DATA; double bp=broadcast_loss(op,a,b);
        MV_TENSOR(b)->data[0]=bv[0]-0.001f; MV_TENSOR(b)->valid=MOO_V_DATA; double bm=broadcast_loss(op,a,b);
        if(fabs(ga-(ap-am)/0.002)>0.003 || fabs(gb-(bp-bm)/0.002)>0.003) {
            fprintf(stderr,"Broadcast kind %d op %d grad %.6g/%.6g FD %.6g/%.6g\n",kind,op,ga,gb,(ap-am)/0.002,(bp-bm)/0.002);
            return 55;
        }
        checks+=2; moo_release(a); moo_release(b);
    }
    printf("Broadcast: 16 Formen/Operatoren, %d FD-Gradienten OK\n",checks); return 0;
}

static int mapped_test(void) {
    int32_t ss[2]={2,3}, ts[2]={2,4};
    float zeros[8]={0}; MooValue s=tensor(2,ss,zeros), t=tensor(2,ts,zeros);
    moo_release(moo_tensor_mit_gradient(s)); parameter=s;
    MooValue e=moo_dict_new(); put(e,"input_ids",pair(0,1)); put(e,"target_ids",pair(1,2));
    put(e,"loss_mask",pair(1,1)); moo_retain(t); put(e,"lehrer_logits",t);
    MooValue es=moo_list_new(1); moo_list_append(es,e);
    MooValue d=moo_dict_new(); put(d,"tokenizer_id",moo_string_new("student"));
    put(d,"lehrer_tokenizer_id",moo_string_new("teacher")); put(d,"beispiele",es);
    MooValue map=moo_list_new(4); moo_list_append(map,moo_number(0)); moo_list_append(map,moo_number(1));
    moo_list_append(map,moo_number(1)); moo_list_append(map,moo_number(2));
    put(d,"lehrer_mapping",map); put(d,"lehrer_zeilen_ausgerichtet",moo_bool(true));
    MooValue cfg=moo_dict_new(); put(cfg,"tokenizer_id",moo_string_new("student"));
    put(cfg,"sequenzlaenge",moo_number(2)); put(cfg,"vokab",moo_number(3)); put(cfg,"schritte",moo_number(1));
    put(cfg,"temperatur",moo_number(2)); put(cfg,"alpha",moo_number(1));
    MooValue params=moo_list_new(1); moo_retain(s); moo_list_append(params,s); put(cfg,"params",params);
    put(cfg,"optimierer",moo_string_new("sgd")); put(cfg,"rate",moo_number(0.1)); put(cfg,"momentum",moo_number(0));
    MooValue fn=moo_func_new((void*)forward,1,"mapped"); MooValue result=moo_train_distillation(fn,d,cfg);
    if(moo_error_flag || result.tag!=MOO_DICT) return 60;
    MooValue h=get(result,"verluste");
    double expected=4*(0.5*log(0.75)+0.5*log(1.5));
    moo_tensor_f32_sichern(MV_TENSOR(s));
    if(h.tag!=MOO_LIST || fabs(MV_NUM(MV_LIST(h)->items[0])-expected)>1e-5 ||
       fabs(MV_TENSOR(s)->data[0]+0.0083333333)>1e-5 ||
       fabs(MV_TENSOR(s)->data[1]-0.0166666667)>1e-5 ||
       fabs(MV_TENSOR(s)->data[2]+0.0083333333)>1e-5) return 61;
    moo_release(h); moo_release(result);
    MooValue mapping=get(d,"lehrer_mapping"); moo_list_set(mapping,moo_number(3),moo_number(3)); moo_release(mapping);
    result=moo_train_distillation(fn,d,cfg);
    if(!moo_error_flag || result.tag!=MOO_NONE) return 63;
    moo_error_flag=0; moo_release(result);
    mapping=get(d,"lehrer_mapping"); moo_list_set(mapping,moo_number(3),moo_number(2)); moo_release(mapping);
    put(d,"lehrer_zeilen_ausgerichtet",moo_bool(false)); result=moo_train_distillation(fn,d,cfg);
    if(!moo_error_flag || result.tag!=MOO_NONE) return 62;
    moo_error_flag=0; moo_release(result); moo_release(fn); moo_release(d); moo_release(cfg);
    moo_release(s); moo_release(t); moo_ag_reset();
    puts("Mapping: 4 Lehrerklassen auf 3 Schuelerklassen und Alignment-Fehler OK"); return 0;
}

static int temperatures_test(void) {
    int32_t shape[2]={1,3}, maskshape[1]={1};
    float sv[]={0.2f,-0.4f,0.9f}, tv[]={-0.1f,0.8f,0.3f}, mask[]={1};
    MooValue s=tensor(2,shape,sv), t=tensor(2,shape,tv), m=tensor(1,maskshape,mask);
    moo_release(moo_tensor_mit_gradient(s)); moo_release(moo_tensor_mit_gradient(t));
    double temps[]={0.5,1,4};
    for(int i=0;i<3;i++) {
        moo_release(moo_tensor_gradient_loeschen(s)); moo_ag_an();
        MooValue l=moo_distill_kl(s,t,m,temps[i]);
        if(l.tag!=MOO_TENSOR) return 70;
        moo_release(moo_tensor_rueckwaerts(l)); moo_tensor_grad_sichern(MV_TENSOR(s));
        double grad=MV_TENSOR(s)->grad[0];
        moo_release(l); moo_ag_reset(); moo_ag_aus(); moo_tensor_f32_sichern(MV_TENSOR(s));
        MV_TENSOR(s)->data[0]=sv[0]+0.001f; MV_TENSOR(s)->valid=MOO_V_DATA; double plus=loss(s,t,m,temps[i]);
        MV_TENSOR(s)->data[0]=sv[0]-0.001f; MV_TENSOR(s)->valid=MOO_V_DATA; double minus=loss(s,t,m,temps[i]);
        MV_TENSOR(s)->data[0]=sv[0]; MV_TENSOR(s)->valid=MOO_V_DATA;
        double sp=0,tp=0; for(int j=0;j<3;j++) {sp+=exp(sv[j]/temps[i]); tp+=exp(tv[j]/temps[i]);}
        double expected=temps[i]*(exp(sv[0]/temps[i])/sp-exp(tv[0]/temps[i])/tp);
        if(!isfinite(plus) || !isfinite(minus) || fabs(grad-expected)>1e-5 || fabs(grad-(plus-minus)/0.002)>0.003 || MV_TENSOR(t)->grad) return 71;
    }
    MooValue bad=moo_distill_kl(s,t,m,0);
    if(!moo_error_flag || bad.tag!=MOO_NONE) return 72;
    moo_error_flag=0; moo_release(bad);
    MV_TENSOR(t)->data[0]=NAN; MV_TENSOR(t)->valid=MOO_V_DATA;
    bad=moo_distill_kl(s,t,m,1);
    if(!moo_error_flag || bad.tag!=MOO_NONE) return 73;
    moo_error_flag=0; moo_release(bad); moo_release(s); moo_release(t); moo_release(m);
    puts("Distillation: 3 Temperaturen, analytisch und FD, NaN/Temperaturfehler OK"); return 0;
}

static double group_objective(MooValue layers,MooValue x1,MooValue y1,MooValue x2,MooValue y2,
                              MooValue yr,MooValue mask,double refdelta) {
    double a=net_ce(layers,x1,y1,mask),b=net_ce(layers,x2,y2,mask),r=net_ce(layers,x1,yr,mask);
    return log1p(exp(-0.7*((-a-b+r)-refdelta)))+0.5*(a+b);
}
static int group_dpo_test(void) {
    pol_layers=make_layers(); ref_layers=make_layers();
    MooValue pp=moo_nn_parameter(pol_layers), rp=moo_nn_parameter(ref_layers);
    MooTensor *p=MV_TENSOR(MV_LIST(pp)->items[2]); moo_tensor_f32_sichern(p);
    int32_t sh[1]={2}; float x1v[]={0,1},y1v[]={1,2},x2v[]={1,2},y2v[]={2,0},yrv[]={1,0},mv[]={0,1};
    MooValue x1=tensor(1,sh,x1v),y1=tensor(1,sh,y1v),x2=tensor(1,sh,x2v),y2=tensor(1,sh,y2v),yr=tensor(1,sh,yrv),m=tensor(1,sh,mv);
    double rd=-net_ce(ref_layers,x1,y1,m)-net_ce(ref_layers,x2,y2,m)+net_ce(ref_layers,x1,yr,m);
    float old=p->data[2];
    p->data[2]=old+0.001f; p->valid=MOO_V_DATA; double plus=group_objective(pol_layers,x1,y1,x2,y2,yr,m,rd);
    p->data[2]=old-0.001f; p->valid=MOO_V_DATA; double minus=group_objective(pol_layers,x1,y1,x2,y2,yr,m,rd);
    p->data[2]=old; p->valid=MOO_V_DATA;
    MooValue c1=dpo_sample(2), c2=moo_dict_new(), r1=dpo_sample(0);
    put(c1,"fenster_offset",moo_number(0)); put(c2,"input_ids",pair(1,2));
    put(c2,"target_ids",pair(2,0)); put(c2,"loss_mask",pair(0,1)); put(c2,"fenster_offset",moo_number(1));
    put(r1,"fenster_offset",moo_number(0));
    MooValue cg=moo_list_new(2), rg=moo_list_new(1); moo_list_append(cg,c1); moo_retain(c2); moo_list_append(cg,c2); moo_list_append(rg,r1);
    MooValue e=moo_dict_new(); put(e,"gewaehlt",cg); put(e,"verworfen",rg);
    MooValue es=moo_list_new(1); moo_list_append(es,e);
    MooValue d=moo_dict_new(); put(d,"tokenizer_id",moo_string_new("test")); put(d,"beispiele",es);
    MooValue cfg=moo_dict_new(); put(cfg,"tokenizer_id",moo_string_new("test")); put(cfg,"sequenzlaenge",moo_number(2));
    put(cfg,"vokab",moo_number(3)); put(cfg,"schritte",moo_number(1)); put(cfg,"beta",moo_number(0.7));
    put(cfg,"sft_gewicht",moo_number(1)); put(cfg,"optimierer",moo_string_new("sgd"));
    put(cfg,"rate",moo_number(0.001)); put(cfg,"momentum",moo_number(0));
    moo_retain(pp); put(cfg,"params",pp); moo_retain(rp); put(cfg,"referenz_params",rp);
    put(cfg,"referenz_forward",moo_func_new((void*)ref_forward,1,"ref"));
    MooValue fn=moo_func_new((void*)pol_forward,1,"pol"); MooValue out=moo_train_praeferenzen(fn,d,cfg);
    if(moo_error_flag || out.tag!=MOO_DICT) return 80;
    moo_tensor_f32_sichern(p); double grad=(old-p->data[2])/0.001;
    if(fabs(grad-(plus-minus)/0.002)>0.003) {
        fprintf(stderr,"Group CE+DPO gradient %.7g FD %.7g\n",grad,(plus-minus)/0.002); return 81;
    }
    moo_release(out); put(c2,"fenster_offset",moo_number(0));
    out=moo_train_praeferenzen(fn,d,cfg);
    if(!moo_error_flag || out.tag!=MOO_NONE) return 82;
    moo_error_flag=0; moo_release(out); moo_release(c2); moo_release(fn); moo_release(d); moo_release(cfg);
    moo_release(x1); moo_release(y1); moo_release(x2); moo_release(y2); moo_release(yr); moo_release(m);
    moo_release(pp); moo_release(rp); moo_release(pol_layers); moo_release(ref_layers); moo_ag_reset();
    puts("DPO: Mehrfenster-CE+DPO FD und Duplikat-Ablehnung OK"); return 0;
}
int main(void) {
    int32_t shape[2] = {2,3}, ms[1] = {2};
    float sv[] = {0.2f,-0.4f,0.9f,2.f,-1.f,0.f};
    float tv[] = {-0.1f,0.8f,0.3f,0.f,1.f,-2.f};
    float mask[] = {1,0};
    MooValue s = tensor(2,shape,sv), t = tensor(2,shape,tv), m = tensor(1,ms,mask);
    moo_ag_an(); moo_release(moo_tensor_mit_gradient(s));
    MooValue l = moo_distill_kl(s,t,m,2.0);
    if (l.tag != MOO_TENSOR || moo_error_flag) return 1;
    moo_release(moo_tensor_rueckwaerts(l));
    moo_tensor_grad_sichern(MV_TENSOR(s));
    float grad[6]; for(int i=0;i<6;i++) grad[i]=MV_TENSOR(s)->grad[i];
    moo_release(l); moo_ag_reset(); moo_ag_aus();
    moo_tensor_f32_sichern(MV_TENSOR(s));
    printf("GRAD %.7g %.7g %.7g %.7g %.7g %.7g\n",grad[0],grad[1],grad[2],grad[3],grad[4],grad[5]);
    for(int i=0;i<6;i++) {
        float old=MV_TENSOR(s)->data[i];
        MV_TENSOR(s)->data[i]=old+0.001f; MV_TENSOR(s)->valid=MOO_V_DATA; double a=loss(s,t,m,2.0);
        MV_TENSOR(s)->data[i]=old-0.001f; MV_TENSOR(s)->valid=MOO_V_DATA; double b=loss(s,t,m,2.0);
        MV_TENSOR(s)->data[i]=old; MV_TENSOR(s)->valid=MOO_V_DATA;
        if (!isfinite(a) || !isfinite(b) || fabs(grad[i]-(a-b)/0.002)>0.002) {
            fprintf(stderr,"Gradient %d: %g vs %g\n",i,grad[i],(a-b)/0.002); return 1;
        }
    }
    if (loss(t,t,m,2.0) < -1e-5 || fabs(loss(t,t,m,2.0)) > 1e-5) return 2;
    MooValue missing = moo_distill_kl(s,moo_none(),m,2.0);
    if (!moo_error_flag || missing.tag != MOO_NONE) return 3;
    moo_error_flag=0; int rc=trainer(s,t); if (rc) return rc;
    rc=dpo_test(); if (rc) return rc;
    rc=dpo_trainer_test(); if (rc) return rc;
    rc=net_ce_grad_test(); if (!rc) rc=broadcast_test(); if (!rc) rc=mapped_test(); if (!rc) rc=temperatures_test(); if (!rc) rc=group_dpo_test();
    moo_release(s); moo_release(t); moo_release(m); if (rc) return rc;
    puts("Distillation: 6 FD-Gradienten, Identitaet und fehlender Lehrer OK"); return 0;
}

#include "moo_distill.h"
#include <math.h>
#include <float.h>
#include <string.h>

static bool dist_map_valid(MooValue map, int teacher_cols, int student_cols) {
    if (map.tag==MOO_NONE) return teacher_cols==student_cols;
    if (map.tag!=MOO_LIST || MV_LIST(map)->length!=teacher_cols) return false;
    for (int i=0;i<teacher_cols;i++) {
        MooValue v=MV_LIST(map)->items[i];
        if (v.tag!=MOO_NUMBER || !isfinite(MV_NUM(v)) || MV_NUM(v)<0 ||
            MV_NUM(v)>=student_cols || floor(MV_NUM(v))!=MV_NUM(v)) return false;
    }
    return true;
}
static MooValue dist_kl(MooValue schueler, MooValue lehrer, MooValue maske, double temperatur, MooValue map) {
    if (schueler.tag != MOO_TENSOR || lehrer.tag != MOO_TENSOR || maske.tag != MOO_TENSOR ||
        !isfinite(temperatur) || temperatur < 0.001 || temperatur > 1000) {
        moo_throw(moo_error("Distillation: Lehrer-Logits, Maske und positive endliche Temperatur erforderlich"));
        return moo_none();
    }
    MooTensor *s = MV_TENSOR(schueler), *t = MV_TENSOR(lehrer), *m = MV_TENSOR(maske);
    if (s->ndim != 2 || t->ndim != 2 || s->shape[0] <= 0 || s->shape[1] <= 0 ||
        s->shape[0] != t->shape[0] || t->shape[1] <= 0 || !dist_map_valid(map,t->shape[1],s->shape[1]) ||
        !((m->ndim == 1 && m->shape[0] == s->shape[0]) ||
          (m->ndim == 2 && m->shape[0] == s->shape[0] && m->shape[1] == 1))) {
        moo_throw(moo_error("Distillation: Lehrer und Schueler brauchen gleiche [Sequenz,Vokabular]-Form; Maske [Sequenz]"));
        return moo_none();
    }
    moo_tensor_f32_sichern(s); moo_tensor_f32_sichern(t); moo_tensor_f32_sichern(m);
    for (int64_t i=0;i<s->size;i++) {
        if (!isfinite(s->data[i]) || fabs((double)s->data[i] / temperatur) > FLT_MAX) {
            moo_throw(moo_error("Distillation: Schueler-Logits oder skalierte Werte sind nicht endlich")); return moo_none();
        }
    }
    int32_t rows = s->shape[0], cols = s->shape[1];
    double count = 0, entropy = 0;
    for (int32_t i = 0; i < rows; i++) {
        if (!isfinite(m->data[i]) || (m->data[i] != 0 && m->data[i] != 1)) {
            moo_throw(moo_error("Distillation: Maske muss binaer sein")); return moo_none();
        }
        count += m->data[i];
    }
    if (count == 0) { moo_throw(moo_error("Distillation: Maske ist leer")); return moo_none(); }
    MooTensor *p = moo_tensor_raw(2, s->shape);
    if (!p) return moo_none();
    MooValue prob; prob.tag = MOO_TENSOR; moo_val_set_ptr(&prob, p);
    int32_t teacher_cols=t->shape[1];
    for (int32_t i = 0; i < rows; i++) {
        int64_t tb=(int64_t)i*teacher_cols, sb=(int64_t)i*cols;
        double maxv=-INFINITY, sum=0;
        for (int32_t j=0;j<teacher_cols;j++) {
            double x=t->data[tb+j];
            if (!isfinite(x)) { moo_release(prob); moo_throw(moo_error("Distillation: Lehrer-Logits muessen endlich sein")); return moo_none(); }
            if (x>maxv) maxv=x;
        }
        for (int32_t j=0;j<teacher_cols;j++) sum+=exp(((double)t->data[tb+j]-maxv)/temperatur);
        for (int32_t j=0;j<teacher_cols;j++) {
            int dest=map.tag==MOO_LIST ? (int)MV_NUM(MV_LIST(map)->items[j]) : j;
            p->data[sb+dest]+=(float)(exp(((double)t->data[tb+j]-maxv)/temperatur)/sum);
        }
        for (int32_t j=0;j<cols;j++) {
            double q=p->data[sb+j];
            if(q>0) entropy+=m->data[i]*q*log(q);
        }
    }
    MooValue scaled = moo_tensor_muls(schueler, moo_number(1.0 / temperatur));
    MooValue ce = scaled.tag == MOO_TENSOR ? moo_nn_kreuzentropie_maskiert(scaled, prob, maske) : moo_none();
    MooValue kl = ce.tag == MOO_TENSOR ? moo_tensor_adds(ce, moo_number(entropy / count)) : moo_none();
    MooValue result = kl.tag == MOO_TENSOR ? moo_tensor_muls(kl, moo_number(temperatur * temperatur)) : moo_none();
    moo_release(prob); moo_release(scaled); moo_release(ce); moo_release(kl);
    return result;
}

MooValue moo_distill_kl(MooValue s, MooValue t, MooValue m, double temperatur) {
    return dist_kl(s,t,m,temperatur,moo_none());
}
static MooValue dist_get(MooValue d, const char *key) {
    return moo_dict_get(d, moo_string_new(key));
}
static void dist_put(MooValue d, const char *key, MooValue v) {
    moo_dict_set(d, moo_string_new(key), v);
}
static bool dist_int(MooValue v, int min, int max, int *out) {
    if (v.tag != MOO_NUMBER || !isfinite(MV_NUM(v)) || MV_NUM(v) < min ||
        MV_NUM(v) > max || floor(MV_NUM(v)) != MV_NUM(v)) return false;
    *out = (int)MV_NUM(v); return true;
}
static bool dist_text_equal(MooValue a, MooValue b) {
    return a.tag == MOO_STRING && b.tag == MOO_STRING && MV_STR(a)->length > 0 &&
        MV_STR(a)->length == MV_STR(b)->length &&
        memcmp(MV_STR(a)->chars, MV_STR(b)->chars, (size_t)MV_STR(a)->length) == 0;
}
static bool dist_example(MooValue e, int seq, int vocab, MooValue map) {
    if (e.tag != MOO_DICT) return false;
    MooValue x = dist_get(e,"input_ids"), y = dist_get(e,"target_ids");
    MooValue m = dist_get(e,"loss_mask"), t = dist_get(e,"lehrer_logits");
    bool ok = x.tag == MOO_LIST && y.tag == MOO_LIST && m.tag == MOO_LIST &&
        MV_LIST(x)->length == seq && MV_LIST(y)->length == seq && MV_LIST(m)->length == seq &&
        t.tag == MOO_TENSOR && MV_TENSOR(t)->ndim == 2 &&
        MV_TENSOR(t)->shape[0] == seq && MV_TENSOR(t)->shape[1]>0 &&
        dist_map_valid(map,MV_TENSOR(t)->shape[1],vocab);
    int active = 0;
    if (ok) for (int i=0; i<seq; ++i) {
        int xi, yi, mi;
        if (!dist_int(MV_LIST(x)->items[i],0,vocab-1,&xi) ||
            !dist_int(MV_LIST(y)->items[i],0,vocab-1,&yi) ||
            !dist_int(MV_LIST(m)->items[i],0,1,&mi)) { ok=false; break; }
        active += mi;
    }
    if (ok) {
        MooTensor *teacher = MV_TENSOR(t);
        moo_tensor_f32_sichern(teacher);
        for (int64_t i=0;i<teacher->size;i++) if (!isfinite(teacher->data[i])) {ok=false; break;}
    }
    moo_release(x); moo_release(y); moo_release(m); moo_release(t);
    return ok && active > 0;
}
MooValue moo_train_distillation(MooValue forward, MooValue daten, MooValue konfig) {
    if (forward.tag != MOO_FUNC || !MV_FUNC(forward)->fn_ptr || MV_FUNC(forward)->arity != 1 ||
        daten.tag != MOO_DICT || konfig.tag != MOO_DICT) {
        moo_throw(moo_error("Distillation: forward, Daten und Konfiguration erforderlich")); return moo_none();
    }
    MooValue seqv=dist_get(konfig,"sequenzlaenge"), vv=dist_get(konfig,"vokab");
    MooValue st=dist_get(konfig,"schritte"), tempv=dist_get(konfig,"temperatur");
    MooValue alphav=dist_get(konfig,"alpha"), dt=dist_get(daten,"tokenizer_id");
    MooValue kt=dist_get(konfig,"tokenizer_id"), lt=dist_get(daten,"lehrer_tokenizer_id");
    MooValue examples=dist_get(daten,"beispiele"), map=dist_get(daten,"lehrer_mapping");
    MooValue aligned=dist_get(daten,"lehrer_zeilen_ausgerichtet");
    int seq=0, vocab=0, steps=0;
    double temp=tempv.tag==MOO_NUMBER ? MV_NUM(tempv) : 1.0;
    double alpha=alphav.tag==MOO_NUMBER ? MV_NUM(alphav) : 1.0;
    bool ok=dist_int(seqv,2,4096,&seq) && dist_int(vv,1,16777216,&vocab) &&
        dist_int(st,1,1000000,&steps) && isfinite(temp) && temp>=0.001 && temp<=1000 &&
        (tempv.tag==MOO_NONE || tempv.tag==MOO_NUMBER) &&
        (alphav.tag==MOO_NONE || alphav.tag==MOO_NUMBER) &&
        isfinite(alpha) && alpha>=0 && alpha<=1 && dist_text_equal(dt,kt) &&
        lt.tag==MOO_STRING && MV_STR(lt)->length>0 &&
        (dist_text_equal(dt,lt) || (map.tag==MOO_LIST && aligned.tag==MOO_BOOL && MV_BOOL(aligned))) &&
        examples.tag==MOO_LIST && MV_LIST(examples)->length>0;
    moo_release(seqv); moo_release(vv); moo_release(st); moo_release(tempv);
    moo_release(alphav); moo_release(dt); moo_release(kt); moo_release(lt); moo_release(aligned);
    if (ok) for (int i=0;i<MV_LIST(examples)->length;i++)
        if (!dist_example(MV_LIST(examples)->items[i],seq,vocab,map)) {ok=false; break;}
    if (!ok) { moo_release(map); moo_release(examples); moo_throw(moo_error("Distillation: Lehrer-Logits fehlen oder Daten, Tokenizer, Form, Maske oder Konfiguration ungueltig")); return moo_none(); }
    MooValue params=dist_get(konfig,"params"), opt=dist_get(konfig,"opt");
    if (params.tag!=MOO_LIST || MV_LIST(params)->length==0) ok=false;
    if (ok && opt.tag==MOO_NONE) {
        MooValue art=dist_get(konfig,"optimierer"), rate=dist_get(konfig,"rate");
        MooValue mom=dist_get(konfig,"momentum"), decay=dist_get(konfig,"decay");
        if (art.tag==MOO_STRING && rate.tag==MOO_NUMBER && isfinite(MV_NUM(rate)) && MV_NUM(rate)>0) {
            if (!strcmp(MV_STR(art)->chars,"sgd")) opt=moo_nn_opt_sgd(params,rate,mom);
            else if (!strcmp(MV_STR(art)->chars,"adam")) opt=moo_nn_opt_adam(params,rate);
            else if (!strcmp(MV_STR(art)->chars,"adamw")) opt=moo_nn_opt_adamw(params,rate,decay);
        }
        moo_release(art); moo_release(rate); moo_release(mom); moo_release(decay);
    } else if (ok && opt.tag==MOO_DICT) {
        MooValue op=dist_get(opt,"params");
        ok=op.tag==MOO_LIST && MV_LIST(op)->length==MV_LIST(params)->length;
        if (ok) for(int i=0;i<MV_LIST(op)->length;i++)
            if (MV_LIST(op)->items[i].tag!=MOO_TENSOR ||
                MV_LIST(op)->items[i].data!=MV_LIST(params)->items[i].data) {ok=false; break;}
        moo_release(op);
    }
    if (!ok || opt.tag!=MOO_DICT || moo_error_flag) {
        moo_release(params); moo_release(opt); moo_release(examples); moo_release(map);
        if (!moo_error_flag) moo_throw(moo_error("Distillation: Optimierer oder Parameter ungueltig"));
        return moo_none();
    }
    bool was_on=moo_ag_ist_an(); moo_ag_an();
    MooValue history=moo_list_new(steps);
    for(int i=0;i<steps && ok && !moo_error_flag;i++) {
        MooValue e=MV_LIST(examples)->items[i % MV_LIST(examples)->length];
        MooValue il=dist_get(e,"input_ids"), yl=dist_get(e,"target_ids"), ml=dist_get(e,"loss_mask");
        MooValue teacher=dist_get(e,"lehrer_logits");
        MooValue input=moo_tensor_aus_liste(il), target=moo_tensor_aus_liste(yl), mask=moo_tensor_aus_liste(ml);
        moo_release(il); moo_release(yl); moo_release(ml);
        MooValue output=moo_none(), kl=moo_none(), ce=moo_none(), a=moo_none(), b=moo_none(), loss=moo_none();
        if (!moo_error_flag && input.tag==MOO_TENSOR) {moo_retain(input); output=moo_func_call_1(forward,input);}
        if (!moo_error_flag && output.tag==MOO_TENSOR && MV_TENSOR(output)->ndim==2 &&
            MV_TENSOR(output)->shape[0]==seq && MV_TENSOR(output)->shape[1]==vocab) {
            if (alpha>0) kl=dist_kl(output,teacher,mask,temp,map);
            if (!moo_error_flag && alpha<1) ce=moo_nn_kreuzentropie_maskiert(output,target,mask);
            if (alpha==0 && ce.tag==MOO_TENSOR) { moo_retain(ce); loss=ce; }
            else if (alpha==1 && kl.tag==MOO_TENSOR) { moo_retain(kl); loss=kl; }
            else if (kl.tag==MOO_TENSOR && ce.tag==MOO_TENSOR) {
                a=moo_tensor_muls(kl,moo_number(alpha)); b=moo_tensor_muls(ce,moo_number(1-alpha));
                if (a.tag==MOO_TENSOR && b.tag==MOO_TENSOR) loss=moo_tensor_add(a,b);
            }
        }
        if (!moo_error_flag && loss.tag==MOO_TENSOR) {
            moo_tensor_f32_sichern(MV_TENSOR(loss)); double value=MV_TENSOR(loss)->data[0];
            if (isfinite(value)) {
                moo_list_append(history,moo_number(value)); moo_release(moo_tensor_rueckwaerts(loss));
                if (!moo_error_flag) moo_release(moo_nn_opt_schritt(opt));
            } else ok=false;
        } else ok=false;
        moo_release(input); moo_release(target); moo_release(mask); moo_release(teacher);
        moo_release(output); moo_release(kl); moo_release(ce); moo_release(a); moo_release(b); moo_release(loss);
    }
    if (!ok || moo_error_flag) moo_ag_reset();
    if (!was_on) moo_ag_aus();
    moo_release(params); moo_release(examples); moo_release(map);
    if (!ok || moo_error_flag) {
        moo_release(opt); moo_release(history);
        if (!moo_error_flag) moo_throw(moo_error("Distillation: Vorwaertslauf oder Verlust ungueltig"));
        return moo_none();
    }
    MooValue result=moo_dict_new(); dist_put(result,"opt",opt); dist_put(result,"verluste",history);
    dist_put(result,"schritte",moo_number(steps)); return result;
}


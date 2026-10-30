#include "moo_dpo.h"
#include <math.h>
#include <float.h>
#include <string.h>

MooValue moo_dpo_loss(MooValue gewaehlt, MooValue verworfen, double ref_gewaehlt, double ref_verworfen, double beta) {
    if (gewaehlt.tag != MOO_TENSOR || verworfen.tag != MOO_TENSOR ||
        MV_TENSOR(gewaehlt)->ndim != 1 || MV_TENSOR(gewaehlt)->size != 1 ||
        MV_TENSOR(verworfen)->ndim != 1 || MV_TENSOR(verworfen)->size != 1 ||
        !isfinite(ref_gewaehlt) || !isfinite(ref_verworfen) ||
        !isfinite(beta) || beta <= 0 || beta > 1000 || (float)beta == 0.0f) {
        moo_throw(moo_error("DPO: zwei skalare Logprobs, endliche Referenzwerte und positives Beta erforderlich"));
        return moo_none();
    }
    MooTensor *c=MV_TENSOR(gewaehlt), *r=MV_TENSOR(verworfen);
    moo_tensor_f32_sichern(c); moo_tensor_f32_sichern(r);
    double shift=ref_verworfen-ref_gewaehlt;
    double delta=(double)c->data[0]-(double)r->data[0];
    double z=beta*(delta+shift);
    if (!isfinite(delta) || fabs(delta)>FLT_MAX/4 || !isfinite(z) || fabs(z)>FLT_MAX/4 ||
        !isfinite(shift) || fabs(shift)>FLT_MAX/4) {
        moo_throw(moo_error("DPO: Logprob-Differenz ist nicht endlich oder zu gross")); return moo_none();
    }
    int32_t two[1]={2};
    MooTensor *selector=moo_tensor_raw(1,two);
    if (!selector) return moo_none();
    selector->data[0]=1; selector->data[1]=0;
    MooValue sel; sel.tag=MOO_TENSOR; moo_val_set_ptr(&sel,selector);
    MooValue diff=moo_tensor_sub(gewaehlt,verworfen);
    MooValue shifted=diff.tag==MOO_TENSOR ? moo_tensor_adds(diff,moo_number(shift)) : moo_none();
    MooValue score=shifted.tag==MOO_TENSOR ? moo_tensor_muls(shifted,moo_number(beta)) : moo_none();
    MooValue pair=score.tag==MOO_TENSOR ? moo_tensor_mul(score,sel) : moo_none();
    MooValue ls=pair.tag==MOO_TENSOR ? moo_tensor_logsoftmax(pair) : moo_none();
    MooValue picked=ls.tag==MOO_TENSOR ? moo_tensor_mul(ls,sel) : moo_none();
    MooValue sum=picked.tag==MOO_TENSOR ? moo_tensor_summe(picked,moo_number(-1)) : moo_none();
    MooValue result=sum.tag==MOO_TENSOR ? moo_tensor_neg(sum) : moo_none();
    moo_release(sel); moo_release(diff); moo_release(shifted);
    moo_release(score); moo_release(pair); moo_release(ls); moo_release(picked); moo_release(sum);
    return result;
}


static MooValue dpo_get(MooValue d,const char *k) { return moo_dict_get(d,moo_string_new(k)); }
static void dpo_put(MooValue d,const char *k,MooValue v) { moo_dict_set(d,moo_string_new(k),v); }
static bool dpo_int(MooValue v,int min,int max,int *out) {
    if(v.tag!=MOO_NUMBER || !isfinite(MV_NUM(v)) || MV_NUM(v)<min ||
       MV_NUM(v)>max || floor(MV_NUM(v))!=MV_NUM(v)) return false;
    *out=(int)MV_NUM(v); return true;
}
static bool dpo_text_equal(MooValue a,MooValue b) {
    return a.tag==MOO_STRING && b.tag==MOO_STRING && MV_STR(a)->length>0 &&
        MV_STR(a)->length==MV_STR(b)->length &&
        memcmp(MV_STR(a)->chars,MV_STR(b)->chars,(size_t)MV_STR(a)->length)==0;
}
static bool dpo_sample_valid(MooValue e,int seq,int vocab) {
    if(e.tag!=MOO_DICT) return false;
    MooValue x=dpo_get(e,"input_ids"), y=dpo_get(e,"target_ids"), m=dpo_get(e,"loss_mask");
    bool ok=x.tag==MOO_LIST && y.tag==MOO_LIST && m.tag==MOO_LIST &&
        MV_LIST(x)->length==seq && MV_LIST(y)->length==seq && MV_LIST(m)->length==seq;
    int active=0;
    if(ok) for(int i=0;i<seq;i++) {
        int xi,yi,mi;
        if(!dpo_int(MV_LIST(x)->items[i],0,vocab-1,&xi) ||
           !dpo_int(MV_LIST(y)->items[i],0,vocab-1,&yi) ||
           !dpo_int(MV_LIST(m)->items[i],0,1,&mi)) {ok=false; break;}
        active+=mi;
        if(i+1<seq && mi && MV_LIST(m)->items[i+1].tag==MOO_NUMBER &&
           MV_NUM(MV_LIST(m)->items[i+1])==1 && MV_LIST(x)->items[i+1].data!=MV_LIST(y)->items[i].data) {
            ok=false; break;
        }
    }
    moo_release(x); moo_release(y); moo_release(m); return ok && active>0;
}
static bool dpo_same_prompt(MooValue c,MooValue r,int seq) {
    MooValue cx=dpo_get(c,"input_ids"), rx=dpo_get(r,"input_ids");
    MooValue cm=dpo_get(c,"loss_mask"), rm=dpo_get(r,"loss_mask");
    int ci=seq, ri=seq;
    for(int i=0;i<seq;i++) {
        if(ci==seq && MV_NUM(MV_LIST(cm)->items[i])==1) ci=i;
        if(ri==seq && MV_NUM(MV_LIST(rm)->items[i])==1) ri=i;
    }
    bool same=ci==ri && ci<seq;
    if(same) for(int i=0;i<=ci;i++)
        if(MV_LIST(cx)->items[i].data!=MV_LIST(rx)->items[i].data) {same=false; break;}
    moo_release(cx); moo_release(rx); moo_release(cm); moo_release(rm); return same;
}

/* Listen gruppieren Fenster einer Antwort. Zielpositionen muessen in globaler
 * Reihenfolge ohne Luecke oder Duplikat erscheinen. */
static bool dpo_group_valid(MooValue g,int seq,int vocab) {
    if(g.tag==MOO_DICT) return dpo_sample_valid(g,seq,vocab);
    if(g.tag!=MOO_LIST || MV_LIST(g)->length<1 || MV_LIST(g)->length>1000000) return false;
    int last=-1, prev_offset=-1;
    for(int i=0;i<MV_LIST(g)->length;i++) {
        MooValue e=MV_LIST(g)->items[i];
        if(!dpo_sample_valid(e,seq,vocab)) return false;
        MooValue ov=dpo_get(e,"fenster_offset"); int offset=0;
        bool valid=dpo_int(ov,0,1000000,&offset) && offset>prev_offset; moo_release(ov);
        if(!valid) return false;
        MooValue mask=dpo_get(e,"loss_mask");
        for(int j=0;j<seq;j++) if(MV_NUM(MV_LIST(mask)->items[j])==1) {
            int pos=offset+j;
            if(last>=0 && pos!=last+1) {moo_release(mask); return false;}
            last=pos;
        }
        moo_release(mask); prev_offset=offset;
    }
    return last>=0;
}
static MooValue dpo_first(MooValue g) {return g.tag==MOO_LIST ? MV_LIST(g)->items[0] : g;}
static bool dpo_pair_valid(MooValue c,MooValue r,int seq,int vocab) {
    if(!dpo_group_valid(c,seq,vocab) || !dpo_group_valid(r,seq,vocab)) return false;
    MooValue a=dpo_first(c), b=dpo_first(r);
    if(c.tag==MOO_LIST || r.tag==MOO_LIST) {
        if(c.tag!=MOO_LIST || r.tag!=MOO_LIST) return false;
        MooValue ao=dpo_get(a,"fenster_offset"), bo=dpo_get(b,"fenster_offset");
        bool same=ao.tag==MOO_NUMBER && bo.tag==MOO_NUMBER && MV_NUM(ao)==MV_NUM(bo);
        moo_release(ao); moo_release(bo); if(!same) return false;
    }
    return dpo_same_prompt(a,b,seq);
}
static int dpo_sample_count(MooValue g) {
    int count=0, n=g.tag==MOO_LIST ? MV_LIST(g)->length : 1;
    for(int i=0;i<n;i++) {
        MooValue e=g.tag==MOO_LIST ? MV_LIST(g)->items[i] : g;
        MooValue m=dpo_get(e,"loss_mask");
        for(int j=0;j<MV_LIST(m)->length;j++) if(MV_NUM(MV_LIST(m)->items[j])==1) count++;
        moo_release(m);
    }
    return count;
}
/* Gibt die Summe der Logprobs auf den maskierten Zielpositionen zurueck. */
static MooValue dpo_logprob_one(MooValue fn,MooValue sample,int seq,int vocab) {
    MooValue xl=dpo_get(sample,"input_ids"), yl=dpo_get(sample,"target_ids"), ml=dpo_get(sample,"loss_mask");
    int count=0; for(int i=0;i<seq;i++) count+=(int)MV_NUM(MV_LIST(ml)->items[i]);
    MooValue x=moo_tensor_aus_liste(xl), y=moo_tensor_aus_liste(yl), m=moo_tensor_aus_liste(ml);
    moo_release(xl); moo_release(yl); moo_release(ml);
    MooValue out=moo_none(), ce=moo_none(), result=moo_none();
    if(!moo_error_flag && x.tag==MOO_TENSOR) {moo_retain(x); out=moo_func_call_1(fn,x);}
    if(!moo_error_flag && out.tag==MOO_TENSOR && MV_TENSOR(out)->ndim==2 &&
       MV_TENSOR(out)->shape[0]==seq && MV_TENSOR(out)->shape[1]==vocab)
        ce=moo_nn_kreuzentropie_maskiert(out,y,m);
    if(ce.tag==MOO_TENSOR) result=moo_tensor_muls(ce,moo_number(-count));
    moo_release(x); moo_release(y); moo_release(m); moo_release(out); moo_release(ce);
    return result;
}

static MooValue dpo_logprob(MooValue fn,MooValue sample,int seq,int vocab) {
    if(sample.tag==MOO_DICT) return dpo_logprob_one(fn,sample,seq,vocab);
    MooValue acc=moo_none();
    for(int i=0;i<MV_LIST(sample)->length && !moo_error_flag;i++) {
        MooValue part=dpo_logprob_one(fn,MV_LIST(sample)->items[i],seq,vocab);
        if(part.tag!=MOO_TENSOR) {moo_release(acc); moo_release(part); return moo_none();}
        if(acc.tag==MOO_NONE) acc=part;
        else {MooValue sum=moo_tensor_add(acc,part); moo_release(acc); moo_release(part); acc=sum;}
    }
    return acc;
}
MooValue moo_train_praeferenzen(MooValue forward,MooValue daten,MooValue konfig) {
    if(forward.tag!=MOO_FUNC || !MV_FUNC(forward)->fn_ptr || MV_FUNC(forward)->arity!=1 ||
       daten.tag!=MOO_DICT || konfig.tag!=MOO_DICT) {
        moo_throw(moo_error("DPO: forward, Daten und Konfiguration erforderlich")); return moo_none();
    }
    MooValue ref=dpo_get(konfig,"referenz_forward"), params=dpo_get(konfig,"params");
    MooValue rp=dpo_get(konfig,"referenz_params"), opt=dpo_get(konfig,"opt");
    MooValue seqv=dpo_get(konfig,"sequenzlaenge"), vv=dpo_get(konfig,"vokab");
    MooValue stepsv=dpo_get(konfig,"schritte"), bv=dpo_get(konfig,"beta");
    MooValue sv=dpo_get(konfig,"sft_gewicht");
    MooValue dt=dpo_get(daten,"tokenizer_id"), kt=dpo_get(konfig,"tokenizer_id");
    MooValue examples=dpo_get(daten,"beispiele");
    int seq=0,vocab=0,steps=0; double beta=bv.tag==MOO_NUMBER ? MV_NUM(bv) : 0;
    double sft=sv.tag==MOO_NUMBER ? MV_NUM(sv) : 0;
    bool ok=ref.tag==MOO_FUNC && ref.data!=forward.data && MV_FUNC(ref)->fn_ptr && MV_FUNC(ref)->arity==1 &&
      params.tag==MOO_LIST && MV_LIST(params)->length>0 && rp.tag==MOO_LIST && MV_LIST(rp)->length>0 &&
      dpo_int(seqv,2,4096,&seq) && dpo_int(vv,1,16777216,&vocab) &&
      dpo_int(stepsv,1,1000000,&steps) && isfinite(beta) && beta>0 && beta<=1000 && (float)beta!=0.0f &&
      (sv.tag==MOO_NONE || sv.tag==MOO_NUMBER) && isfinite(sft) && sft>=0 && sft<=1000 &&
      dpo_text_equal(dt,kt) && examples.tag==MOO_LIST && MV_LIST(examples)->length>0;
    moo_release(seqv); moo_release(vv); moo_release(stepsv); moo_release(bv); moo_release(sv); moo_release(dt); moo_release(kt);
    if(ok) for(int i=0;i<MV_LIST(params)->length;i++) {
        MooValue a=MV_LIST(params)->items[i];
        if(a.tag!=MOO_TENSOR) {ok=false; break;}
        for(int j=0;j<MV_LIST(rp)->length;j++) {
            MooValue b=MV_LIST(rp)->items[j];
            if(b.tag!=MOO_TENSOR || a.data==b.data) {ok=false; break;}
        }
    }
    if(ok) for(int i=0;i<MV_LIST(examples)->length;i++) {
        MooValue e=MV_LIST(examples)->items[i];
        if(e.tag!=MOO_DICT) {ok=false; break;}
        MooValue c=dpo_get(e,"gewaehlt"), r=dpo_get(e,"verworfen");
        bool good=dpo_pair_valid(c,r,seq,vocab);
        moo_release(c); moo_release(r); if(!good) {ok=false; break;}
    }
    if(ok && opt.tag==MOO_NONE) {
        MooValue art=dpo_get(konfig,"optimierer"), rate=dpo_get(konfig,"rate");
        MooValue mom=dpo_get(konfig,"momentum"), decay=dpo_get(konfig,"decay");
        if(art.tag==MOO_STRING && rate.tag==MOO_NUMBER && isfinite(MV_NUM(rate)) && MV_NUM(rate)>0) {
            if(!strcmp(MV_STR(art)->chars,"sgd")) opt=moo_nn_opt_sgd(params,rate,mom);
            else if(!strcmp(MV_STR(art)->chars,"adam")) opt=moo_nn_opt_adam(params,rate);
            else if(!strcmp(MV_STR(art)->chars,"adamw")) opt=moo_nn_opt_adamw(params,rate,decay);
        }
        moo_release(art); moo_release(rate); moo_release(mom); moo_release(decay);
    } else if(ok && opt.tag==MOO_DICT) {
        MooValue p=dpo_get(opt,"params"); ok=p.tag==MOO_LIST && MV_LIST(p)->length==MV_LIST(params)->length;
        if(ok) for(int i=0;i<MV_LIST(p)->length;i++)
            if(MV_LIST(p)->items[i].tag!=MOO_TENSOR || MV_LIST(p)->items[i].data!=MV_LIST(params)->items[i].data) {ok=false; break;}
        moo_release(p);
    }
    if(!ok || opt.tag!=MOO_DICT || moo_error_flag) {
        moo_release(ref); moo_release(params); moo_release(rp); moo_release(opt); moo_release(examples);
        if(!moo_error_flag) moo_throw(moo_error("DPO: Daten, Referenzmodell, disjunkte Parameter oder Optimierer ungueltig"));
        return moo_none();
    }
    bool was_on=moo_ag_ist_an(); MooValue history=moo_list_new(steps);
    for(int i=0;i<steps && ok && !moo_error_flag;i++) {
        MooValue e=MV_LIST(examples)->items[i%MV_LIST(examples)->length];
        MooValue c=dpo_get(e,"gewaehlt"), r=dpo_get(e,"verworfen");
        moo_ag_aus();
        MooValue rc=dpo_logprob(ref,c,seq,vocab), rr=dpo_logprob(ref,r,seq,vocab);
        double cv=NAN,rv=NAN;
        if(rc.tag==MOO_TENSOR && rr.tag==MOO_TENSOR && !moo_error_flag) {
            moo_tensor_f32_sichern(MV_TENSOR(rc)); moo_tensor_f32_sichern(MV_TENSOR(rr));
            cv=MV_TENSOR(rc)->data[0]; rv=MV_TENSOR(rr)->data[0];
        }
        moo_release(rc); moo_release(rr);
        moo_ag_an();
        MooValue pc=moo_none(),pr=moo_none(),loss=moo_none();
        if(isfinite(cv) && isfinite(rv) && !moo_error_flag) {
            pc=dpo_logprob(forward,c,seq,vocab); pr=dpo_logprob(forward,r,seq,vocab);
            if(pc.tag==MOO_TENSOR && pr.tag==MOO_TENSOR && !moo_error_flag)
                loss=moo_dpo_loss(pc,pr,cv,rv,beta);
            if(sft>0 && loss.tag==MOO_TENSOR) {
                int count=dpo_sample_count(c);
                MooValue ce=moo_tensor_muls(pc,moo_number(-sft/(double)count));
                MooValue combined=ce.tag==MOO_TENSOR ? moo_tensor_add(loss,ce) : moo_none();
                moo_release(ce); moo_release(loss); loss=combined;
            }
        }
        if(loss.tag==MOO_TENSOR && !moo_error_flag) {
            moo_tensor_f32_sichern(MV_TENSOR(loss)); double value=MV_TENSOR(loss)->data[0];
            if(isfinite(value)) {
                moo_list_append(history,moo_number(value)); moo_release(moo_tensor_rueckwaerts(loss));
                if(!moo_error_flag) moo_release(moo_nn_opt_schritt(opt));
            } else ok=false;
        } else ok=false;
        moo_release(c); moo_release(r); moo_release(pc); moo_release(pr); moo_release(loss);
    }
    if(!ok || moo_error_flag) moo_ag_reset();
    if(!was_on) moo_ag_aus();
    moo_release(ref); moo_release(params); moo_release(rp); moo_release(examples);
    if(!ok || moo_error_flag) {
        moo_release(opt); moo_release(history);
        if(!moo_error_flag) moo_throw(moo_error("DPO: Referenz, Vorwaertslauf oder Verlust ungueltig"));
        return moo_none();
    }
    MooValue result=moo_dict_new(); dpo_put(result,"opt",opt); dpo_put(result,"verluste",history);
    dpo_put(result,"schritte",moo_number(steps)); return result;
}

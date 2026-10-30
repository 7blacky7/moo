/* P5: teacher-forced evaluation over the exact scored UTF-8 suffix.
 * Callbacks consume their argument; all API arguments are borrowed. */
#include "moo_train_eval.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static MooValue ev_get(MooValue d, const char *k) { return moo_dict_get(d, moo_string_new(k)); }
static void ev_set(MooValue d, const char *k, MooValue v) { moo_dict_set(d, moo_string_new(k), v); }
static bool ev_func(MooValue f) {
    return f.tag == MOO_FUNC && MV_FUNC(f)->arity == 1 && MV_FUNC(f)->fn_ptr;
}
static bool ev_int(MooValue v, int low, int high, int *out) {
    if (v.tag != MOO_NUMBER) return false;
    double x = MV_NUM(v);
    if (!isfinite(x) || x < low || x > high || floor(x) != x) return false;
    *out = (int)x; return true;
}
static bool ev_utf8(const unsigned char *s, size_t n, size_t *count) {
    size_t i=0, c=0;
    while (i<n) {
        uint32_t cp; unsigned char a=s[i++]; size_t extra;
        if (a<0x80) { if (!a) return false; cp=a; extra=0; }
        else if (a>=0xc2 && a<=0xdf) { cp=a & 31u; extra=1; }
        else if (a>=0xe0 && a<=0xef) { cp=a & 15u; extra=2; }
        else if (a>=0xf0 && a<=0xf4) { cp=a & 7u; extra=3; }
        else return false;
        if (extra>n-i) return false;
        for (size_t j=0;j<extra;j++) {
            unsigned char b=s[i++]; if ((b & 0xc0u)!=0x80u) return false;
            cp=(cp<<6)|(b & 63u);
        }
        if ((extra==2 && cp<0x800u) || (extra==3 && cp<0x10000u) ||
            (cp>=0xd800u && cp<=0xdfffu) || cp>0x10ffffu) return false;
        c++;
    }
    *count=c; return c>0;
}
static bool ev_equal(MooValue a, MooValue b) {
    return a.tag==MOO_STRING && b.tag==MOO_STRING &&
        MV_STR(a)->length==MV_STR(b)->length &&
        !memcmp(MV_STR(a)->chars, MV_STR(b)->chars, (size_t)MV_STR(a)->length);
}

/* factory(checkpoint_state)->forward(ids), encoder(text)->ids,
 * decoder(ids)->text. The factory must use the passed checkpoint state.
 * Data: {text, split}; config: {tokenizer, encoder, decoder, checkpoint_pfad,
 * arch_version, sequenzlaenge, vokab, pad_id}. First token supplies context
 * and is excluded from both the NLL and the text denominators. */
MooValue moo_train_eval_modell(MooValue factory, MooValue daten, MooValue config) {
    MooValue text=moo_none(), split=moo_none(), tok=moo_none(), enc=moo_none(), dec=moo_none();
    MooValue path=moo_none(), arch=moo_none(), seqv=moo_none(), vv=moo_none(), pv=moo_none();
    MooValue tokhash=moo_none(), ckbytes=moo_none(), ckhash=moo_none(), dochash=moo_none();
    MooValue erwart=moo_none(), state=moo_none(), forward=moo_none(), ids=moo_none();
    MooValue decoded=moo_none(), firstlist=moo_none(), first=moo_none(), result=moo_none();
    const char *err="evaluiere_modell: ungueltige Daten, Konfiguration oder Modell";
    int seq=0, vocab=0, pad=0; bool was_on=moo_ag_ist_an(); bool ok=false;
    if (daten.tag!=MOO_DICT || config.tag!=MOO_DICT || !ev_func(factory)) goto done;
    text=ev_get(daten,"text"); split=ev_get(daten,"split");
    tok=ev_get(config,"tokenizer"); enc=ev_get(config,"encoder"); dec=ev_get(config,"decoder");
    path=ev_get(config,"checkpoint_pfad"); arch=ev_get(config,"arch_version");
    seqv=ev_get(config,"sequenzlaenge"); vv=ev_get(config,"vokab"); pv=ev_get(config,"pad_id");
    if (text.tag!=MOO_STRING || MV_STR(text)->length<=0 ||
        split.tag!=MOO_STRING ||
        !((MV_STR(split)->length==3 && !memcmp(MV_STR(split)->chars,"val",3)) ||
          (MV_STR(split)->length==4 && !memcmp(MV_STR(split)->chars,"test",4)) ||
          (MV_STR(split)->length==5 && !memcmp(MV_STR(split)->chars,"train",5))) ||
        tok.tag!=MOO_STRING || MV_STR(tok)->length<=0 || !ev_func(enc) || !ev_func(dec) ||
        path.tag!=MOO_STRING || MV_STR(path)->length<=0 ||
        memchr(MV_STR(path)->chars,0,(size_t)MV_STR(path)->length) || arch.tag!=MOO_STRING ||
        MV_STR(arch)->length<=0 || !ev_int(seqv,2,4096,&seq) ||
        !ev_int(vv,1,16777216,&vocab) || !ev_int(pv,0,vocab-1,&pad)) goto done;
    size_t total_c=0;
    if (!ev_utf8((const unsigned char*)MV_STR(text)->chars,(size_t)MV_STR(text)->length,&total_c)) goto done;
    tokhash=moo_sha256(tok); moo_retain(path); ckbytes=moo_file_read(path);
    if (moo_error_flag || ckbytes.tag!=MOO_STRING || MV_STR(ckbytes)->length<=0) goto done;
    ckhash=moo_sha256(ckbytes); dochash=moo_sha256(text);
    erwart=moo_dict_new(); moo_retain(tokhash); ev_set(erwart,"tokenizer_version",tokhash);
    moo_retain(arch); ev_set(erwart,"arch_version",arch);
    state=moo_nn_ckpt_laden(path,erwart);
    if (moo_error_flag || state.tag!=MOO_DICT) goto done;
    /* Reject a file changed between hashing and loading. */
    moo_retain(path); MooValue again=moo_file_read(path);
    MooValue againhash=again.tag==MOO_STRING ? moo_sha256(again) : moo_none();
    bool same_file=ev_equal(ckhash,againhash);
    moo_release(again); moo_release(againhash);
    if (moo_error_flag || !same_file) { err="evaluiere_modell: Checkpoint waehrend Laden geaendert"; goto done; }
    moo_ag_aus();
    moo_retain(state); forward=moo_func_call_1(factory,state);
    if (moo_error_flag || !ev_func(forward)) goto done;
    moo_retain(text); ids=moo_func_call_1(enc,text);
    if (moo_error_flag || ids.tag!=MOO_TENSOR) goto done;
    MooTensor *it=MV_TENSOR(ids); moo_tensor_f32_sichern(it);
    if (moo_error_flag || it->ndim!=1 || it->size<2 || !it->data) goto done;
    for (int64_t i=0;i<it->size;i++) {
        double x=it->data[i]; if (!isfinite(x) || x<0 || x>=vocab || floor(x)!=x) goto done;
    }
    moo_retain(ids); decoded=moo_func_call_1(dec,ids);
    if (moo_error_flag || !ev_equal(decoded,text)) goto done;
    firstlist=moo_list_new(1); moo_list_append(firstlist,moo_number(it->data[0]));
    moo_retain(firstlist); first=moo_func_call_1(dec,firstlist);
    if (moo_error_flag || first.tag!=MOO_STRING || MV_STR(first)->length<=0 ||
        MV_STR(first)->length>=MV_STR(text)->length ||
        memcmp(MV_STR(first)->chars,MV_STR(text)->chars,(size_t)MV_STR(first)->length)) goto done;
    size_t skipped=(size_t)MV_STR(first)->length;
    size_t bytes=(size_t)MV_STR(text)->length-skipped, cps=0;
    if (!ev_utf8((const unsigned char*)MV_STR(text)->chars+skipped,bytes,&cps)) {
        err="evaluiere_modell: erster Token endet nicht an einer UTF-8-Grenze"; goto done;
    }
    double nll=0.0;
    for (int64_t pos=1; pos<it->size; pos++) {
        int64_t start=pos>seq ? pos-seq : 0;
        int used=(int)(pos-start);
        MooValue list=moo_list_new(seq);
        for (int j=0;j<seq;j++)
            moo_list_append(list,moo_number(j<used ? it->data[start+j] : pad));
        MooValue input=moo_tensor_aus_liste(list); moo_release(list);
        if (moo_error_flag || input.tag!=MOO_TENSOR) { moo_release(input); goto done; }
        moo_retain(input); MooValue out=moo_func_call_1(forward,input); moo_release(input);
        if (moo_error_flag || out.tag!=MOO_TENSOR) { moo_release(out); goto done; }
        MooTensor *ot=MV_TENSOR(out); moo_tensor_f32_sichern(ot);
        if (moo_error_flag || ot->ndim!=2 || ot->shape[0]!=seq ||
            ot->shape[1]!=vocab || !ot->data) { moo_release(out); goto done; }
        const float *row=ot->data+(int64_t)(used-1)*vocab;
        double max=-INFINITY;
        for (int j=0;j<vocab;j++) { if (!isfinite(row[j])) { moo_release(out); goto done; } if (row[j]>max) max=row[j]; }
        double sum=0;
        for (int j=0;j<vocab;j++) sum+=exp((double)row[j]-max);
        double loss=max-(double)row[(int)it->data[pos]]+log(sum);
        moo_release(out);
        if (!isfinite(loss) || loss < -1e-10) goto done;
        nll+=loss>0 ? loss : 0;
        if (!isfinite(nll)) goto done;
    }
    result=moo_dict_new(); ev_set(result,"nll",moo_number(nll));
    ev_set(result,"tokens",moo_number((double)(it->size-1)));
    ev_set(result,"bytes",moo_number((double)bytes)); ev_set(result,"codepoints",moo_number((double)cps));
    ev_set(result,"nll_pro_byte",moo_number(nll/(double)bytes));
    ev_set(result,"nll_pro_codepoint",moo_number(nll/(double)cps));
    moo_retain(tokhash); ev_set(result,"tokenizer_sha256",tokhash);
    moo_retain(ckhash); ev_set(result,"checkpoint_sha256",ckhash);
    moo_retain(dochash); ev_set(result,"dokument_sha256",dochash);
    moo_retain(split); ev_set(result,"split",split);
    moo_retain(path); ev_set(result,"checkpoint_pfad",path);
    ev_set(result,"sequenzlaenge",moo_number(seq));
    ev_set(result,"vokab",moo_number(vocab)); ev_set(result,"pad_id",moo_number(pad));
    ok=true;
done:
    if (was_on) moo_ag_an(); else moo_ag_aus();
    moo_release(text); moo_release(split); moo_release(tok); moo_release(enc); moo_release(dec);
    moo_release(path); moo_release(arch); moo_release(seqv); moo_release(vv); moo_release(pv);
    moo_release(tokhash); moo_release(ckbytes); moo_release(ckhash); moo_release(dochash);
    moo_release(erwart); moo_release(state); moo_release(forward); moo_release(ids);
    moo_release(decoded); moo_release(firstlist); moo_release(first);
    if (!ok) { moo_release(result); if (!moo_error_flag) moo_throw(moo_error(err)); return moo_none(); }
    return result;
}

/* Val-only ranking, fail closed on incompatible tokenizer, corpus or protocol. */
static bool ev_hash(MooValue v) {
    if (v.tag!=MOO_STRING || MV_STR(v)->length!=64) return false;
    for (int i=0;i<64;i++) {
        char c=MV_STR(v)->chars[i];
        if (!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
    }
    return true;
}
static bool ev_same_field(MooValue a, MooValue b, const char *key) {
    MooValue x=ev_get(a,key), y=ev_get(b,key);
    bool equal=(x.tag==MOO_STRING && y.tag==MOO_STRING && ev_equal(x,y)) ||
        (x.tag==MOO_NUMBER && y.tag==MOO_NUMBER && isfinite(MV_NUM(x)) &&
         isfinite(MV_NUM(y)) && MV_NUM(x)==MV_NUM(y));
    moo_release(x); moo_release(y); return equal;
}
MooValue moo_train_eval_waehlen(MooValue reports) {
    if (reports.tag!=MOO_LIST || MV_LIST(reports)->length<1) {
        moo_throw(moo_error("checkpoint_waehlen: nichtleere Val-Berichtsliste erforderlich"));
        return moo_none();
    }
    MooList *l=MV_LIST(reports);
    MooValue basis=l->items[0]; int best=-1; double score=INFINITY;
    const char *fields[]={"tokenizer_sha256","dokument_sha256","sequenzlaenge",
                          "vokab","pad_id","bytes","codepoints","tokens"};
    for (int32_t i=0;i<l->length;i++) {
        MooValue r=l->items[i];
        if (r.tag!=MOO_DICT) goto invalid;
        MooValue split=ev_get(r,"split"), metric=ev_get(r,"nll_pro_byte");
        MooValue ch=ev_get(r,"checkpoint_sha256"), th=ev_get(r,"tokenizer_sha256");
        MooValue dh=ev_get(r,"dokument_sha256"), path=ev_get(r,"checkpoint_pfad");
        MooValue total=ev_get(r,"nll"), bv=ev_get(r,"bytes"), cv=ev_get(r,"codepoints");
        MooValue tv=ev_get(r,"tokens"), sv=ev_get(r,"sequenzlaenge"), vv=ev_get(r,"vokab"), pv=ev_get(r,"pad_id");
        int bi=0, ci=0, ti=0, si=0, vi=0, pi=0;
        bool valid=split.tag==MOO_STRING && MV_STR(split)->length==3 &&
            !memcmp(MV_STR(split)->chars,"val",3) && metric.tag==MOO_NUMBER &&
            isfinite(MV_NUM(metric)) && MV_NUM(metric)>=0 &&
            ev_hash(ch) && ev_hash(th) && ev_hash(dh) &&
            path.tag==MOO_STRING && MV_STR(path)->length>0 &&
            total.tag==MOO_NUMBER && isfinite(MV_NUM(total)) && MV_NUM(total)>=0 &&
            ev_int(bv,1,2147483647,&bi) && ev_int(cv,1,2147483647,&ci) &&
            ev_int(tv,1,2147483647,&ti) && ev_int(sv,2,4096,&si) &&
            ev_int(vv,1,16777216,&vi) && ev_int(pv,0,vi-1,&pi) &&
            fabs(MV_NUM(metric)-MV_NUM(total)/(double)bi)<=1e-5*(1+MV_NUM(metric));
        /* json_string uses limited decimal precision; derive ranking from totals. */
        double n=valid ? MV_NUM(total)/(double)bi : 0;
        moo_release(total); moo_release(bv); moo_release(cv); moo_release(tv);
        moo_release(sv); moo_release(vv); moo_release(pv);
        moo_release(split); moo_release(metric); moo_release(ch);
        moo_release(th); moo_release(dh); moo_release(path);
        if (!valid) goto invalid;
        /* Verify the checkpoint still matches the report, not just its label. */
        path=ev_get(r,"checkpoint_pfad"); ch=ev_get(r,"checkpoint_sha256");
        if (memchr(MV_STR(path)->chars,0,(size_t)MV_STR(path)->length)) {
            moo_release(path); moo_release(ch); goto invalid;
        }
        moo_retain(path); MooValue blob=moo_file_read(path);
        MooValue actual=(blob.tag==MOO_STRING && MV_STR(blob)->length>0) ? moo_sha256(blob) : moo_none();
        bool unchanged=!moo_error_flag && ev_equal(actual,ch);
        moo_release(path); moo_release(ch); moo_release(blob); moo_release(actual);
        if (!unchanged) {
            if (!moo_error_flag) moo_throw(moo_error("checkpoint_waehlen: Checkpoint fehlt oder Hash hat sich geaendert"));
            return moo_none();
        }
        for (size_t j=0;j<sizeof(fields)/sizeof(fields[0]);j++)
            if (!ev_same_field(basis,r,fields[j])) {
                moo_throw(moo_error("checkpoint_waehlen: inkompatibler Tokenizer, Val-Datensatz oder Auswertungsprotokoll"));
                return moo_none();
            }
        if (n<score) { score=n; best=i; }
    }
    if (best<0) goto invalid;
    MooValue selected=l->items[best]; moo_retain(selected); return selected;
invalid:
    moo_throw(moo_error("checkpoint_waehlen: ungueltiger Bericht oder nicht Val"));
    return moo_none();
}

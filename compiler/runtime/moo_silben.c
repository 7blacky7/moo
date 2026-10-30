
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/* Additiver orthografischer Tokenizer. Muster: dehyph-exptl 2024-02-28.
 * Bytes im Artefakt sind die einzige Identitaet. Kein globaler Zustand. */
#include "moo_silben.h"
#include "moo_krypto.h"
#include <limits.h>
#include <errno.h>
#include <wchar.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

#define SIL_MAX_TEXT (16u * 1024u * 1024u)
#define SIL_MAX_PAT (1024u * 1024u)
#define SIL_MAX_VOC 65536u
#define SIL_MAX_WORD 128u
#define SIL_MAX_TOKEN 512u
#define SIL_MAGIC "MOOSIL01"
#define SIL_HEADER 24u

typedef struct { const unsigned char *p; uint32_t n, count; } SilEntry;
typedef struct { const unsigned char *blob, *pat; uint32_t len, pn, count; SilEntry *entries, *sorted; } SilTok;

static MooValue sil_error(const char *s) { moo_throw(moo_error(s)); return moo_none(); }
static uint32_t sil_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void sil_put32(unsigned char *p, uint32_t v) {
    p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8); p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24);
}
static int sil_cp(const unsigned char *s, size_t n, uint32_t *out) {
    if (!n) return 0;
    uint32_t x=s[0], min; int k;
    if (x<0x80) { *out=x; return 1; }
    if (x>=0xc2 && x<=0xdf) { x &= 31; k=2; min=0x80; }
    else if (x>=0xe0 && x<=0xef) { x &= 15; k=3; min=0x800; }
    else if (x>=0xf0 && x<=0xf4) { x &= 7; k=4; min=0x10000; }
    else return 0;
    if (n<(size_t)k) return 0;
    for (int i=1;i<k;i++) { if ((s[i]&0xc0)!=0x80) return 0; x=(x<<6)|(s[i]&63); }
    if (x<min || x>0x10ffff || (x>=0xd800 && x<=0xdfff)) return 0;
    *out=x; return k;
}
static int sil_text(const unsigned char *s, size_t n) {
    for (size_t i=0;i<n;) { uint32_t c; int k=sil_cp(s+i,n-i,&c); if (!k || !c) return 0; i+=(size_t)k; }
    return 1;
}
static int sil_entry_cmp(const void *aa, const void *bb) {
    const SilEntry *a=aa,*b=bb; size_t n=a->n<b->n?a->n:b->n;
    int x=memcmp(a->p,b->p,n); if (x) return x;
    return (a->n>b->n)-(a->n<b->n);
}
static int sil_freq_cmp(const void *aa, const void *bb) {
    const SilEntry *a=aa,*b=bb;
    if(a->count!=b->count) return a->count>b->count?-1:1;
    return sil_entry_cmp(aa,bb);
}
static int sil_pattern_valid(const unsigned char *p, size_t n);
static void sil_free(SilTok *t) { free(t->entries); free(t->sorted); memset(t,0,sizeof(*t)); }
static int sil_parse(MooValue v, SilTok *t) {
    memset(t,0,sizeof(*t));
    if(v.tag!=MOO_STRING) return 0;
    MooString *s=MV_STR(v);
    if(s->length<(int32_t)(SIL_HEADER+32u) || (uint32_t)s->length>SIL_MAX_TEXT) return 0;
    const unsigned char *p=(const unsigned char*)s->chars; uint32_t n=(uint32_t)s->length;
    if(memcmp(p,SIL_MAGIC,8) || sil_u32(p+8)!=1 || sil_u32(p+20)!=0) return 0;
    unsigned char digest[32]; moo_krypto_sha256(p,n-32,digest);
    if(memcmp(digest,p+n-32,32)) return 0;
    uint32_t pn=sil_u32(p+12), count=sil_u32(p+16);
    if(!pn || pn>SIL_MAX_PAT || count>SIL_MAX_VOC-256 || pn>n-SIL_HEADER-32u) return 0;
    t->entries=calloc(count?count:1,sizeof(*t->entries)); if(!t->entries) return 0;
    t->blob=p; t->len=n; t->pat=p+SIL_HEADER; t->pn=pn; t->count=count;
    if(!sil_pattern_valid(t->pat,t->pn)) goto fail;
    size_t off=SIL_HEADER+pn;
    for(uint32_t i=0;i<count;i++) {
        if(off+4>n-32u) goto fail;
        uint32_t len=sil_u32(p+off); off+=4;
        if(!len || len>SIL_MAX_TOKEN || len>n-32u-off || !sil_text(p+off,len)) goto fail;
        t->entries[i].p=p+off; t->entries[i].n=len; off+=len;
        t->entries[i].count=i;
    }
    if(off!=n-32u) goto fail;
    t->sorted=malloc((count?count:1)*sizeof(*t->sorted)); if(!t->sorted) goto fail;
    memcpy(t->sorted,t->entries,count*sizeof(*t->sorted));
    qsort(t->sorted,count,sizeof(*t->sorted),sil_entry_cmp);
    for(uint32_t i=1;i<count;i++) if(!sil_entry_cmp(&t->sorted[i-1],&t->sorted[i])) goto fail;
    return 1;
fail: sil_free(t); return 0;
}

/* Trie weights implement TeX's max-overlap rule; digits annotate gaps. */
typedef struct { uint32_t cp, child, next; unsigned char w[33]; } SilNode;
typedef struct { SilNode *v; uint32_t n, cap; } SilTrie;
static uint32_t sil_node(SilTrie *tr, uint32_t parent, uint32_t cp) {
    uint32_t i=tr->v[parent].child;
    while(i && tr->v[i].cp!=cp) i=tr->v[i].next;
    if(i) return i;
    if(tr->n==tr->cap) {
        uint32_t cap=tr->cap?tr->cap*2:1024;
        if(cap>1000000u) return 0;
        SilNode *p=realloc(tr->v,(size_t)cap*sizeof(*p)); if(!p) return 0;
        tr->v=p; tr->cap=cap;
    }
    i=tr->n++; memset(&tr->v[i],0,sizeof(tr->v[i]));
    tr->v[i].cp=cp; tr->v[i].next=tr->v[parent].child; tr->v[parent].child=i;
    return i;
}
static int sil_trie(const unsigned char *p, size_t n, SilTrie *tr) {
    if(!sil_text(p,n)) return 0;
    memset(tr,0,sizeof(*tr)); tr->cap=1024; tr->v=calloc(tr->cap,sizeof(*tr->v)); if(!tr->v) return 0; tr->n=1;
    const char key[]="\\patterns{"; size_t i=0;
    for(;i+sizeof(key)-1<=n;i++) if(!memcmp(p+i,key,sizeof(key)-1)) break;
    if(i+sizeof(key)-1>n) goto fail;
    i+=sizeof(key)-1;
    unsigned patterns=0;
    while(i<n) {
        if(p[i]=='%') { while(i<n && p[i]!='\n') i++; continue; }
        if(p[i]=='}') { if(patterns) return 1; goto fail; }
        if(p[i]==' ' || p[i]=='\n' || p[i]=='\r' || p[i]=='\t') { i++; continue; }
        uint32_t node=0, depth=0; unsigned char w[33]={0};
        while(i<n && p[i]!='}' && p[i]!='%' && p[i]!=' ' && p[i]!='\n' && p[i]!='\r' && p[i]!='\t') {
            if(p[i]>='0' && p[i]<='9') { unsigned d=p[i++]-'0'; if(d>w[depth]) w[depth]=(unsigned char)d; continue; }
            uint32_t cp; int k=sil_cp(p+i,n-i,&cp);
            if(!k || !cp || depth==32 || (cp<128 && cp!='.' && (cp<'a' || cp>'z'))) goto fail;
            i+=(size_t)k; node=sil_node(tr,node,cp); if(!node) goto fail; depth++;
        }
        if(!depth) goto fail;
        for(uint32_t j=0;j<=depth;j++) if(w[j]>tr->v[node].w[j]) tr->v[node].w[j]=w[j];
        patterns++;
    }
fail: free(tr->v); memset(tr,0,sizeof(*tr)); return 0;
}
static int sil_pattern_valid(const unsigned char *p, size_t n) {
    SilTrie tr;
    if(!sil_trie(p,n,&tr)) return 0;
    free(tr.v);
    return 1;
}
static uint32_t sil_fold(uint32_t c) {
    if(c>='A'&&c<='Z') return c+32;
    if(c==0xc4 || c==0xd6 || c==0xdc) return c+32;
    return c;
}
static int sil_word(uint32_t c) {
    c=sil_fold(c);
    return (c>='a'&&c<='z') || c==0xe4 || c==0xf6 || c==0xfc || c==0xdf;
}
/* Non-ASCII or combining characters adjacent to a candidate make the whole
 * contiguous run opaque. This conservative subset avoids false grapheme cuts. */
static int sil_run(uint32_t c) { return sil_word(c) || c>=128; }
typedef int (*SilEmit)(void *, const unsigned char *, uint32_t, int);
static int sil_segments(const unsigned char *s, size_t n, const SilTrie *tr, SilEmit emit, void *ctx) {
    size_t i=0;
    while(i<n) {
        uint32_t c; int k=sil_cp(s+i,n-i,&c); if(!k) return 0;
        if(!sil_run(c)) { if(!emit(ctx,s+i,(uint32_t)k,0)) return 0; i+=(size_t)k; continue; }
        size_t start=i; uint32_t cps[SIL_MAX_WORD+2], offs[SIL_MAX_WORD+1], m=0; int safe=1;
        while(i<n) {
            int q=sil_cp(s+i,n-i,&c); if(!q || !sil_run(c)) break;
            if(!sil_word(c) || m>=SIL_MAX_WORD) safe=0;
            if(m<SIL_MAX_WORD) { offs[m]=(uint32_t)(i-start); cps[m+1]=sil_fold(c); m++; }
            i+=(size_t)q;
        }
        if(!safe || !m || i-start>SIL_MAX_TOKEN) {
            /* Byte fallback for unsafe/long spans, without linguistic claims. */
            for(size_t j=start;j<i;j++) if(!emit(ctx,s+j,1,2)) return 0;
            continue;
        }
        offs[m]=(uint32_t)(i-start); cps[0]='.'; cps[m+1]='.';
        unsigned char w[SIL_MAX_WORD+3]={0};
        for(uint32_t a=0;a<m+2;a++) {
            uint32_t node=0;
            for(uint32_t b=a;b<m+2 && b-a<32;b++) {
                uint32_t x=tr->v[node].child;
                while(x && tr->v[x].cp!=cps[b]) x=tr->v[x].next;
                if(!x) break;
                node=x;
                for(uint32_t z=0;z<=b-a+1;z++) if(tr->v[node].w[z]>w[a+z]) w[a+z]=tr->v[node].w[z];
            }
        }
        uint32_t prev=0;
        for(uint32_t cut=2;cut+2<=m;cut++) {
            if(w[cut+1]&1) { if(!emit(ctx,s+start+offs[prev],offs[cut]-offs[prev],1)) return 0; prev=cut; }
        }
        if(!emit(ctx,s+start+offs[prev],offs[m]-offs[prev],1)) return 0;
    }
    return 1;
}

typedef struct { SilEntry *v; size_t n, cap; } SilVec;
static int sil_collect(void *ctx, const unsigned char *p, uint32_t n, int kind) {
    (void)kind; SilVec *v=ctx; if(n<=1) return 1;
    if(v->n>=1000000u) return 0;
    if(v->n==v->cap) {
        size_t cap=v->cap?v->cap*2:1024;
        if(cap>1000000u) cap=1000000u;
        SilEntry *x=realloc(v->v,cap*sizeof(*x)); if(!x) return 0; v->v=x; v->cap=cap;
    }
    v->v[v->n++]=(SilEntry){p,n,1}; return 1;
}
typedef struct { const SilTok *t; int32_t *ids; size_t n, cap; } SilOut;
static int sil_append(SilOut *o, int32_t id) {
    if(o->n>=SIL_MAX_TEXT) return 0;
    if(o->n==o->cap) {
        size_t cap=o->cap?o->cap*2:1024; if(cap>SIL_MAX_TEXT) cap=SIL_MAX_TEXT;
        int32_t *p=realloc(o->ids,cap*sizeof(*p)); if(!p) return 0; o->ids=p; o->cap=cap;
    }
    o->ids[o->n++]=id; return 1;
}
static int sil_encode_piece(void *ctx,const unsigned char *p,uint32_t n,int kind) {
    (void)kind; SilOut *o=ctx;
    SilEntry key={p,n,0};
    SilEntry *found=bsearch(&key,o->t->sorted,o->t->count,sizeof(*o->t->sorted),sil_entry_cmp);
    if(found) return sil_append(o,(int32_t)(256+found->count));
    for(uint32_t i=0;i<n;i++) if(!sil_append(o,p[i])) return 0;
    return 1;
}
MooValue moo_silben_trainiere(MooValue corpus, MooValue limit, MooValue pattern) {
    if(corpus.tag!=MOO_STRING || pattern.tag!=MOO_STRING || limit.tag!=MOO_NUMBER)
        return sil_error("silben_trainiere: Text, Grenze und Muster erforderlich");
    double d=MV_NUM(limit);
    if(!isfinite(d) || d<256 || d>SIL_MAX_VOC || floor(d)!=d)
        return sil_error("silben_trainiere: Grenze muss ganzzahlig zwischen 256 und 65536 liegen");
    MooString *s=MV_STR(corpus), *p=MV_STR(pattern);
    if(s->length<=0 || (uint32_t)s->length>SIL_MAX_TEXT || !sil_text((unsigned char*)s->chars,(size_t)s->length))
        return sil_error("silben_trainiere: Text leer, zu gross, ungueltiges UTF-8 oder NUL");
    if(p->length<=0 || (uint32_t)p->length>SIL_MAX_PAT)
        return sil_error("silben_trainiere: Muster leer oder zu gross");
    SilTrie tr; if(!sil_trie((unsigned char*)p->chars,(size_t)p->length,&tr)) return sil_error("silben_trainiere: ungueltige Muster");
    SilVec v={0}; int ok=sil_segments((unsigned char*)s->chars,(size_t)s->length,&tr,sil_collect,&v);
    free(tr.v);
    if(!ok) { free(v.v); return sil_error("silben_trainiere: Ressourcenlimit"); }
    qsort(v.v,v.n,sizeof(*v.v),sil_entry_cmp);
    size_t unique=0;
    for(size_t i=0;i<v.n;i++) {
        if(unique && !sil_entry_cmp(&v.v[unique-1],&v.v[i])) v.v[unique-1].count++;
        else v.v[unique++]=v.v[i];
    }
    qsort(v.v,unique,sizeof(*v.v),sil_freq_cmp);
    size_t count=unique; if(count>(size_t)d-256) count=(size_t)d-256;
    size_t len=SIL_HEADER+(size_t)p->length+32u;
    for(size_t i=0;i<count;i++) {
        if(len>SIL_MAX_TEXT-4u-v.v[i].n) { free(v.v); return sil_error("silben_trainiere: Artefakt zu gross"); }
        len+=4u+v.v[i].n;
    }
    unsigned char *buf=malloc(len); if(!buf) { free(v.v); return sil_error("silben_trainiere: Speicher"); }
    memcpy(buf,SIL_MAGIC,8); sil_put32(buf+8,1); sil_put32(buf+12,(uint32_t)p->length);
    sil_put32(buf+16,(uint32_t)count); sil_put32(buf+20,0); memcpy(buf+SIL_HEADER,p->chars,(size_t)p->length);
    size_t off=SIL_HEADER+(size_t)p->length;
    for(size_t i=0;i<count;i++) { sil_put32(buf+off,v.v[i].n); off+=4; memcpy(buf+off,v.v[i].p,v.v[i].n); off+=v.v[i].n; }
    moo_krypto_sha256(buf,off,buf+off);
    MooValue ret=moo_string_new_len((char*)buf,(int32_t)len); free(buf); free(v.v); return ret;
}
MooValue moo_silben_kodiere(MooValue tok, MooValue text) {
    SilTok t; if(!sil_parse(tok,&t)) return sil_error("silben_kodiere: ungueltiges Artefakt");
    if(text.tag!=MOO_STRING || MV_STR(text)->length<=0 || (uint32_t)MV_STR(text)->length>SIL_MAX_TEXT ||
       !sil_text((unsigned char*)MV_STR(text)->chars,(size_t)MV_STR(text)->length)) {
        sil_free(&t); return sil_error("silben_kodiere: Text leer, zu gross, ungueltiges UTF-8 oder NUL");
    }
    SilTrie tr; if(!sil_trie(t.pat,t.pn,&tr)) { sil_free(&t); return sil_error("silben_kodiere: ungueltige Muster"); }
    SilOut o={0}; o.t=&t;
    int ok=sil_segments((unsigned char*)MV_STR(text)->chars,(size_t)MV_STR(text)->length,&tr,sil_encode_piece,&o);
    free(tr.v); sil_free(&t);
    if(!ok || !o.n) { free(o.ids); return sil_error("silben_kodiere: Ressourcenlimit"); }
    int32_t shape[1]={(int32_t)o.n}; MooTensor *v=moo_tensor_raw(1,shape);
    if(!v) { free(o.ids); return moo_none(); }
    for(size_t i=0;i<o.n;i++) v->data[i]=(float)o.ids[i];
    free(o.ids); MooValue ret; ret.tag=MOO_TENSOR; moo_val_set_ptr(&ret,v); return ret;
}


typedef struct { const unsigned char *base; uint32_t *ends; size_t n, cap; } SilBounds;
static int sil_bound_piece(void *ctx, const unsigned char *p, uint32_t n, int kind) {
    SilBounds *b=ctx;
    if(b->n>=100000u) return 0;
    if(b->n==b->cap) {
        size_t cap=b->cap?b->cap*2:256; if(cap>100000u) cap=100000u;
        uint32_t *x=realloc(b->ends,cap*sizeof(*x)); if(!x) return 0;
        b->ends=x; b->cap=cap;
    }
    b->ends[b->n++]=((uint32_t)(p-b->base)+n) | ((uint32_t)kind<<25); return 1;
}
MooValue moo_silben_grenzen(MooValue tok, MooValue text) {
    SilTok t; if(!sil_parse(tok,&t)) return sil_error("silben_grenzen: ungueltiges Artefakt");
    if(text.tag!=MOO_STRING || MV_STR(text)->length<=0 ||
       (uint32_t)MV_STR(text)->length>SIL_MAX_TEXT ||
       !sil_text((unsigned char*)MV_STR(text)->chars,(size_t)MV_STR(text)->length)) {
        sil_free(&t); return sil_error("silben_grenzen: Text leer, zu gross, ungueltiges UTF-8 oder NUL");
    }
    SilTrie tr;
    if(!sil_trie(t.pat,t.pn,&tr)) { sil_free(&t); return sil_error("silben_grenzen: ungueltige Muster"); }
    SilBounds b={0}; b.base=(unsigned char*)MV_STR(text)->chars;
    int ok=sil_segments(b.base,(size_t)MV_STR(text)->length,&tr,sil_bound_piece,&b);
    free(tr.v); sil_free(&t);
    if(!ok) { free(b.ends); return sil_error("silben_grenzen: zu viele Spannen oder Speicherfehler"); }
    MooValue out=moo_list_new((int32_t)b.n); uint32_t start=0;
    for(size_t i=0;i<b.n;i++) {
        uint32_t end=b.ends[i]&0x1ffffffu, kind=b.ends[i]>>25;
        MooValue pair=moo_list_new(3);
        moo_list_append(pair,moo_number((double)start));
        moo_list_append(pair,moo_number((double)end));
        moo_list_append(pair,moo_number((double)kind));
        moo_list_append(out,pair);
        start=end;
    }
    free(b.ends); return out;
}

static int sil_id(MooValue ids, size_t i, uint32_t max, uint32_t *out) {
    double x;
    if(ids.tag==MOO_TENSOR) x=(double)MV_TENSOR(ids)->data[i];
    else {
        MooValue v=MV_LIST(ids)->items[i]; if(v.tag!=MOO_NUMBER) return 0; x=MV_NUM(v);
    }
    if(!isfinite(x) || x<0 || x>=(double)max || floor(x)!=x) return 0;
    *out=(uint32_t)x; return 1;
}
MooValue moo_silben_dekodiere(MooValue tok, MooValue ids) {
    SilTok t; if(!sil_parse(tok,&t)) return sil_error("silben_dekodiere: ungueltiges Artefakt");
    size_t n;
    if(ids.tag==MOO_TENSOR) {
        MooTensor *v=MV_TENSOR(ids);
        if(v->ndim!=1 || v->size<0) { sil_free(&t); return sil_error("silben_dekodiere: Tensorform"); }
        moo_tensor_f32_sichern(v);
        if(moo_error_flag) { sil_free(&t); return moo_none(); }
        if(!(v->valid & MOO_V_DATA) || !v->data) { sil_free(&t); return sil_error("silben_dekodiere: Tensor nicht lesbar"); }
        n=(size_t)v->size;
    } else if(ids.tag==MOO_LIST) n=(size_t)MV_LIST(ids)->length;
    else { sil_free(&t); return sil_error("silben_dekodiere: IDs als Tensor oder Liste erforderlich"); }
    size_t len=0;
    for(size_t i=0;i<n;i++) {
        uint32_t id; if(!sil_id(ids,i,256+t.count,&id)) { sil_free(&t); return sil_error("silben_dekodiere: ungueltige ID"); }
        size_t k=id<256?1:t.entries[id-256].n;
        if(k>SIL_MAX_TEXT-len) { sil_free(&t); return sil_error("silben_dekodiere: Ausgabe zu gross"); } len+=k;
    }
    unsigned char *buf=malloc(len?len:1); if(!buf) { sil_free(&t); return sil_error("silben_dekodiere: Speicher"); }
    size_t off=0;
    for(size_t i=0;i<n;i++) {
        uint32_t id; (void)sil_id(ids,i,256+t.count,&id);
        if(id<256) buf[off++]=(unsigned char)id;
        else { SilEntry *e=&t.entries[id-256]; memcpy(buf+off,e->p,e->n); off+=e->n; }
    }
    MooValue ret=moo_string_new_len((char*)buf,(int32_t)len); free(buf); sil_free(&t); return ret;
}
MooValue moo_silben_hash(MooValue tok) {
    SilTok t; if(!sil_parse(tok,&t)) return sil_error("silben_hash: ungueltiges Artefakt");
    unsigned char h[32]; moo_krypto_sha256(t.blob,t.len,h); char hex[65];
    for(unsigned i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",h[i]);
    sil_free(&t); return moo_string_new(hex);
}
MooValue moo_silben_hash64(MooValue tok) {
    SilTok t; if(!sil_parse(tok,&t)) return sil_error("silben_hash64: ungueltiges Artefakt");
    uint64_t h=14695981039346656037ULL;
    for(uint32_t i=0;i<t.len;i++) { h^=t.blob[i]; h*=1099511628211ULL; }
    char hex[17]; snprintf(hex,sizeof(hex),"%016llx",(unsigned long long)h);
    sil_free(&t); return moo_string_new(hex);
}
MooValue moo_silben_info(MooValue tok) {
    SilTok t; if(!sil_parse(tok,&t)) return sil_error("silben_info: ungueltiges Artefakt");
    uint32_t n=t.count; unsigned char digest[32]; char hex[65], hex64[17];
    moo_krypto_sha256(t.blob,t.len,digest);
    for(unsigned i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",digest[i]);
    uint64_t h=14695981039346656037ULL;
    for(uint32_t i=0;i<t.len;i++) { h^=t.blob[i]; h*=1099511628211ULL; }
    snprintf(hex64,sizeof(hex64),"%016llx",(unsigned long long)h);
    sil_free(&t);
    MooValue d=moo_dict_new();
    moo_dict_set(d,moo_string_new("version"),moo_number(1));
    moo_dict_set(d,moo_string_new("vokab"),moo_number(256+n));
    moo_dict_set(d,moo_string_new("hash"),moo_string_new(hex));
    moo_dict_set(d,moo_string_new("hash64"),moo_string_new(hex64));
    return d;
}

static int sil_path(MooValue path) {
    return path.tag==MOO_STRING && MV_STR(path)->length>0 &&
           strlen(MV_STR(path)->chars)==(size_t)MV_STR(path)->length &&
           sil_text((unsigned char*)MV_STR(path)->chars,(size_t)MV_STR(path)->length);
}
#ifdef _WIN32
static wchar_t *sil_wide(const char *s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0);
    if(!n) return NULL;
    wchar_t *w=malloc((size_t)n*sizeof(*w)); if(!w) return NULL;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,w,n)) { free(w); return NULL; }
    return w;
}
#endif
MooValue moo_silben_speichern(MooValue tok, MooValue path) {
    SilTok t; if(!sil_parse(tok,&t)) return sil_error("silben_speichern: ungueltiges Artefakt");
    if(!sil_path(path)) { sil_free(&t); return sil_error("silben_speichern: ungueltiger Pfad"); }
    const char *name=MV_STR(path)->chars; int ok=0;
#ifdef _WIN32
    wchar_t *w=sil_wide(name); wchar_t temp[MAX_PATH], dir[MAX_PATH];
    FILE *f=NULL; int have=0;
    if(w && wcslen(w)<MAX_PATH-1) {
        wcscpy(dir,w); wchar_t *slash=wcsrchr(dir,L'\\'), *slash2=wcsrchr(dir,L'/');
        if(slash2 && (!slash || slash2>slash)) slash=slash2;
        if(slash) { if(slash==dir) slash[1]=0; else *slash=0; } else wcscpy(dir,L".");
        if(GetTempFileNameW(dir,L"sil",0,temp)) { have=1; f=_wfopen(temp,L"wb"); }
    }
    if(f) { ok=fwrite(t.blob,1,t.len,f)==t.len; if(fclose(f)!=0) ok=0; }
    if(ok) ok=MoveFileExW(temp,w,MOVEFILE_REPLACE_EXISTING)!=0;
    if(have && !ok) DeleteFileW(temp);
    free(w);
#else
    size_t n=strlen(name); char *tmp=malloc(n+12); FILE *f=NULL;
    if(tmp) {
        memcpy(tmp,name,n); memcpy(tmp+n,".tmp.XXXXXX",12);
        int fd=mkstemp(tmp);
        if(fd>=0) { f=fdopen(fd,"wb"); if(!f) { close(fd); unlink(tmp); } }
    }
    if(f) { ok=fwrite(t.blob,1,t.len,f)==t.len; if(fclose(f)!=0) ok=0; }
    if(ok) ok=rename(tmp,name)==0;
    if(tmp && !ok) unlink(tmp);
    free(tmp);
#endif
    sil_free(&t);
    if(!ok) return sil_error("silben_speichern: Schreiben oder Ersetzen fehlgeschlagen");
    return moo_bool(true);
}
MooValue moo_silben_laden(MooValue path) {
    if(!sil_path(path)) return sil_error("silben_laden: ungueltiger Pfad");
#ifdef _WIN32
    wchar_t *w=sil_wide(MV_STR(path)->chars); FILE *f=w?_wfopen(w,L"rb"):NULL; free(w);
#else
    FILE *f=fopen(MV_STR(path)->chars,"rb");
#endif
    if(!f) return sil_error("silben_laden: Datei nicht lesbar");
    if(fseek(f,0,SEEK_END)!=0) { fclose(f); return sil_error("silben_laden: Dateigroesse"); }
    long sz=ftell(f);
    if(sz<0 || (unsigned long)sz>SIL_MAX_TEXT || fseek(f,0,SEEK_SET)!=0) { fclose(f); return sil_error("silben_laden: Dateigroesse"); }
    unsigned char *b=malloc(sz?(size_t)sz:1);
    if(!b) { fclose(f); return sil_error("silben_laden: Speicher"); }
    size_t n=fread(b,1,(size_t)sz,f); int closed=fclose(f);
    if(n!=(size_t)sz || closed) { free(b); return sil_error("silben_laden: Lesefehler"); }
    MooValue ret=moo_string_new_len((char*)b,(int32_t)sz); free(b);
    SilTok t; if(!sil_parse(ret,&t)) { moo_release(ret); return sil_error("silben_laden: ungueltiges Artefakt"); }
    SilTrie tr;
    if(!sil_trie(t.pat,t.pn,&tr)) { sil_free(&t); moo_release(ret); return sil_error("silben_laden: ungueltige Muster"); }
    free(tr.v); sil_free(&t); return ret;
}

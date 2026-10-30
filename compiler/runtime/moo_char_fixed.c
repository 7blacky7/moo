#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "moo_char_fixed.h"
#include "moo_krypto.h"
#include <limits.h>
#include <wchar.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#define CF_BASE 256u
#define MOO_CHAR_FIXED_VOCAB_MAX 65536u
#define CF_TEXT_MAX (16u * 1024u * 1024u)
#define CF_HEAD 20u
#define CF_DIGEST 32u
#define CF_UNICODE 0x110000u

typedef struct { uint32_t cp, count; } CFEntry;
static MooValue cf_error(const char *s) { moo_throw(moo_error(s)); return moo_none(); }
static uint32_t cf_get(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void cf_put(unsigned char *p, uint32_t v) {
    p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8);
    p[2]=(unsigned char)(v>>16); p[3]=(unsigned char)(v>>24);
}
/* Strict UTF-8: excludes overlong encodings, surrogates, NUL and > U+10FFFF. */
static int cf_next(const unsigned char *p, size_t n, uint32_t *cp) {
    if (!n || !p[0]) return 0;
    unsigned char c=p[0]; unsigned len; uint32_t x;
    if(c<0x80) { *cp=c; return 1; }
    if(c>=0xc2 && c<=0xdf) { len=2; x=c&31u; }
    else if(c>=0xe0 && c<=0xef) { len=3; x=c&15u; }
    else if(c>=0xf0 && c<=0xf4) { len=4; x=c&7u; }
    else return 0;
    if(n<len) return 0;
    for(unsigned i=1;i<len;i++) { if((p[i]&0xc0u)!=0x80u) return 0; x=(x<<6)|(p[i]&63u); }
    if((len==2 && x<0x80) || (len==3 && x<0x800) ||
       (len==4 && x<0x10000) || x>0x10ffff || (x>=0xd800 && x<=0xdfff)) return 0;
    *cp=x; return (int)len;
}
static int cf_text(MooValue v, int empty_ok) {
    if(v.tag!=MOO_STRING || MV_STR(v)->length<0) return 0;
    size_t n=(size_t)MV_STR(v)->length;
    if(n>CF_TEXT_MAX || (!empty_ok && !n)) return 0;
    const unsigned char *p=(const unsigned char*)MV_STR(v)->chars;
    for(size_t i=0;i<n;) { uint32_t cp; int l=cf_next(p+i,n-i,&cp); if(!l)return 0; i+=(size_t)l; }
    return 1;
}
static int cf_parse(MooValue tok, uint32_t *v) {
    if(tok.tag!=MOO_STRING || MV_STR(tok)->length<0) return 0;
    size_t n=(size_t)MV_STR(tok)->length; const unsigned char *p=(const unsigned char*)MV_STR(tok)->chars;
    if(n<CF_HEAD+CF_DIGEST || memcmp(p,"MOOCF001",8) || cf_get(p+8)!=1 || cf_get(p+12)!=0) return 0;
    uint32_t count=cf_get(p+16);
    if(count<CF_BASE || count>MOO_CHAR_FIXED_VOCAB_MAX || n!=CF_HEAD+(size_t)(count-CF_BASE)*4+CF_DIGEST) return 0;
    unsigned char digest[32]; moo_krypto_sha256(p,n-CF_DIGEST,digest);
    if(memcmp(digest,p+n-CF_DIGEST,32)) return 0;
    unsigned char *seen=(unsigned char*)calloc(CF_UNICODE,1); if(!seen) return 0;
    int ok=1;
    for(uint32_t i=CF_BASE;i<count;i++) {
        uint32_t c=cf_get(p+CF_HEAD+4u*(i-CF_BASE));
        if(c<128 || c>=CF_UNICODE || (c>=0xd800 && c<=0xdfff) || seen[c]) { ok=0; break; }
        seen[c]=1;
    }
    free(seen); if(ok)*v=count; return ok;
}
static int cf_cmp(const void *a,const void *b) {
    const CFEntry *x=a,*y=b;
    if(x->count!=y->count) return x->count>y->count?-1:1;
    return x->cp<y->cp?-1:x->cp>y->cp?1:0;
}
MooValue moo_char_fixed_trainiere(MooValue text, MooValue limit) {
    if(!cf_text(text,0) || limit.tag!=MOO_NUMBER) return cf_error("char_fixed: Korpus oder Vokabulargrenze ungueltig");
    double d=MV_NUM(limit);
    if(!isfinite(d) || d<CF_BASE || d>MOO_CHAR_FIXED_VOCAB_MAX || floor(d)!=d) return cf_error("char_fixed: Grenze muss ganzzahlig 256..65536 sein");
    uint32_t *freq=calloc(CF_UNICODE,sizeof(*freq)); if(!freq)return cf_error("char_fixed: Speicher erschoepft");
    const unsigned char *p=(const unsigned char*)MV_STR(text)->chars; size_t n=(size_t)MV_STR(text)->length;
    for(size_t i=0;i<n;) {uint32_t cp; int l=cf_next(p+i,n-i,&cp); i+=(size_t)l; if(cp>=128)freq[cp]++;}
    uint32_t used=0; for(uint32_t i=128;i<CF_UNICODE;i++) if(freq[i])used++;
    CFEntry *entries=malloc((used?used:1)*sizeof(*entries));
    if(!entries){free(freq);return cf_error("char_fixed: Speicher erschoepft");}
    uint32_t j=0;for(uint32_t i=128;i<CF_UNICODE;i++)if(freq[i])entries[j++]=(CFEntry){i,freq[i]};
    free(freq);qsort(entries,used,sizeof(*entries),cf_cmp);
    uint32_t keep=used, cap=(uint32_t)d-CF_BASE; if(keep>cap)keep=cap;
    size_t len=CF_HEAD+(size_t)keep*4+CF_DIGEST; unsigned char *buf=calloc(len,1);
    if(!buf){free(entries);return cf_error("char_fixed: Speicher erschoepft");}
    memcpy(buf,"MOOCF001",8);cf_put(buf+8,1);cf_put(buf+16,CF_BASE+keep);
    for(uint32_t i=0;i<keep;i++)cf_put(buf+CF_HEAD+4u*i,entries[i].cp);
    free(entries);moo_krypto_sha256(buf,len-CF_DIGEST,buf+len-CF_DIGEST);
    MooValue result=moo_string_new_len((const char*)buf,(int32_t)len);free(buf);return result;
}
MooValue moo_char_fixed_kodiere(MooValue tok, MooValue text) {
    uint32_t v; if(!cf_parse(tok,&v) || !cf_text(text,0))return cf_error("char_fixed: Artefakt oder Text ungueltig");
    uint32_t *map=calloc(CF_UNICODE,sizeof(*map)); if(!map)return cf_error("char_fixed: Speicher erschoepft");
    const unsigned char *b=(const unsigned char*)MV_STR(tok)->chars;
    for(uint32_t i=CF_BASE;i<v;i++)map[cf_get(b+CF_HEAD+4u*(i-CF_BASE))]=i;
    const unsigned char *p=(const unsigned char*)MV_STR(text)->chars; size_t n=(size_t)MV_STR(text)->length;
    size_t count=0;for(size_t i=0;i<n;){uint32_t cp;int l=cf_next(p+i,n-i,&cp);count+=map[cp]?1:(size_t)l;i+=(size_t)l;}
    int32_t shape[1]={(int32_t)count};MooTensor *t=moo_tensor_raw(1,shape);
    if(!t){free(map);return moo_none();}
    size_t w=0;for(size_t i=0;i<n;){uint32_t cp;int l=cf_next(p+i,n-i,&cp);
        if(map[cp])t->data[w++]=(float)map[cp];else for(int x=0;x<l;x++)t->data[w++]=(float)p[i+(size_t)x];
        i+=(size_t)l;}
    free(map);MooValue result;result.tag=MOO_TENSOR;moo_val_set_ptr(&result,t);return result;
}
static int cf_id(MooValue ids, int64_t i, uint32_t vocab, uint32_t *out) {
    double d;
    if(ids.tag==MOO_TENSOR)d=(double)MV_TENSOR(ids)->data[i];
    else {MooValue x=MV_LIST(ids)->items[i];if(x.tag!=MOO_NUMBER)return 0;d=MV_NUM(x);}
    if(!isfinite(d)||d<0||d>=vocab||floor(d)!=d)return 0;
    *out=(uint32_t)d;
    return 1;
}
static unsigned cf_encode_cp(uint32_t cp, unsigned char *p) {
    if(cp<0x800){p[0]=0xc0u|(cp>>6);p[1]=0x80u|(cp&63u);return 2;}
    if(cp<0x10000){p[0]=0xe0u|(cp>>12);p[1]=0x80u|((cp>>6)&63u);p[2]=0x80u|(cp&63u);return 3;}
    p[0]=0xf0u|(cp>>18);p[1]=0x80u|((cp>>12)&63u);p[2]=0x80u|((cp>>6)&63u);p[3]=0x80u|(cp&63u);return 4;
}
MooValue moo_char_fixed_dekodiere(MooValue tok,MooValue ids) {
    uint32_t v;if(!cf_parse(tok,&v))return cf_error("char_fixed: Artefakt ungueltig");
    int64_t n;
    if(ids.tag==MOO_TENSOR) {
        MooTensor *t=MV_TENSOR(ids);
        if(!t) return cf_error("char_fixed: ungueltiger Tensor");
        moo_tensor_f32_sichern(t);
        if(!(t->valid & MOO_V_DATA) || !t->data) {
            if(moo_error_flag) return moo_none();
            return cf_error("char_fixed: Tensor hat keine gueltigen f32-Daten");
        }
        n=t->size;
    }
    else if(ids.tag==MOO_LIST)n=moo_list_iter_len(ids);else return cf_error("char_fixed: IDs muessen Liste oder Tensor sein");
    if(n<0 || (uint64_t)n>CF_TEXT_MAX)return cf_error("char_fixed: zu viele IDs");
    size_t cap=(size_t)n*4u;unsigned char *buf=malloc(cap?cap:1);if(!buf)return cf_error("char_fixed: Speicher erschoepft");
    const unsigned char *blob=(const unsigned char*)MV_STR(tok)->chars;size_t w=0;
    for(int64_t i=0;i<n;i++){uint32_t id;if(!cf_id(ids,i,v,&id)){free(buf);return cf_error("char_fixed: ungueltige ID");}
        if(id<CF_BASE)buf[w++]=(unsigned char)id;
        else w+=cf_encode_cp(cf_get(blob+CF_HEAD+4u*(id-CF_BASE)),buf+w);
        if(w>CF_TEXT_MAX){free(buf);return cf_error("char_fixed: Ausgabe zu lang");}}
    for(size_t i=0;i<w;){uint32_t cp;int l=cf_next(buf+i,w-i,&cp);if(!l){free(buf);return cf_error("char_fixed: IDs dekodieren nicht zu gueltigem UTF-8");}i+=(size_t)l;}
    MooValue result=moo_string_new_len((const char*)buf,(int32_t)w);free(buf);return result;
}
static MooValue cf_hash_valid(MooValue tok,int full) {
    const unsigned char *p=(const unsigned char*)MV_STR(tok)->chars;size_t n=(size_t)MV_STR(tok)->length;
    char hex[65];if(full){unsigned char digest[32];moo_krypto_sha256(p,n,digest);static const char h[]="0123456789abcdef";
        for(int i=0;i<32;i++){hex[2*i]=h[digest[i]>>4];hex[2*i+1]=h[digest[i]&15];}hex[64]=0;
    }else{uint64_t h=14695981039346656037ULL;for(size_t i=0;i<n;i++){h^=p[i];h*=1099511628211ULL;}
        snprintf(hex,sizeof(hex),"%016llx",(unsigned long long)h);}return moo_string_new(hex);
}
static MooValue cf_hash(MooValue tok,int full) {
    uint32_t v;
    if(!cf_parse(tok,&v)) return cf_error("char_fixed: Artefakt ungueltig");
    return cf_hash_valid(tok,full);
}
MooValue moo_char_fixed_hash(MooValue t){return cf_hash(t,1);}
MooValue moo_char_fixed_hash64(MooValue t){return cf_hash(t,0);}
MooValue moo_char_fixed_info(MooValue tok) {
    uint32_t v;if(!cf_parse(tok,&v))return cf_error("char_fixed: Artefakt ungueltig");
    MooValue d=moo_dict_new();moo_dict_set(d,moo_string_new("version"),moo_number(1));
    moo_dict_set(d,moo_string_new("vokab"),moo_number(v));
    moo_dict_set(d,moo_string_new("hash"),cf_hash_valid(tok,1));
    moo_dict_set(d,moo_string_new("hash64"),cf_hash_valid(tok,0));return d;
}
static int cf_path(MooValue p) {
    return p.tag==MOO_STRING && MV_STR(p)->length>0 &&
        (size_t)MV_STR(p)->length==strlen(MV_STR(p)->chars);
}
#ifdef _WIN32
static wchar_t *cf_wide(const char *s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0);if(!n)return NULL;
    wchar_t *w=malloc((size_t)n*sizeof(*w));if(!w)return NULL;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,w,n)){free(w);return NULL;}return w;
}
#endif
MooValue moo_char_fixed_speichern(MooValue tok,MooValue path) {
    uint32_t v;if(!cf_parse(tok,&v)||!cf_path(path))return cf_error("char_fixed: Artefakt oder Pfad ungueltig");
    const char *p=MV_STR(path)->chars;size_t n=(size_t)MV_STR(tok)->length;int ok=0;
#ifdef _WIN32
    wchar_t *dest=cf_wide(p);if(!dest)return cf_error("char_fixed: ungueltiger UTF-8-Pfad");
    size_t plen=wcslen(dest);wchar_t *tmp=malloc((plen+80)*sizeof(*tmp));
    if(!tmp){free(dest);return cf_error("char_fixed: Speicher erschoepft");}
    HANDLE h=INVALID_HANDLE_VALUE;
    for(unsigned i=0;i<128 && h==INVALID_HANDLE_VALUE;i++){
        swprintf(tmp,plen+80,L"%ls.tmp.%lu.%u",dest,(unsigned long)GetCurrentProcessId(),i);
        h=CreateFileW(tmp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        if(h==INVALID_HANDLE_VALUE && GetLastError()!=ERROR_FILE_EXISTS)break;
    }
    if(h!=INVALID_HANDLE_VALUE){DWORD wrote=0;BOOL wr=WriteFile(h,MV_STR(tok)->chars,(DWORD)n,&wrote,NULL);
        BOOL fl=FlushFileBuffers(h);BOOL cl=CloseHandle(h);
        if(wr && wrote==n && fl && cl)ok=MoveFileExW(tmp,dest,MOVEFILE_REPLACE_EXISTING)!=0;
        if(!ok)DeleteFileW(tmp);
    } free(tmp);free(dest);
#else
    size_t plen=strlen(p);if(plen>SIZE_MAX-16)return cf_error("char_fixed: Pfad zu lang");
    char *tmp=malloc(plen+16);if(!tmp)return cf_error("char_fixed: Speicher erschoepft");
    memcpy(tmp,p,plen);memcpy(tmp+plen,".tmp.XXXXXX",12);
    int fd=mkstemp(tmp);
    if(fd>=0){FILE *f=fdopen(fd,"wb");if(!f){close(fd);unlink(tmp);}else{
        size_t wr=fwrite(MV_STR(tok)->chars,1,n,f);int fl=fflush(f);int cl=fclose(f);
        if(wr==n && fl==0 && cl==0)ok=(rename(tmp,p)==0);
        if(!ok)unlink(tmp);}}
    free(tmp);
#endif
    if(!ok)return cf_error("char_fixed: atomare Speicherung fehlgeschlagen");
    return moo_bool(true);
}
MooValue moo_char_fixed_laden(MooValue path) {
    if(!cf_path(path))return cf_error("char_fixed: Pfad ungueltig");
#ifdef _WIN32
    wchar_t *p=cf_wide(MV_STR(path)->chars);if(!p)return cf_error("char_fixed: ungueltiger UTF-8-Pfad");
    FILE *f=_wfopen(p,L"rb");free(p);
#else
    FILE *f=fopen(MV_STR(path)->chars,"rb");
#endif
    if(!f)return cf_error("char_fixed: Datei nicht lesbar");
    if(fseek(f,0,SEEK_END)){fclose(f);return cf_error("char_fixed: Datei nicht seekbar");}
    long n=ftell(f);if(n<0 || (unsigned long)n>CF_HEAD+(MOO_CHAR_FIXED_VOCAB_MAX-CF_BASE)*4u+CF_DIGEST || fseek(f,0,SEEK_SET)){
        fclose(f);return cf_error("char_fixed: Datei zu gross");}
    unsigned char *buf=malloc(n?(size_t)n:1);if(!buf){fclose(f);return cf_error("char_fixed: Speicher erschoepft");}
    size_t got=fread(buf,1,(size_t)n,f);int cl=fclose(f);
    if(got!=(size_t)n || cl){free(buf);return cf_error("char_fixed: Datei unvollstaendig");}
    MooValue tok=moo_string_new_len((const char*)buf,(int32_t)n);free(buf);uint32_t v;
    if(!cf_parse(tok,&v)){moo_release(tok);return cf_error("char_fixed: Artefakt beschaedigt");}return tok;
}

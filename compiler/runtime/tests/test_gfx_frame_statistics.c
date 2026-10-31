/* GFX-TESTAPI-ROI: CPU-only numerical/validation harness.
 * Tiny owning value containers isolate the production frame kernel.
 * The separate Moo fixture exercises real runtime containers/bindings.
 */
#include "moo_runtime.h"
#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { size_t n; MooValue keys[16], values[16]; } TestDict;
typedef struct { size_t n; MooValue values[256]; } TestList;
static int raised;
static size_t checks;
static void check(int ok) { ++checks; assert(ok); }
MooValue moo_number(double n) { MooValue v; v.tag=MOO_NUMBER; moo_val_set_double(&v,n); return v; }
MooValue moo_bool(bool b) { MooValue v; v.tag=MOO_BOOL; v.data=b; return v; }
MooValue moo_none(void) { MooValue v; v.tag=MOO_NONE; v.data=0; return v; }
static MooValue pointer(uint64_t tag, void* p) { MooValue v; v.tag=tag; moo_val_set_ptr(&v,p); return v; }
MooValue moo_string_new(const char* s) {
    MooString* p=malloc(sizeof(*p)); assert(p);
    p->refcount=1; p->length=(int32_t)strlen(s); p->capacity=p->length+1;
    p->chars=malloc((size_t)p->capacity); assert(p->chars); strcpy(p->chars,s);
    return pointer(MOO_STRING,p);
}
MooValue moo_dict_new(void) {
    TestDict* p=calloc(1,sizeof(*p)); assert(p); return pointer(MOO_DICT,p);
}
MooValue moo_list_new(int32_t initial_capacity) {
    assert(initial_capacity >= 0 && initial_capacity <= 256);
    TestList* p=calloc(1,sizeof(*p)); assert(p); return pointer(MOO_LIST,p);
}
void moo_dict_set(MooValue d, MooValue key, MooValue value) {
    TestDict* p=moo_val_as_ptr(d); assert(p->n<16);
    p->keys[p->n]=key; p->values[p->n++]=value;
}
void moo_list_append(MooValue l, MooValue value) {
    TestList* p=moo_val_as_ptr(l); assert(p->n<256); p->values[p->n++]=value;
}
static void destroy(MooValue v) {
    if(v.tag==MOO_DICT) {
        TestDict* p=moo_val_as_ptr(v);
        for(size_t i=0;i<p->n;++i){ destroy(p->keys[i]); destroy(p->values[i]); }
        free(p);
    } else if(v.tag==MOO_LIST) {
        TestList* p=moo_val_as_ptr(v);
        for(size_t i=0;i<p->n;++i) destroy(p->values[i]);
        free(p);
    } else if(v.tag==MOO_STRING) { MooString* p=moo_val_as_ptr(v); free(p->chars); free(p); }
}
void moo_throw(MooValue error) { ++raised; destroy(error); } /* deliberately returns */
static MooValue get(MooValue d, const char* key) {
    TestDict* p=moo_val_as_ptr(d);
    for(size_t i=0;i<p->n;++i)
        if(!strcmp(((MooString*)moo_val_as_ptr(p->keys[i]))->chars,key)) return p->values[i];
    assert(!"missing key"); return moo_none();
}
static double number(MooValue d, const char* key) { return MV_NUM(get(d,key)); }
static void close_to(double a,double b){ check(fabs(a-b)<=1e-9*fmax(1.0,fabs(b))); }
static MooValue measure(MooFrame* f,int x,int y,int w,int h) {
    raised=0;
    MooValue v=moo_test_frame_statistics(pointer(MOO_FRAME,f),moo_number(x),moo_number(y),moo_number(w),moo_number(h));
    check(raised==0 && v.tag==MOO_DICT); return v;
}
static void reference(MooFrame* f,int x,int y,int w,int h) {
    MooValue out=measure(f,x,y,w,h);
    const char* names[]={"rot","gruen","blau","alpha"};
    int bright=0,dark=0;
    for(int row=y;row<y+h;++row) for(int col=x;col<x+w;++col){
        const uint8_t* p=f->pixels+row*f->stride+col*4;
        bright+=(p[0]==255||p[1]==255||p[2]==255);
        dark+=(p[0]<=5&&p[1]<=5&&p[2]<=5);
    }
    close_to(number(out,"pixelzahl"),w*h);
    close_to(number(out,"any_rgb255"),bright);
    close_to(number(out,"all_rgb_le5"),dark);
    for(int c=0;c<4;++c){
        double sum=0,variance=0; int lo=255,hi=0,bins[256]={0};
        for(int row=y;row<y+h;++row) for(int col=x;col<x+w;++col){
            int val=f->pixels[row*f->stride+col*4+c];
            sum+=val; ++bins[val]; if(val<lo)lo=val; if(val>hi)hi=val;
        }
        double mean=sum/(w*h);
        for(int row=y;row<y+h;++row) for(int col=x;col<x+w;++col){
            double d=f->pixels[row*f->stride+col*4+c]-mean; variance+=d*d;
        }
        variance/=w*h;
        MooValue ch=get(out,names[c]);
        close_to(number(ch,"min"),lo); close_to(number(ch,"max"),hi);
        close_to(number(ch,"mittelwert"),mean); close_to(number(ch,"varianz"),variance);
        TestList* hist=moo_val_as_ptr(get(ch,"histogramm")); check(hist->n==256);
        double total=0,weighted=0;
        for(int k=0;k<256;++k){ close_to(MV_NUM(hist->values[k]),bins[k]); total+=MV_NUM(hist->values[k]); weighted+=k*MV_NUM(hist->values[k]); }
        close_to(total,w*h); close_to(weighted,sum);
    }
    destroy(out);
}
static void invalid(MooValue f,MooValue x,MooValue y,MooValue w,MooValue h){
    raised=0; MooValue r=moo_test_frame_statistics(f,x,y,w,h);
    check(raised==1 && r.tag==MOO_NONE);
}
int main(int argc,char** argv){
    uint8_t data[4*4]={0,0,0,0, 0,255,1,64, 255,0,2,128, 255,255,255,255};
    MooFrame f={1,2,2,MOO_FRAME_FMT_RGBA8,8,data};
    MooValue out=measure(&f,0,0,2,2);
    MooValue red=get(out,"rot");
    close_to(number(red,"mittelwert"),127.5); close_to(number(red,"varianz"),16256.25);
    close_to(number(out,"any_rgb255"),3); close_to(number(out,"all_rgb_le5"),1);
    destroy(out);
    uint8_t padded[3*24], before[3*24];
    memset(padded,0xcd,sizeof padded);
    for(int y=0;y<3;++y)for(int x=0;x<5;++x)for(int c=0;c<4;++c)
        padded[y*24+x*4+c]=(uint8_t)((x*67+y*91+c*31)%256);
    padded[0]=255; padded[4]=0;padded[5]=1;padded[6]=5;
    memcpy(before,padded,sizeof padded);
    MooFrame wide={1,5,3,MOO_FRAME_FMT_RGBA8,24,padded};
    for(int y=0;y<3;++y)for(int x=0;x<5;++x)
        for(int h=1;h<=3-y;++h)for(int w=1;w<=5-x;++w) reference(&wide,x,y,w,h);
    reference(&wide,0,0,5,3); /* repeated, independent state */
    check(!memcmp(before,padded,sizeof padded));
    reference(&f,0,0,2,2);
    MooValue vf=pointer(MOO_FRAME,&f),zero=moo_number(0),one=moo_number(1);
    double bad[]={NAN,INFINITY,-INFINITY,-1,0.5,DBL_MAX,2147483648.0};
    for(size_t k=0;k<sizeof bad/sizeof bad[0];++k) for(int i=0;i<4;++i){
        MooValue a[4]={zero,zero,one,one}; a[i]=moo_number(bad[k]);
        invalid(vf,a[0],a[1],a[2],a[3]);
    }
    invalid(vf,zero,zero,zero,one); invalid(vf,zero,zero,one,zero);
    invalid(vf,one,one,moo_number(2),one);
    invalid(vf,moo_bool(false),zero,one,one);
    invalid(moo_none(),zero,zero,one,one);
    invalid(pointer(MOO_FRAME,NULL),zero,zero,one,one);
    MooFrame broken=f; broken.stride=1;
    invalid(pointer(MOO_FRAME,&broken),zero,zero,one,one);
    broken=f; broken.format=-1;
    invalid(pointer(MOO_FRAME,&broken),zero,zero,one,one);
    broken=f; broken.pixels=NULL;
    invalid(pointer(MOO_FRAME,&broken),zero,zero,one,one);
    broken=f; broken.width=536870911; broken.height=INT_MAX; broken.stride=2147483644;
    invalid(pointer(MOO_FRAME,&broken),zero,zero,moo_number(broken.width),moo_number(broken.height));
    if(argc==2){
        MooValue path=moo_string_new(argv[1]);
        check(MV_BOOL(moo_test_frame_save_bmp(vf,path)));
        destroy(path);
    }
    printf("GFX-ROI C PASS %zu checks\n",checks);
    return 0;
}

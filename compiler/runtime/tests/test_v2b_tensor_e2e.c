#include "../moo_runtime.h"
#include "../moo_ki_gpu_api.h"
#include <stdio.h>
#include <math.h>
int moo_error_flag=0; MooValue moo_last_error; int moo_try_depth=0; jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue e){moo_error_flag=1;if(e.tag==MOO_ERROR)free(moo_val_as_ptr(e));}
#define STUB(n) void n(void*p){(void)p;}
STUB(moo_socket_free) STUB(moo_thread_free) STUB(moo_channel_free) STUB(moo_db_free)
STUB(moo_db_stmt_free) STUB(moo_window_free) STUB(moo_web_free) STUB(moo_voxel_free)
STUB(moo_frame_free) STUB(moo_gif_handle_free) STUB(moo_video_handle_free)
STUB(moo_kamera_free) STUB(moo_mikro_free)
static int f=0;
#define CK(x,m) do{if(x)printf(" OK  %s\n",m);else{printf(" FAIL %s\n",m);f++;}}while(0)
static MooValue tv(int nd,const int32_t*s,const float*d){MooTensor*t=moo_tensor_raw(nd,s);for(int64_t i=0;i<t->size;i++)t->data[i]=d[i];MooValue v;v.tag=MOO_TENSOR;moo_val_set_ptr(&v,t);return v;}
static int nearv(MooValue v,const float*e,int n){MooTensor*t=MV_TENSOR(v);moo_tensor_f32_sichern(t);for(int i=0;i<n;i++)if(fabsf(t->data[i]-e[i])>1e-4f)return 0;return 1;}
int main(void){
 void*p=moo_ki_gpu_buf_belegen(4);if(!p){puts("SKIP: keine GPU");return 77;}moo_ki_gpu_buf_freigeben(p);
 moo_ki_gpu_strikt_setzen(true);
 int32_t sx[4]={1,3,3,1},sw[2]={4,1};float xd[9]={1,2,3,4,5,6,7,8,9},wd[4]={1,1,1,1};
 MooValue x=tv(4,sx,xd),w=tv(2,sw,wd);moo_tensor_mit_gradient(x);moo_tensor_mit_gradient(w);
 uint32_t cp=2u|(2u<<8)|(1u<<16);moo_ki_gpu_telemetrie_reset();
 MooValue col=moo_tensor_im2col(x,moo_number(cp));MooValue y=moo_tensor_matmul(col,w);
 MooValue loss=moo_tensor_summe(y,moo_number(-1));moo_tensor_rueckwaerts(loss);
 MooValue gx=moo_tensor_gradient(x),gw=moo_tensor_gradient(w);
 float ex[9]={1,2,1,2,4,2,1,2,1},ew[4]={12,16,24,28};
 CK(!moo_error_flag&&nearv(gx,ex,9)&&nearv(gw,ew,4),"Conv-Kette STRIKT F+B Gradienten");
 MooKiGpuTelemetrie t;moo_ki_gpu_telemetrie(&t);CK(t.cpu_fallbacks==0,"Conv-Kette ohne CPU-Fallback");
 moo_release(gx);moo_release(gw);moo_release(loss);moo_release(y);moo_release(col);moo_ag_reset();moo_release(w);moo_release(x);
 float px[9]={1,2,3,4,5,6,7,8,9};x=tv(4,sx,px);moo_tensor_mit_gradient(x);
 uint32_t pp=1u|(2u<<8)|(1u<<16);moo_ki_gpu_telemetrie_reset();
 MooValue po=moo_tensor_pool(x,moo_number(pp));loss=moo_tensor_summe(po,moo_number(-1));moo_tensor_rueckwaerts(loss);
 gx=moo_tensor_gradient(x);float ep[9]={.25,.5,.25,.5,1,.5,.25,.5,.25};
 CK(!moo_error_flag&&nearv(gx,ep,9),"Pooling STRIKT F+B Gradient");moo_ki_gpu_telemetrie(&t);CK(t.cpu_fallbacks==0,"Pooling ohne CPU-Fallback");
 moo_release(gx);moo_release(loss);moo_release(po);moo_ag_reset();moo_release(x);
 printf("%s V2b Tensor-E2E (%d)\n",f?"FAIL":"PASS",f);return f?1:0;
}

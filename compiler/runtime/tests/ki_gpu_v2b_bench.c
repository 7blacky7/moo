#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include "moo_ki_gpu_api.h"
static double ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
static void cpu_im2col(const float*x,float*o,int b,int h,int w,int c,int k){
 int oh=h-k+1,ow=w-k+1,kc=k*k*c;
 for(int bi=0;bi<b;bi++)for(int oy=0;oy<oh;oy++)for(int ox=0;ox<ow;ox++){
  float*r=o+((bi*oh+oy)*ow+ox)*kc;
  for(int ky=0;ky<k;ky++)for(int kx=0;kx<k;kx++)for(int ch=0;ch<c;ch++)
   r[(ky*k+kx)*c+ch]=x[((bi*h+oy+ky)*w+ox+kx)*c+ch];
 }}
static int run(int b){
 const int h=28,w=28,c=16,k=3,co=32,it=20,oh=26,ow=26,kc=144;
 int64_t nx=(int64_t)b*h*w*c,ncol=(int64_t)b*oh*ow*kc,no=(int64_t)b*oh*ow*co,nw=(int64_t)kc*co;
 float*x=malloc(nx*4),*col=malloc(ncol*4),*wei=malloc(nw*4),*out=malloc(no*4);
 for(int64_t i=0;i<nx;i++)x[i]=(float)(i%97)/97;for(int64_t i=0;i<nw;i++)wei[i]=(float)(i%31)/310;
 void *hx=moo_ki_gpu_buf_belegen(nx*4),*hc=moo_ki_gpu_buf_belegen(ncol*4),*hw=moo_ki_gpu_buf_belegen(nw*4),*ho=moo_ki_gpu_buf_belegen(no*4);
 if(!hx||!hc||!hw||!ho)return 77;moo_ki_gpu_upload(hx,x,nx*4);moo_ki_gpu_upload(hw,wei,nw*4);
 for(int q=0;q<3;q++){moo_ki_gpu_im2col_res(hx,hc,b,h,w,c,k,k,1,0);moo_ki_gpu_matmul_res(hc,hw,ho,b*oh*ow,kc,co);}
 moo_ki_gpu_telemetrie_reset();double a=ms();
 for(int q=0;q<it;q++){cpu_im2col(x,col,b,h,w,c,k);moo_ki_gpu_upload(hc,col,ncol*4);moo_ki_gpu_matmul_res(hc,hw,ho,b*oh*ow,kc,co);moo_ki_gpu_download(ho,out,no*4);}
 double old=(ms()-a)/it;MooKiGpuTelemetrie to;moo_ki_gpu_telemetrie(&to);
 moo_ki_gpu_telemetrie_reset();a=ms();
 for(int q=0;q<it;q++){moo_ki_gpu_im2col_res(hx,hc,b,h,w,c,k,k,1,0);moo_ki_gpu_matmul_res(hc,hw,ho,b*oh*ow,kc,co);}
 double neu=(ms()-a)/it;MooKiGpuTelemetrie tn;moo_ki_gpu_telemetrie(&tn);
 printf("b=%d old=%.3fms neu=%.3fms speedup=%.2fx old_u/d=%llu/%llu neu_u/d=%llu/%llu\n",b,old,neu,old/neu,
 (unsigned long long)to.uploads,(unsigned long long)to.downloads,(unsigned long long)tn.uploads,(unsigned long long)tn.downloads);
 int ok=tn.uploads==0&&tn.downloads==0&&tn.cpu_fallbacks==0;
 void*hs[]={hx,hc,hw,ho};for(int i=0;i<4;i++)moo_ki_gpu_buf_freigeben(hs[i]);free(x);free(col);free(wei);free(out);return ok?0:1;
}
int main(void){int bs[]={1,8,32};for(int i=0;i<3;i++){int r=run(bs[i]);if(r)return r;}return 0;}

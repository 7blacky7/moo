/**
 * KI-MULTI-V2b Hardware-Gate: residente im2col/col2im + Pooling F/B.
 * Kleine exakte Matrix, Adjungiertheits-/Gradientenbeweis und Telemetrie.
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include "moo_ki_gpu_api.h"
static int fail=0;
#define CK(x,m) do{if(x)printf(" OK  %s\n",m);else{printf(" FAIL %s\n",m);fail++;}}while(0)
static int nahe(const float*a,const float*b,int n){for(int i=0;i<n;i++)if(fabsf(a[i]-b[i])>1e-5f)return 0;return 1;}
int main(void){
 void* probe=moo_ki_gpu_buf_belegen(4);if(!probe){puts("SKIP: keine Vulkan-GPU — V2b nicht bewiesen");return 77;}moo_ki_gpu_buf_freigeben(probe);
 float x[9]={1,2,3,4,5,6,7,8,9},ones16[16],z9[9]={0};for(int i=0;i<16;i++)ones16[i]=1;
 float expcol[16]={1,2,4,5,2,3,5,6,4,5,7,8,5,6,8,9};
 float expcnt[9]={1,2,1,2,4,2,1,2,1},expmax[4]={5,6,8,9},expavg[4]={3,4,6,7};
 float expmaxbw[9]={0,0,0,0,1,1,0,1,1},expavgbw[9]={.25,.5,.25,.5,1,.5,.25,.5,.25};
 void *hx=moo_ki_gpu_buf_belegen(36),*hcol=moo_ki_gpu_buf_belegen(64),*hgcol=moo_ki_gpu_buf_belegen(64);
 void *hdx=moo_ki_gpu_buf_belegen(36),*hpmax=moo_ki_gpu_buf_belegen(16),*hpavg=moo_ki_gpu_buf_belegen(16);
 void *hgout=moo_ki_gpu_buf_belegen(16),*hdxm=moo_ki_gpu_buf_belegen(36),*hdxa=moo_ki_gpu_buf_belegen(36);
 CK(hx&&hcol&&hgcol&&hdx&&hpmax&&hpavg&&hgout&&hdxm&&hdxa,"Buffer belegt");
 CK(moo_ki_gpu_upload(hx,x,36)&&moo_ki_gpu_upload(hgcol,ones16,64)&&
    moo_ki_gpu_upload(hgout,ones16,16)&&moo_ki_gpu_upload(hdx,z9,36),"Rand-Uploads");
 moo_ki_gpu_telemetrie_reset();
 CK(moo_ki_gpu_im2col_res(hx,hcol,1,3,3,1,2,2,1,0),"im2col resident");
 CK(moo_ki_gpu_col2im_res(hgcol,hdx,1,3,3,1,2,2,1,0),"col2im resident");
 CK(moo_ki_gpu_pool_res(0,hx,hpmax,1,3,3,1,2,1),"maxpool forward resident");
 CK(moo_ki_gpu_pool_res(1,hx,hpavg,1,3,3,1,2,1),"meanpool forward resident");
 CK(moo_ki_gpu_pool_bw_res(0,hx,hgout,hdxm,1,3,3,1,2,1),"maxpool backward resident");
 CK(moo_ki_gpu_pool_bw_res(1,hx,hgout,hdxa,1,3,3,1,2,1),"meanpool backward resident");
 MooKiGpuTelemetrie t;moo_ki_gpu_telemetrie(&t);
 CK(t.submits==6&&t.uploads==0&&t.downloads==0&&t.cpu_fallbacks==0,"6 Submits, null Transfers/Fallbacks im Kern");
 float col[16],dx[9],pm[4],pa[4],dxm[9],dxa[9];
 CK(moo_ki_gpu_download(hcol,col,64)&&moo_ki_gpu_download(hdx,dx,36)&&
    moo_ki_gpu_download(hpmax,pm,16)&&moo_ki_gpu_download(hpavg,pa,16)&&
    moo_ki_gpu_download(hdxm,dxm,36)&&moo_ki_gpu_download(hdxa,dxa,36),"Rand-Downloads");
 CK(nahe(col,expcol,16),"im2col CPU-Referenz");
 CK(nahe(dx,expcnt,9),"col2im Adjungierter/FD-Gradient");
 CK(nahe(pm,expmax,4)&&nahe(pa,expavg,4),"Pooling Forward CPU-Referenz");
 CK(nahe(dxm,expmaxbw,9)&&nahe(dxa,expavgbw,9),"Pooling Backward FD-Gradient");
 void* hs[]={hx,hcol,hgcol,hdx,hpmax,hpavg,hgout,hdxm,hdxa};for(int i=0;i<9;i++)moo_ki_gpu_buf_freigeben(hs[i]);
 printf("%s V2b GPU-Gate (%d Fehler)\n",fail?"FAIL":"PASS",fail);return fail?1:0;
}

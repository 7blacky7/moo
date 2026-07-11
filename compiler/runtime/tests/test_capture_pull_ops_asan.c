#include "../moo_capture_pull_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int moo_error_flag=0; MooValue moo_last_error; int moo_try_depth=0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue e){moo_error_flag=1;if(e.tag==MOO_ERROR)moo_release(e);}
#define STUB(n) void n(void*p){(void)p;}
STUB(moo_socket_free) STUB(moo_thread_free) STUB(moo_channel_free) STUB(moo_db_free)
STUB(moo_db_stmt_free) STUB(moo_window_free) STUB(moo_web_free) STUB(moo_voxel_free)
STUB(moo_gif_handle_free) STUB(moo_video_handle_free)

static int fails=0,startups=0,shutdowns=0,cam_closes=0,mic_closes=0,releases=0;
#define CK(x,m) do{if(x)printf(" OK  %s\n",m);else{printf(" FAIL %s\n",m);fails++;}}while(0)
static int64_t now_ms=1000;
static MooPullResult wait_result=MOO_PULL_OK;
static int cam_step=0,total_devices=2,audio_step=0,recover_count=0;
static int64_t clock_ms(void){return now_ms++;}
static bool startup(char*e,size_t c){(void)e;(void)c;startups++;return true;}
static void shutdown_(void){shutdowns++;}
static MooPullResult enumerate(MooPullCameraInfo*out,int32_t cap,int32_t*total,char*e,size_t ec){
 (void)e;(void)ec;*total=total_devices;
 for(int i=0;i<total_devices&&i<cap;i++){snprintf(out[i].id,sizeof out[i].id,"cam%d",i);snprintf(out[i].name,sizeof out[i].name,"Kamera %d",i);}
 return MOO_PULL_OK;
}
static MooPullResult cam_open(const char*id,int32_t w,int32_t h,double fps,bool exact,
 void**s,int32_t*aw,int32_t*ah,double*af,int32_t*b,char*e,size_t ec){
 (void)id;(void)exact;(void)e;(void)ec;*s=(void*)0x11;*aw=w;*ah=h;*af=fps;*b=2;cam_step=0;return MOO_PULL_OK;
}
static MooPullResult cam_wait(void*s,int32_t t,char*e,size_t ec){(void)s;(void)t;(void)e;(void)ec;return wait_result;}
static MooPullResult cam_next(void*s,MooPullFramePacket*p,char*e,size_t ec){
 (void)s;(void)e;(void)ec;if(cam_step>=2)return MOO_PULL_EMPTY;
 p->width=2;p->height=1;p->stride=8;p->bytes=8;p->bgra=malloc(8);
 uint8_t v=(uint8_t)(10+cam_step*20);uint8_t q[8]={v,2,3,255,4,5,6,255};memcpy(p->bgra,q,8);cam_step++;return MOO_PULL_OK;
}
static void cam_release(MooPullFramePacket*p){releases++;free(p->bgra);memset(p,0,sizeof(*p));}
static void cam_close(void*s){(void)s;cam_closes++;}
static MooPullResult mic_open(const char*id,int32_t r,int32_t c,void**s,int32_t*ar,int32_t*ac,
 int32_t*p,int32_t*b,char*e,size_t ec){(void)id;(void)e;(void)ec;*s=(void*)0x22;*ar=r;*ac=c;*p=2;*b=8;audio_step=0;return MOO_PULL_OK;}
static MooPullResult mic_wait(void*s,int32_t t,char*e,size_t ec){
 (void)s;(void)t;(void)e;(void)ec;if(wait_result==MOO_PULL_RECOVERABLE&&recover_count==0)return MOO_PULL_RECOVERABLE;return wait_result==MOO_PULL_RECOVERABLE?MOO_PULL_OK:wait_result;
}
static const float a0[]={1,3,2,4,5,7,6,8};
static MooPullResult mic_next(void*s,MooPullAudioPacket*p,char*e,size_t ec){
 (void)s;(void)e;(void)ec;if(audio_step++)return MOO_PULL_EMPTY;
 p->samples=a0;p->frames=4;p->channels=2;p->token=(void*)a0;return MOO_PULL_OK;
}
static void mic_release(MooPullAudioPacket*p){releases++;p->token=NULL;}
static MooPullResult recover(void*s,char*e,size_t ec){(void)s;(void)e;(void)ec;recover_count++;return MOO_PULL_OK;}
static void mic_close(void*s){(void)s;mic_closes++;}
static const MooCapturePullOps fake={clock_ms,startup,shutdown_,enumerate,cam_open,cam_wait,cam_next,
 cam_release,cam_close,mic_open,mic_wait,mic_next,mic_release,recover,mic_close};
const MooCapturePullOps* moo_capture_pull_system_ops(void){return &fake;}
static void reset(void){moo_error_flag=0;wait_result=MOO_PULL_OK;now_ms=1000;releases=0;recover_count=0;}

int main(void){
 moo_capture_pull_set_ops_for_tests(&fake);reset();
 MooValue list=moo_capture_camera_list_native();CK(list.tag==MOO_LIST&&MV_LIST(list)->length==2,"Kameraliste und IDs");moo_release(list);
 total_devices=1025;list=moo_capture_camera_list_native();CK(moo_error_flag&&list.tag==MOO_NONE,"Kandidatenlimit >1024 fail-loud");total_devices=2;reset();
 MooKamera c={.refcount=1,.state=MOO_CAPTURE_OPEN};CK(moo_capture_camera_open_native(&c,NULL,2,1,30,true)&&c.state==MOO_CAPTURE_STREAMING,"Kamera open/verhandelt");
 MooValue fr=moo_capture_camera_frame_native(&c,50);CK(fr.tag==MOO_FRAME,"Latest-Frame geliefert");
 MooFrame*f=MV_FRAME(fr);CK(f->pixels[0]==3&&f->pixels[1]==2&&f->pixels[2]==30&&releases==2,"Latest + BGRA->RGBA + alle Pakete released");moo_release(fr);
 wait_result=MOO_PULL_TIMEOUT;MooValue no=moo_capture_camera_frame_native(&c,0);CK(moo_error_flag&&no.tag==MOO_NONE,"timeout=0 definiert");moo_capture_camera_close_native(&c);CK(cam_closes>0&&c.backend==NULL,"Kamera Cleanup idempotent");
 reset();MooMikro m={.refcount=1,.state=MOO_CAPTURE_OPEN};CK(moo_capture_microphone_open_native(&m,"default",48000,2),"Mikrofon open");
 MooValue ar=moo_capture_microphone_read_native(&m,3,50);CK(ar.tag==MOO_DICT,"Audio exact-block Dict");
 MooValue dv=moo_dict_get(ar,moo_string_new("daten"));MooTensor*t=MV_TENSOR(dv);
 CK(t->size==3&&fabsf(t->data[0]-2)<1e-6&&fabsf(t->data[1]-3)<1e-6&&fabsf(t->data[2]-6)<1e-6,"Stereo-Downmix + Teildaten");
 moo_release(dv);moo_release(ar);
 ar=moo_capture_microphone_read_native(&m,1,50);dv=moo_dict_get(ar,moo_string_new("daten"));t=MV_TENSOR(dv);
 CK(fabsf(t->data[0]-7)<1e-6,"Paket-Rest wird verlustfrei weitergereicht");moo_release(dv);moo_release(ar);
 wait_result=MOO_PULL_RECOVERABLE;audio_step=0;ar=moo_capture_microphone_read_native(&m,1,50);
 CK(ar.tag==MOO_DICT&&recover_count==1,"Recovery begrenzt und danach erfolgreich");moo_release(ar);
 moo_capture_microphone_close_native(&m);CK(mic_closes>0&&m.backend==NULL,"Mikro Cleanup");
 CK(startups==shutdowns,"Startup/Shutdown bilanziert");
 printf("%s Capture-Pull Fault-Matrix (%d)\n",fails?"FAIL":"PASS",fails);return fails?1:0;
}

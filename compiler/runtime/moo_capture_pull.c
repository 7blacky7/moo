#include "moo_capture_pull_internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void* session;
    bool started;
    int32_t queue_bound;
} PullCamera;

typedef struct {
    void* session;
    bool started;
    float* spill;
    int32_t spill_frames;
    int32_t spill_pos;
    int32_t spill_channels;
} PullMicrophone;

static const MooCapturePullOps* injected_ops = NULL;
static const MooCapturePullOps* pull_ops(void) {
    return injected_ops ? injected_ops : moo_capture_pull_system_ops();
}
void moo_capture_pull_set_ops_for_tests(const MooCapturePullOps* ops) { injected_ops = ops; }
void moo_capture_pull_reset_ops_for_tests(void) { injected_ops = NULL; }

static MooValue camera_fail(const char* message) {
    moo_throw(moo_error(message));
    return moo_none();
}
static bool open_error(char* dst, size_t cap, const char* fallback, const char* detail) {
    snprintf(dst, cap, "%s%s%s", fallback, detail && detail[0] ? ": " : "",
             detail && detail[0] ? detail : "");
    return false;
}
static int remaining_ms(int64_t deadline) {
    int64_t now = pull_ops()->monotonic_ms();
    if (now < 0 || now >= deadline) return 0;
    int64_t left = deadline - now;
    return left > INT32_MAX ? INT32_MAX : (int)left;
}

MooValue moo_capture_camera_list_native(void) {
    char error[256] = {0};
    if (!pull_ops()->startup(error, sizeof error))
        return camera_fail(error[0] ? error : "kamera_liste: natives Capture konnte nicht starten");
    MooPullCameraInfo* infos = (MooPullCameraInfo*)calloc(MOO_PULL_CAPTURE_MAX_DEVICES,
                                                        sizeof(MooPullCameraInfo));
    if (!infos) { pull_ops()->shutdown(); return camera_fail("kamera_liste: Speicher voll"); }
    int32_t total = 0;
    MooPullResult r = pull_ops()->camera_enumerate(
        infos, MOO_PULL_CAPTURE_MAX_DEVICES, &total, error, sizeof error);
    if (r != MOO_PULL_OK || total < 0 || total > MOO_PULL_CAPTURE_MAX_DEVICES) {
        free(infos); pull_ops()->shutdown();
        return camera_fail(total > MOO_PULL_CAPTURE_MAX_DEVICES
            ? "kamera_liste: mehr als 1024 Kameras — Kandidatenlimit ueberschritten"
            : (error[0] ? error : "kamera_liste: Kamera-Aufzaehlung fehlgeschlagen"));
    }
    MooValue list = moo_list_new(total > 0 ? total : 1);
    for (int32_t i = 0; i < total; ++i) {
        MooValue d = moo_dict_new();
        moo_dict_set(d, moo_string_new("pfad"), moo_string_new(infos[i].id));
        moo_dict_set(d, moo_string_new("id"), moo_string_new(infos[i].id));
        moo_dict_set(d, moo_string_new("name"), moo_string_new(infos[i].name));
        moo_list_append(list, d); /* Transfer-Semantik wie V4L2-Liste. */
    }
    free(infos); pull_ops()->shutdown();
    return list;
}

bool moo_capture_camera_open_native(MooKamera* camera, const char* path,
                                    int32_t width, int32_t height, double fps,
                                    bool require_exact) {
    char error[256] = {0};
    if (!pull_ops()->startup(error, sizeof error))
        return open_error(camera->last_error, sizeof camera->last_error,
                          "kamera_oeffnen: natives Capture konnte nicht starten", error);
    PullCamera* n = (PullCamera*)calloc(1, sizeof(PullCamera));
    if (!n) { pull_ops()->shutdown(); return open_error(camera->last_error,
        sizeof camera->last_error, "kamera_oeffnen: Speicher voll", NULL); }
    n->started = true;
    int32_t aw=0, ah=0, bound=0; double afps=0.0;
    MooPullResult r = pull_ops()->camera_open(path, width, height, fps, require_exact,
        &n->session, &aw, &ah, &afps, &bound, error, sizeof error);
    if (r != MOO_PULL_OK || !n->session || aw < 1 || ah < 1 ||
        aw > MOO_CAPTURE_MAX_WIDTH || ah > MOO_CAPTURE_MAX_HEIGHT ||
        bound < 1 || bound > MOO_CAPTURE_MAX_BUFFERS) {
        if (n->session) pull_ops()->camera_close(n->session);
        free(n); pull_ops()->shutdown();
        return open_error(camera->last_error, sizeof camera->last_error,
                          "kamera_oeffnen: Kamera-Backend lieferte keinen gueltigen Stream", error);
    }
    if (require_exact && (aw != width || ah != height || afps != fps)) {
        pull_ops()->camera_close(n->session); free(n); pull_ops()->shutdown();
        return open_error(camera->last_error, sizeof camera->last_error,
                          "kamera_oeffnen: exakt angeforderte Aufloesung/FPS nicht verfuegbar", NULL);
    }
    n->queue_bound=bound; camera->backend=n; camera->width=aw; camera->height=ah;
    camera->fps=afps; camera->state=MOO_CAPTURE_STREAMING; return true;
}

MooValue moo_capture_camera_frame_native(MooKamera* camera, int32_t timeout_ms) {
    PullCamera* n=(PullCamera*)camera->backend;
    if(!n||!n->session)return camera_fail("kamera_frame: natives Capture-Backend fehlt");
    int64_t start=pull_ops()->monotonic_ms();
    if(start<0)return camera_fail("kamera_frame: monotone Uhr fehlt");
    int64_t deadline=start+timeout_ms; char error[256]={0};
    MooPullResult r=pull_ops()->camera_wait(n->session,timeout_ms,error,sizeof error);
    if(r==MOO_PULL_TIMEOUT||r==MOO_PULL_EMPTY)return camera_fail("kamera_frame: Timeout");
    if(r!=MOO_PULL_OK){
        camera->state=MOO_CAPTURE_BROKEN;moo_capture_camera_close_native(camera);
        return camera_fail(error[0]?error:"kamera_frame: Geraet getrennt");
    }
    MooPullFramePacket latest={0};bool have=false;
    for(int32_t i=0;i<n->queue_bound;i++){
        MooPullFramePacket p={0};r=pull_ops()->camera_next(n->session,&p,error,sizeof error);
        if(r==MOO_PULL_EMPTY)break;
        if(r!=MOO_PULL_OK){
            if(have)pull_ops()->camera_release(&latest);
            camera->state=MOO_CAPTURE_BROKEN;moo_capture_camera_close_native(camera);
            return camera_fail(error[0]?error:"kamera_frame: Kamera-Lesefehler");
        }
        if(have)pull_ops()->camera_release(&latest);
        latest=p;have=true;
    }
    if(!have)return camera_fail("kamera_frame: Timeout");
    size_t stride=(size_t)(latest.stride<0?-latest.stride:latest.stride);
    size_t need=stride*(size_t)latest.height;
    if(!latest.bgra||latest.width!=camera->width||latest.height!=camera->height||
       stride<(size_t)camera->width*4u||need>latest.bytes||need>MOO_CAPTURE_MAX_FRAME_BYTES){
        pull_ops()->camera_release(&latest);camera->state=MOO_CAPTURE_BROKEN;
        moo_capture_camera_close_native(camera);
        return camera_fail("kamera_frame: ungueltige native Framegroesse");
    }
    uint8_t* rgba=(uint8_t*)malloc((size_t)camera->width*camera->height*4u);
    if(!rgba){pull_ops()->camera_release(&latest);return camera_fail("kamera_frame: Speicher voll");}
    for(int32_t y=0;y<camera->height;y++){
        int32_t sy=latest.stride<0?camera->height-1-y:y;
        const uint8_t* src=latest.bgra+(size_t)sy*stride;
        uint8_t* dst=rgba+(size_t)y*camera->width*4u;
        for(int32_t x=0;x<camera->width;x++){
            dst[x*4]=src[x*4+2];dst[x*4+1]=src[x*4+1];
            dst[x*4+2]=src[x*4];dst[x*4+3]=src[x*4+3];
        }
    }
    pull_ops()->camera_release(&latest);
    if(timeout_ms>0&&remaining_ms(deadline)==0){free(rgba);return camera_fail("kamera_frame: Timeout");}
    return moo_frame_new_take(camera->width,camera->height,rgba);
}

void moo_capture_camera_close_native(MooKamera* camera) {
    if (!camera || !camera->backend) return;
    PullCamera* n = (PullCamera*)camera->backend;
    if(n->session)pull_ops()->camera_close(n->session);
    if (n->started) pull_ops()->shutdown();
    free(n);
    camera->backend = NULL;
}

bool moo_capture_microphone_open_native(MooMikro* microphone,const char* device,
                                        int32_t rate,int32_t channels){
    char error[256]={0};
    if(!pull_ops()->startup(error,sizeof error))return open_error(microphone->last_error,
      sizeof microphone->last_error,"mikro_oeffnen: natives Capture konnte nicht starten",error);
    PullMicrophone*n=(PullMicrophone*)calloc(1,sizeof(PullMicrophone));
    if(!n){pull_ops()->shutdown();return open_error(microphone->last_error,sizeof microphone->last_error,"mikro_oeffnen: Speicher voll",NULL);}
    n->started=true;int32_t ar=0,ac=0,period=0,buffer=0;
    MooPullResult r=pull_ops()->microphone_open(device,rate,channels,&n->session,
      &ar,&ac,&period,&buffer,error,sizeof error);
    if(r!=MOO_PULL_OK||!n->session||ar<1||ac<1||ac>2||period<1||buffer<period){
      if (n->session) pull_ops()->microphone_close(n->session);
      free(n);
      pull_ops()->shutdown();
      return open_error(microphone->last_error,sizeof microphone->last_error,
        "mikro_oeffnen: Mikrofon-Backend lieferte keinen gueltigen Stream",error);
    }
    microphone->backend=n;microphone->rate=ar;microphone->channels=ac;
    microphone->period_frames=period;microphone->buffer_frames=buffer;
    microphone->state=MOO_CAPTURE_STREAMING;return true;
}

MooValue moo_capture_microphone_read_native(MooMikro* microphone,int32_t samples,int32_t timeout_ms){
    PullMicrophone*n=(PullMicrophone*)microphone->backend;
    if(!n||!n->session)return camera_fail("mikro_lesen: natives Capture-Backend fehlt");
    int32_t shape[1]={samples};MooTensor*t=moo_tensor_raw(1,shape);if(!t)return moo_none();
    int64_t start=pull_ops()->monotonic_ms();if(start<0){moo_tensor_free(t);return camera_fail("mikro_lesen: monotone Uhr fehlt");}
    int64_t deadline=start+timeout_ms;int32_t filled=0,recoveries=0;char error[256]={0};bool first=true;
    while(filled<samples){
      while(n->spill&&n->spill_pos<n->spill_frames&&filled<samples){
        const float*q=n->spill+(size_t)n->spill_pos*n->spill_channels;
        t->data[filled++]=n->spill_channels==1?q[0]:(q[0]+q[1])*0.5f;n->spill_pos++;
      }
      if(n->spill&&n->spill_pos>=n->spill_frames){free(n->spill);n->spill=NULL;n->spill_frames=n->spill_pos=0;}
      if(filled>=samples)break;
      int left=first?timeout_ms:remaining_ms(deadline);first=false;
      if(left==0&&timeout_ms>0){moo_tensor_free(t);return camera_fail("mikro_lesen: Timeout nach Teildaten");}
      MooPullResult r=pull_ops()->microphone_wait(n->session,left,error,sizeof error);
      if(r==MOO_PULL_TIMEOUT||r==MOO_PULL_EMPTY){moo_tensor_free(t);return camera_fail("mikro_lesen: Timeout");}
      if(r==MOO_PULL_RECOVERABLE){
        if(++recoveries>MOO_PULL_CAPTURE_MAX_RECOVERIES||pull_ops()->microphone_recover(n->session,error,sizeof error)!=MOO_PULL_OK){
          moo_tensor_free(t);microphone->state=MOO_CAPTURE_BROKEN;moo_capture_microphone_close_native(microphone);
          return camera_fail("mikro_lesen: Mikrofon-Stream nicht wiederherstellbar");
        }
        filled=0;continue;
      }
      if(r!=MOO_PULL_OK){moo_tensor_free(t);microphone->state=MOO_CAPTURE_BROKEN;moo_capture_microphone_close_native(microphone);return camera_fail(error[0]?error:"mikro_lesen: Geraet getrennt");}
      MooPullAudioPacket p={0};r=pull_ops()->microphone_next(n->session,&p,error,sizeof error);
      if(r==MOO_PULL_EMPTY)continue;
      if(r==MOO_PULL_RECOVERABLE){
        /* z.B. WASAPI-Glitch mid-stream: Paket wurde vom System-Layer
         * verworfen — Stream wiederherstellen und neu sammeln. */
        if(++recoveries>MOO_PULL_CAPTURE_MAX_RECOVERIES||pull_ops()->microphone_recover(n->session,error,sizeof error)!=MOO_PULL_OK){
          moo_tensor_free(t);microphone->state=MOO_CAPTURE_BROKEN;moo_capture_microphone_close_native(microphone);
          return camera_fail("mikro_lesen: Mikrofon-Stream nicht wiederherstellbar");
        }
        filled=0;continue;
      }
      if(r!=MOO_PULL_OK||!p.samples||p.frames<1||p.channels<1||p.channels>2){
        if (p.token) pull_ops()->microphone_release(&p);
        moo_tensor_free(t);
        microphone->state=MOO_CAPTURE_BROKEN;
        moo_capture_microphone_close_native(microphone);
        return camera_fail(error[0]?error:"mikro_lesen: ungueltiges WASAPI-Paket");
      }
      size_t count=(size_t)p.frames*p.channels;n->spill=(float*)malloc(count*sizeof(float));
      if(!n->spill){pull_ops()->microphone_release(&p);moo_tensor_free(t);return camera_fail("mikro_lesen: Speicher voll");}
      memcpy(n->spill,p.samples,count*sizeof(float));n->spill_frames=p.frames;n->spill_channels=p.channels;n->spill_pos=0;
      pull_ops()->microphone_release(&p);
    }
    if(timeout_ms>0&&remaining_ms(deadline)==0){
      moo_tensor_free(t);return camera_fail("mikro_lesen: Timeout nach Teildaten");
    }
    MooValue tv={MOO_TENSOR,0};moo_val_set_ptr(&tv,t);MooValue result=moo_dict_new();
    moo_dict_set(result,moo_string_new("daten"),tv);
    moo_dict_set(result,moo_string_new("rate"),moo_number((double)microphone->rate));
    return result;
}
void moo_capture_microphone_close_native(MooMikro* microphone){
    if (!microphone || !microphone->backend) return;
    PullMicrophone* n=(PullMicrophone*)microphone->backend;
    free(n->spill);
    if (n->session) pull_ops()->microphone_close(n->session);
    if (n->started) pull_ops()->shutdown();
    free(n);
    microphone->backend=NULL;
}

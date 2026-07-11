#define COBJMACROS
#define INITGUID
#include "moo_capture_windows_internal.h"
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <stdlib.h>
#include <string.h>

static LONG startup_refs = 0;
static SRWLOCK startup_lock = SRWLOCK_INIT;
static _Thread_local int com_uninit_pending = 0;
static void set_hr(char* out, size_t cap, const char* where, HRESULT hr) {
    if (out && cap) snprintf(out, cap, "%s (HRESULT 0x%08lx)", where, (unsigned long)hr);
}
static int64_t system_clock_ms(void) {
    return (int64_t)GetTickCount64();
}
static bool utf8_to_wide(const char* text, WCHAR** out) {
    *out = NULL;
    if (!text) return true;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (n <= 0) return false;
    WCHAR* w = (WCHAR*)CoTaskMemAlloc((size_t)n * sizeof(WCHAR));
    if (!w) return false;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, w, n)) {
        CoTaskMemFree(w); return false;
    }
    *out = w; return true;
}
static void wide_to_utf8(const WCHAR* w, char* out, size_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (!w) return;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL);
    out[cap - 1] = 0;
}

/* COM apartments are thread-affine: every successful startup must be paired
 * with shutdown on the same thread. The public capture layer preserves this. */
static bool system_startup(char* error, size_t cap) {
    HRESULT co = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(co) && co != RPC_E_CHANGED_MODE) {
        set_hr(error, cap, "Windows COM-Initialisierung fehlgeschlagen", co);
        return false;
    }
    if (co == S_OK || co == S_FALSE) com_uninit_pending++;
    AcquireSRWLockExclusive(&startup_lock);
    HRESULT hr = S_OK;
    if (startup_refs == 0) hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (SUCCEEDED(hr)) startup_refs++;
    ReleaseSRWLockExclusive(&startup_lock);
    if (FAILED(hr)) {
        if (com_uninit_pending > 0) { com_uninit_pending--; CoUninitialize(); }
        set_hr(error, cap, "Media Foundation konnte nicht starten", hr);
        return false;
    }
    return true;
}
static void system_shutdown(void) {
    AcquireSRWLockExclusive(&startup_lock);
    if (startup_refs > 0 && --startup_refs == 0) (void)MFShutdown();
    ReleaseSRWLockExclusive(&startup_lock);
    if (com_uninit_pending > 0) { com_uninit_pending--; CoUninitialize(); }
}

static HRESULT camera_activates(IMFActivate*** out, UINT32* count) {
    IMFAttributes* attr = NULL;
    HRESULT hr = MFCreateAttributes(&attr, 1);
    if (SUCCEEDED(hr))
        hr = IMFAttributes_SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                   &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attr, out, count);
    if (attr) IMFAttributes_Release(attr);
    return hr;
}
static void release_activates(IMFActivate** a, UINT32 n) {
    if (!a) return;
    for (UINT32 i=0;i<n;i++) if (a[i]) IMFActivate_Release(a[i]);
    CoTaskMemFree(a);
}
static MooWinCaptureResult system_camera_enumerate(MooWinCameraInfo* out, int32_t capn,
                                                    int32_t* total, char* error, size_t ecap) {
    IMFActivate** a=NULL; UINT32 n=0; HRESULT hr=camera_activates(&a,&n);
    if (FAILED(hr)) { set_hr(error,ecap,"kamera_liste: MFEnumDeviceSources fehlgeschlagen",hr); return MOO_WIN_ERROR; }
    *total = n > INT32_MAX ? INT32_MAX : (int32_t)n;
    UINT32 lim = n < (UINT32)capn ? n : (UINT32)capn;
    for (UINT32 i=0;i<lim;i++) {
        WCHAR *name=NULL,*id=NULL; UINT32 z=0;
        (void)IMFActivate_GetAllocatedString(a[i],&MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,&name,&z);
        (void)IMFActivate_GetAllocatedString(a[i],&MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,&id,&z);
        wide_to_utf8(name,out[i].name,sizeof out[i].name);
        wide_to_utf8(id,out[i].id,sizeof out[i].id);
        CoTaskMemFree(name); CoTaskMemFree(id);
    }
    release_activates(a,n);
    return MOO_WIN_OK;
}

typedef struct CameraCallback CameraCallback;
struct CameraCallback {
    IMFSourceReaderCallback iface;
    LONG refs;
    CRITICAL_SECTION lock;
    HANDLE event;
    IMFSourceReader* reader;
    bool running;
    BYTE* latest;
    DWORD latest_len;
    LONG width, height, stride;
    HRESULT last_hr;
};
static CameraCallback* cb_from(IMFSourceReaderCallback* p) {
    return CONTAINING_RECORD(p,CameraCallback,iface);
}
static HRESULT STDMETHODCALLTYPE cb_qi(IMFSourceReaderCallback* self, REFIID riid, void** out) {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid,&IID_IUnknown)||IsEqualIID(riid,&IID_IMFSourceReaderCallback)) {
        *out=self; IMFSourceReaderCallback_AddRef(self); return S_OK;
    }
    *out=NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE cb_add(IMFSourceReaderCallback* self) {
    return (ULONG)InterlockedIncrement(&cb_from(self)->refs);
}
static ULONG STDMETHODCALLTYPE cb_release(IMFSourceReaderCallback* self) {
    CameraCallback*c=cb_from(self); LONG r=InterlockedDecrement(&c->refs);
    if(r==0){free(c->latest);CloseHandle(c->event);DeleteCriticalSection(&c->lock);free(c);}
    return (ULONG)r;
}
static HRESULT STDMETHODCALLTYPE cb_sample(IMFSourceReaderCallback* self,HRESULT status,
    DWORD stream,DWORD flags,LONGLONG ts,IMFSample* sample) {
    (void)stream;(void)flags;(void)ts;
    CameraCallback*c=cb_from(self);
    if(SUCCEEDED(status)&&sample){
        IMFMediaBuffer*b=NULL;BYTE*p=NULL;DWORD len=0;
        if(SUCCEEDED(IMFSample_ConvertToContiguousBuffer(sample,&b))&&
           SUCCEEDED(IMFMediaBuffer_Lock(b,&p,NULL,&len))){
            BYTE*copy=(BYTE*)malloc(len);
            if(copy){memcpy(copy,p,len);EnterCriticalSection(&c->lock);
                free(c->latest);c->latest=copy;c->latest_len=len;c->last_hr=S_OK;
                LeaveCriticalSection(&c->lock);SetEvent(c->event);
            }
            IMFMediaBuffer_Unlock(b);
        }
        if(b)IMFMediaBuffer_Release(b);
    } else {
        EnterCriticalSection(&c->lock);c->last_hr=status;LeaveCriticalSection(&c->lock);
        SetEvent(c->event);
    }
    IMFSourceReader*reader=NULL;
    EnterCriticalSection(&c->lock);
    if(c->running&&c->reader){reader=c->reader;IMFSourceReader_AddRef(reader);}
    LeaveCriticalSection(&c->lock);
    if(reader){
        (void)IMFSourceReader_ReadSample(reader,MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,NULL,NULL,NULL,NULL);
        IMFSourceReader_Release(reader);
    }
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE cb_flush(IMFSourceReaderCallback* self,DWORD stream){
    (void)self;(void)stream;return S_OK;
}
static HRESULT STDMETHODCALLTYPE cb_event(IMFSourceReaderCallback* self,DWORD stream,IMFMediaEvent* ev){
    (void)self;(void)stream;(void)ev;return S_OK;
}
static IMFSourceReaderCallbackVtbl cb_vtbl={cb_qi,cb_add,cb_release,cb_sample,cb_flush,cb_event};
static CameraCallback* cb_new(void){
    CameraCallback*c=(CameraCallback*)calloc(1,sizeof(*c));if(!c)return NULL;
    c->iface.lpVtbl=&cb_vtbl;c->refs=1;c->event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!c->event){free(c);return NULL;}InitializeCriticalSection(&c->lock);c->last_hr=S_OK;return c;
}
typedef struct {
    IMFMediaSource* source;
    IMFSourceReader* reader;
    CameraCallback* callback;
} MfCamera;

static HRESULT activate_camera(const char* id, IMFMediaSource** out) {
    IMFActivate** a=NULL;UINT32 n=0;HRESULT hr=camera_activates(&a,&n);
    if (FAILED(hr)) return hr;
    hr=MF_E_NOT_FOUND;
    for(UINT32 i=0;i<n;i++){
        bool match=!id||!id[0];
        WCHAR*wid=NULL;UINT32 z=0;
        if(!match&&SUCCEEDED(IMFActivate_GetAllocatedString(a[i],
          &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,&wid,&z))){
            char u8[512];wide_to_utf8(wid,u8,sizeof u8);match=strcmp(u8,id)==0;CoTaskMemFree(wid);
        }
        if(match){hr=IMFActivate_ActivateObject(a[i],&IID_IMFMediaSource,(void**)out);break;}
    }
    release_activates(a,n);return hr;
}
static MooWinCaptureResult system_camera_open(const char* id,int32_t width,int32_t height,
 double fps,bool exact,void** session,int32_t* aw,int32_t* ah,double* afps,int32_t* bound,
 char* error,size_t ecap){
    (void)exact;MfCamera*n=(MfCamera*)calloc(1,sizeof(*n));if(!n)return MOO_WIN_ERROR;
    HRESULT hr=activate_camera(id,&n->source);if(FAILED(hr)){set_hr(error,ecap,"kamera_oeffnen: Kamera nicht gefunden",hr);free(n);return MOO_WIN_ERROR;}
    n->callback=cb_new();IMFAttributes*attr=NULL;IMFMediaType*type=NULL;
    if(!n->callback)hr=E_OUTOFMEMORY;
    if(SUCCEEDED(hr))hr=MFCreateAttributes(&attr,2);
    if(SUCCEEDED(hr))hr=IMFAttributes_SetUnknown(attr,&MF_SOURCE_READER_ASYNC_CALLBACK,(IUnknown*)&n->callback->iface);
    if(SUCCEEDED(hr))hr=IMFAttributes_SetUINT32(attr,&MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
    if(SUCCEEDED(hr))hr=MFCreateSourceReaderFromMediaSource(n->source,attr,&n->reader);
    if(SUCCEEDED(hr))hr=MFCreateMediaType(&type);
    if(SUCCEEDED(hr))hr=IMFMediaType_SetGUID(type,&MF_MT_MAJOR_TYPE,&MFMediaType_Video);
    if(SUCCEEDED(hr))hr=IMFMediaType_SetGUID(type,&MF_MT_SUBTYPE,&MFVideoFormat_RGB32);
    if(SUCCEEDED(hr))hr=IMFMediaType_SetUINT64(type,&MF_MT_FRAME_SIZE,((UINT64)(UINT32)width<<32)|(UINT32)height);
    if(SUCCEEDED(hr))hr=IMFMediaType_SetUINT64(type,&MF_MT_FRAME_RATE,((UINT64)(UINT32)(fps*1000.0+0.5)<<32)|1000u);
    if(SUCCEEDED(hr))hr=IMFSourceReader_SetCurrentMediaType(n->reader,MF_SOURCE_READER_FIRST_VIDEO_STREAM,NULL,type);
    UINT64 actual_size=0, actual_rate=0; UINT32 actual_stride_bits=0;
    IMFMediaType* current=NULL;
    if(SUCCEEDED(hr))hr=IMFSourceReader_GetCurrentMediaType(n->reader,MF_SOURCE_READER_FIRST_VIDEO_STREAM,&current);
    if(SUCCEEDED(hr))hr=IMFMediaType_GetUINT64(current,&MF_MT_FRAME_SIZE,&actual_size);
    if(SUCCEEDED(hr))hr=IMFMediaType_GetUINT64(current,&MF_MT_FRAME_RATE,&actual_rate);
    if(SUCCEEDED(hr) && FAILED(IMFMediaType_GetUINT32(current,&MF_MT_DEFAULT_STRIDE,&actual_stride_bits)))
      actual_stride_bits=(UINT32)(width*4);
    if(current)IMFMediaType_Release(current);
    if(type)IMFMediaType_Release(type);
    if(attr)IMFAttributes_Release(attr);
    if(FAILED(hr)){
        set_hr(error,ecap,"kamera_oeffnen: RGB32-Format konnte nicht gesetzt werden",hr);
        if(n->reader)IMFSourceReader_Release(n->reader);
        if(n->source)IMFMediaSource_Release(n->source);
        if(n->callback)IMFSourceReaderCallback_Release(&n->callback->iface);
        free(n);return MOO_WIN_ERROR;
    }
    int32_t got_width=(int32_t)(actual_size>>32), got_height=(int32_t)(actual_size&0xffffffffu);
    UINT32 rate_num=(UINT32)(actual_rate>>32), rate_den=(UINT32)(actual_rate&0xffffffffu);
    double got_fps=rate_den?(double)rate_num/(double)rate_den:0.0;
    if(got_width<1||got_height<1||got_fps<=0.0){
      set_hr(error,ecap,"kamera_oeffnen: Media Foundation lieferte ungueltige Geometrie",E_FAIL);
      IMFSourceReader_Release(n->reader);IMFMediaSource_Release(n->source);
      IMFSourceReaderCallback_Release(&n->callback->iface);free(n);return MOO_WIN_ERROR;
    }
    EnterCriticalSection(&n->callback->lock);
    n->callback->reader=n->reader;n->callback->running=true;n->callback->width=got_width;
    n->callback->height=got_height;n->callback->stride=(LONG)actual_stride_bits;
    LeaveCriticalSection(&n->callback->lock);
    hr=IMFSourceReader_ReadSample(n->reader,MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,NULL,NULL,NULL,NULL);
    if(FAILED(hr)){set_hr(error,ecap,"kamera_oeffnen: erster ReadSample fehlgeschlagen",hr);
      EnterCriticalSection(&n->callback->lock);n->callback->running=false;n->callback->reader=NULL;
      LeaveCriticalSection(&n->callback->lock);IMFSourceReader_Release(n->reader);IMFMediaSource_Release(n->source);
      IMFSourceReaderCallback_Release(&n->callback->iface);free(n);return MOO_WIN_ERROR;}
    *session=n;*aw=got_width;*ah=got_height;*afps=got_fps;*bound=1;return MOO_WIN_OK;
}
static MooWinCaptureResult system_camera_wait(void* session,int32_t timeout,char* error,size_t cap){
    MfCamera*n=(MfCamera*)session;DWORD r=WaitForSingleObject(n->callback->event,(DWORD)timeout);
    if(r==WAIT_TIMEOUT)return MOO_WIN_TIMEOUT;
    if(r!=WAIT_OBJECT_0){set_hr(error,cap,"kamera_frame: Event-Wait fehlgeschlagen",HRESULT_FROM_WIN32(GetLastError()));return MOO_WIN_ERROR;}
    return MOO_WIN_OK;
}
static MooWinCaptureResult system_camera_next(void* session,MooWinFramePacket*out,char*error,size_t cap){
    MfCamera*n=(MfCamera*)session;CameraCallback*c=n->callback;EnterCriticalSection(&c->lock);
    if(FAILED(c->last_hr)){HRESULT hr=c->last_hr;LeaveCriticalSection(&c->lock);set_hr(error,cap,"kamera_frame: asynchroner ReadSample-Fehler",hr);return MOO_WIN_DISCONNECTED;}
    if(!c->latest){ResetEvent(c->event);LeaveCriticalSection(&c->lock);return MOO_WIN_EMPTY;}
    out->bgra=c->latest;out->bytes=c->latest_len;out->width=c->width;out->height=c->height;out->stride=c->stride;
    c->latest=NULL;c->latest_len=0;ResetEvent(c->event);LeaveCriticalSection(&c->lock);return MOO_WIN_OK;
}
static void system_camera_release(MooWinFramePacket*p){free(p->bgra);memset(p,0,sizeof(*p));}
static void system_camera_close(void* session){
    MfCamera*n=(MfCamera*)session;if(!n)return;
    EnterCriticalSection(&n->callback->lock);n->callback->running=false;n->callback->reader=NULL;
    LeaveCriticalSection(&n->callback->lock);
    if(n->reader){(void)IMFSourceReader_Flush(n->reader,MF_SOURCE_READER_FIRST_VIDEO_STREAM);IMFSourceReader_Release(n->reader);}
    if(n->source){(void)IMFMediaSource_Shutdown(n->source);IMFMediaSource_Release(n->source);}
    IMFSourceReaderCallback_Release(&n->callback->iface);free(n);
}

typedef struct {
    IMMDeviceEnumerator* enumerator;IMMDevice* device;IAudioClient* client;
    IAudioCaptureClient* capture;HANDLE event;int32_t channels;
} WasapiMic;
static MooWinCaptureResult system_microphone_open(const char* id,int32_t rate,int32_t channels,
 void**session,int32_t*ar,int32_t*ac,int32_t*period,int32_t*buffer,char*error,size_t cap){
    WasapiMic*n=(WasapiMic*)calloc(1,sizeof(*n));if(!n)return MOO_WIN_ERROR;HRESULT hr;
    hr=CoCreateInstance(&CLSID_MMDeviceEnumerator,NULL,CLSCTX_ALL,&IID_IMMDeviceEnumerator,(void**)&n->enumerator);
    WCHAR*wid=NULL;if(SUCCEEDED(hr)&&id&&id[0]&&strcmp(id,"default")!=0){
      if(!utf8_to_wide(id,&wid))hr=E_INVALIDARG;else hr=IMMDeviceEnumerator_GetDevice(n->enumerator,wid,&n->device);
    }else if(SUCCEEDED(hr))hr=IMMDeviceEnumerator_GetDefaultAudioEndpoint(n->enumerator,eCapture,eConsole,&n->device);
    CoTaskMemFree(wid);if(SUCCEEDED(hr))hr=IMMDevice_Activate(n->device,&IID_IAudioClient,CLSCTX_ALL,NULL,(void**)&n->client);
    WAVEFORMATEX fmt={0};fmt.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;fmt.nChannels=(WORD)channels;fmt.nSamplesPerSec=(DWORD)rate;
    fmt.wBitsPerSample=32;fmt.nBlockAlign=(WORD)(channels*4);fmt.nAvgBytesPerSec=fmt.nSamplesPerSec*fmt.nBlockAlign;
    DWORD flags=AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if(SUCCEEDED(hr))hr=IAudioClient_Initialize(n->client,AUDCLNT_SHAREMODE_SHARED,flags,0,0,&fmt,NULL);
    UINT32 frames=0;if(SUCCEEDED(hr))hr=IAudioClient_GetBufferSize(n->client,&frames);
    if(SUCCEEDED(hr)){n->event=CreateEventW(NULL,FALSE,FALSE,NULL);if(!n->event)hr=HRESULT_FROM_WIN32(GetLastError());}
    if(SUCCEEDED(hr))hr=IAudioClient_SetEventHandle(n->client,n->event);
    if(SUCCEEDED(hr))hr=IAudioClient_GetService(n->client,&IID_IAudioCaptureClient,(void**)&n->capture);
    if(SUCCEEDED(hr))hr=IAudioClient_Start(n->client);
    if(FAILED(hr)){set_hr(error,cap,"mikro_oeffnen: WASAPI-Initialisierung fehlgeschlagen",hr);
      if(n->capture)IAudioCaptureClient_Release(n->capture);
      if(n->client)IAudioClient_Release(n->client);
      if(n->device)IMMDevice_Release(n->device);
      if(n->enumerator)IMMDeviceEnumerator_Release(n->enumerator);
      if(n->event)CloseHandle(n->event);
      free(n);return hr==AUDCLNT_E_DEVICE_INVALIDATED?MOO_WIN_DISCONNECTED:MOO_WIN_ERROR;}
    n->channels=channels;*session=n;*ar=rate;*ac=channels;*buffer=(int32_t)frames;*period=frames>4?(int32_t)(frames/4):1;return MOO_WIN_OK;
}
static MooWinCaptureResult system_microphone_wait(void*session,int32_t timeout,char*error,size_t cap){
    WasapiMic*n=(WasapiMic*)session;DWORD r=WaitForSingleObject(n->event,(DWORD)timeout);
    if(r==WAIT_TIMEOUT)return MOO_WIN_TIMEOUT;
    if(r!=WAIT_OBJECT_0){set_hr(error,cap,"mikro_lesen: Event-Wait fehlgeschlagen",HRESULT_FROM_WIN32(GetLastError()));return MOO_WIN_ERROR;}
    return MOO_WIN_OK;
}
static MooWinCaptureResult system_microphone_next(void*session,MooWinAudioPacket*out,char*error,size_t cap){
    WasapiMic*n=(WasapiMic*)session;UINT32 avail=0;HRESULT hr=IAudioCaptureClient_GetNextPacketSize(n->capture,&avail);
    if(FAILED(hr)){set_hr(error,cap,"mikro_lesen: GetNextPacketSize fehlgeschlagen",hr);return hr==AUDCLNT_E_DEVICE_INVALIDATED?MOO_WIN_DISCONNECTED:MOO_WIN_ERROR;}
    if(!avail)return MOO_WIN_EMPTY;
    BYTE*data=NULL;UINT32 frames=0;DWORD flags=0;
    hr=IAudioCaptureClient_GetBuffer(n->capture,&data,&frames,&flags,NULL,NULL);
    if(FAILED(hr)){set_hr(error,cap,"mikro_lesen: GetBuffer fehlgeschlagen",hr);return hr==AUDCLNT_E_DEVICE_INVALIDATED?MOO_WIN_DISCONNECTED:MOO_WIN_ERROR;}
    size_t count=(size_t)frames*n->channels;float*copy=(float*)calloc(count,sizeof(float));
    if(!copy){IAudioCaptureClient_ReleaseBuffer(n->capture,frames);return MOO_WIN_ERROR;}
    if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT))memcpy(copy,data,count*sizeof(float));
    hr=IAudioCaptureClient_ReleaseBuffer(n->capture,frames);
    if(FAILED(hr)){free(copy);set_hr(error,cap,"mikro_lesen: ReleaseBuffer fehlgeschlagen",hr);return MOO_WIN_ERROR;}
    if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY){free(copy);return MOO_WIN_RECOVERABLE;}
    out->samples=copy;out->frames=(int32_t)frames;out->channels=n->channels;out->token=copy;out->flags=flags;return MOO_WIN_OK;
}
static void system_microphone_release(MooWinAudioPacket*p){free(p->token);memset(p,0,sizeof(*p));}
static MooWinCaptureResult system_microphone_recover(void*session,char*error,size_t cap){
    WasapiMic*n=(WasapiMic*)session;HRESULT hr=IAudioClient_Stop(n->client);
    if(SUCCEEDED(hr))hr=IAudioClient_Reset(n->client);
    if(SUCCEEDED(hr))hr=IAudioClient_Start(n->client);
    if(FAILED(hr)){set_hr(error,cap,"mikro_lesen: WASAPI-Recovery fehlgeschlagen",hr);return MOO_WIN_ERROR;}return MOO_WIN_OK;
}
static void system_microphone_close(void*session){
    WasapiMic*n=(WasapiMic*)session;if(!n)return;
    if(n->client)(void)IAudioClient_Stop(n->client);
    if(n->capture)IAudioCaptureClient_Release(n->capture);
    if(n->client)IAudioClient_Release(n->client);
    if(n->device)IMMDevice_Release(n->device);
    if(n->enumerator)IMMDeviceEnumerator_Release(n->enumerator);
    if(n->event)CloseHandle(n->event);
    free(n);
}
static const MooCaptureWindowsOps ops={
 system_clock_ms,system_startup,system_shutdown,system_camera_enumerate,system_camera_open,
 system_camera_wait,system_camera_next,system_camera_release,system_camera_close,
 system_microphone_open,system_microphone_wait,system_microphone_next,
 system_microphone_release,system_microphone_recover,system_microphone_close
};
const MooCaptureWindowsOps* moo_capture_windows_system_ops(void){return &ops;}

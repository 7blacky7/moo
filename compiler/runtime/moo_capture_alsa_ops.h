#ifndef MOO_CAPTURE_ALSA_OPS_H
#define MOO_CAPTURE_ALSA_OPS_H

#include <alsa/asoundlib.h>
#include <time.h>

typedef struct {
    int (*pcm_open)(snd_pcm_t**, const char*, snd_pcm_stream_t, int);
    int (*hw_any)(snd_pcm_t*, snd_pcm_hw_params_t*);
    int (*hw_access)(snd_pcm_t*, snd_pcm_hw_params_t*, snd_pcm_access_t);
    int (*hw_format)(snd_pcm_t*, snd_pcm_hw_params_t*, snd_pcm_format_t);
    int (*hw_channels)(snd_pcm_t*, snd_pcm_hw_params_t*, unsigned int);
    int (*hw_rate_near)(snd_pcm_t*, snd_pcm_hw_params_t*, unsigned int*, int*);
    int (*hw_period_near)(snd_pcm_t*, snd_pcm_hw_params_t*, snd_pcm_uframes_t*, int*);
    int (*hw_buffer_near)(snd_pcm_t*, snd_pcm_hw_params_t*, snd_pcm_uframes_t*);
    int (*hw_commit)(snd_pcm_t*, snd_pcm_hw_params_t*);
    int (*hw_get_channels)(const snd_pcm_hw_params_t*, unsigned int*);
    int (*hw_get_rate)(const snd_pcm_hw_params_t*, unsigned int*, int*);
    int (*hw_get_period)(const snd_pcm_hw_params_t*, snd_pcm_uframes_t*, int*);
    int (*hw_get_buffer)(const snd_pcm_hw_params_t*, snd_pcm_uframes_t*);
    int (*sw_current)(snd_pcm_t*, snd_pcm_sw_params_t*);
    int (*sw_avail_min)(snd_pcm_t*, snd_pcm_sw_params_t*, snd_pcm_uframes_t);
    int (*sw_commit)(snd_pcm_t*, snd_pcm_sw_params_t*);
    int (*prepare)(snd_pcm_t*);
    int (*wait)(snd_pcm_t*, int);
    int (*resume)(snd_pcm_t*);
    snd_pcm_sframes_t (*readi)(snd_pcm_t*, void*, snd_pcm_uframes_t);
    int (*drop)(snd_pcm_t*);
    int (*close)(snd_pcm_t*);
    int (*clock_now)(clockid_t, struct timespec*);
} MooCaptureAlsaOps;

void moo_capture_alsa_set_ops_for_tests(const MooCaptureAlsaOps* ops);
void moo_capture_alsa_reset_ops_for_tests(void);

#endif

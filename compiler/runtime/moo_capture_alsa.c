#include "moo_capture_internal.h"

#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    snd_pcm_t* pcm;
} MicrophoneNative;

static MooValue microphone_fail(const char* message) {
    moo_throw(moo_error(message));
    return moo_none();
}

static int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int remaining_ms(int64_t deadline) {
    int64_t now = monotonic_ms();
    if (now < 0 || now >= deadline) return 0;
    int64_t left = deadline - now;
    return left > INT32_MAX ? INT32_MAX : (int)left;
}

static bool wait_ready(snd_pcm_t* pcm, int64_t deadline) {
    int left = remaining_ms(deadline);
    if (left <= 0) return false;
    int result;
    do { result = snd_pcm_wait(pcm, left); } while (result < 0 && result == -EINTR);
    return result > 0;
}

static bool recover_stream(snd_pcm_t* pcm, int error, int64_t deadline) {
    if (error == -EPIPE)
        return snd_pcm_prepare(pcm) >= 0;
    if (error == -ESTRPIPE) {
        int result;
        do {
            result = snd_pcm_resume(pcm);
            if (result == -EAGAIN && !wait_ready(pcm, deadline)) return false;
        } while (result == -EAGAIN || result == -EINTR);
        if (result < 0) result = snd_pcm_prepare(pcm);
        return result >= 0;
    }
    return false;
}

bool moo_capture_microphone_open_native(MooMikro* microphone, const char* device,
                                        int32_t rate, int32_t channels) {
    MicrophoneNative* native = calloc(1, sizeof(*native));
    if (!native) { microphone_fail("mikro_oeffnen: Speicher voll"); return false; }
    microphone->backend = native;
    int result = snd_pcm_open(&native->pcm, device, SND_PCM_STREAM_CAPTURE,
                              SND_PCM_NONBLOCK);
    if (result < 0) {
        microphone_fail(result == -EBUSY ? "mikro_oeffnen: Geraet belegt" :
                         (result == -EACCES ? "mikro_oeffnen: keine Berechtigung" :
                          "mikro_oeffnen: ALSA-Geraet konnte nicht geoeffnet werden"));
        return false;
    }

    snd_pcm_hw_params_t* hw;
    snd_pcm_hw_params_alloca(&hw);
    if ((result = snd_pcm_hw_params_any(native->pcm, hw)) < 0 ||
        (result = snd_pcm_hw_params_set_access(native->pcm, hw,
                  SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
        (result = snd_pcm_hw_params_set_format(native->pcm, hw,
                  SND_PCM_FORMAT_S16_LE)) < 0 ||
        (result = snd_pcm_hw_params_set_channels(native->pcm, hw,
                  (unsigned int)channels)) < 0) {
        microphone_fail("mikro_oeffnen: S16_LE/interleaved/Kanaele nicht unterstuetzt");
        return false;
    }

    unsigned int actual_rate = (unsigned int)rate;
    int direction = 0;
    if (snd_pcm_hw_params_set_rate_near(native->pcm, hw, &actual_rate,
                                        &direction) < 0) {
        microphone_fail("mikro_oeffnen: Sample-Rate konnte nicht verhandelt werden");
        return false;
    }
    snd_pcm_uframes_t period = 1024;
    snd_pcm_uframes_t buffer = 4096;
    direction = 0;
    (void)snd_pcm_hw_params_set_period_size_near(native->pcm, hw, &period,
                                                  &direction);
    (void)snd_pcm_hw_params_set_buffer_size_near(native->pcm, hw, &buffer);
    if (snd_pcm_hw_params(native->pcm, hw) < 0) {
        microphone_fail("mikro_oeffnen: ALSA-Hardwareparameter abgelehnt");
        return false;
    }

    snd_pcm_hw_params_get_channels(hw, (unsigned int*)&microphone->channels);
    snd_pcm_hw_params_get_rate(hw, &actual_rate, &direction);
    snd_pcm_hw_params_get_period_size(hw, &period, &direction);
    snd_pcm_hw_params_get_buffer_size(hw, &buffer);
    if (microphone->channels < 1 || microphone->channels > 2 ||
        actual_rate == 0 || period > INT32_MAX || buffer > INT32_MAX) {
        microphone_fail("mikro_oeffnen: ALSA lieferte ungueltige Parameter");
        return false;
    }

    snd_pcm_sw_params_t* sw;
    snd_pcm_sw_params_alloca(&sw);
    if (snd_pcm_sw_params_current(native->pcm, sw) < 0 ||
        snd_pcm_sw_params_set_avail_min(native->pcm, sw, period) < 0 ||
        snd_pcm_sw_params(native->pcm, sw) < 0 ||
        snd_pcm_prepare(native->pcm) < 0) {
        microphone_fail("mikro_oeffnen: ALSA-Softwareparameter/prepare fehlgeschlagen");
        return false;
    }

    microphone->rate = (int32_t)actual_rate;
    microphone->period_frames = (int32_t)period;
    microphone->buffer_frames = (int32_t)buffer;
    microphone->state = MOO_CAPTURE_STREAMING;
    return true;
}

MooValue moo_capture_microphone_read_native(MooMikro* microphone,
                                            int32_t samples, int32_t timeout_ms) {
    MicrophoneNative* native = microphone->backend;
    if (!native || !native->pcm) return microphone_fail("mikro_lesen: Backend fehlt");
    uint64_t elements = (uint64_t)(uint32_t)samples *
                        (uint64_t)(uint32_t)microphone->channels;
    if (elements > SIZE_MAX / sizeof(int16_t))
        return microphone_fail("mikro_lesen: Sample-Groesse laeuft ueber");
    int16_t* input = malloc((size_t)elements * sizeof(int16_t));
    if (!input) return microphone_fail("mikro_lesen: Speicher voll");

    int64_t start = monotonic_ms();
    if (start < 0) { free(input); return microphone_fail("mikro_lesen: monotone Uhr fehlt"); }
    int64_t deadline = start + timeout_ms;
    int32_t filled = 0;
    int recoveries = 0;
    while (filled < samples) {
        snd_pcm_sframes_t got = snd_pcm_readi(
            native->pcm,
            input + (size_t)filled * microphone->channels,
            (snd_pcm_uframes_t)(samples - filled));
        if (got > 0) { filled += (int32_t)got; continue; }
        if (got == -EINTR) continue;
        if (got == -EAGAIN || got == 0) {
            if (!wait_ready(native->pcm, deadline)) {
                free(input); return microphone_fail("mikro_lesen: Timeout");
            }
            continue;
        }
        if (got == -EPIPE || got == -ESTRPIPE) {
            if (++recoveries > 3 || !recover_stream(native->pcm, (int)got,
                                                     deadline)) {
                free(input); microphone->state = MOO_CAPTURE_BROKEN;
                return microphone_fail("mikro_lesen: XRUN/Suspend nicht wiederherstellbar");
            }
            filled = 0;
            continue;
        }
        free(input); microphone->state = MOO_CAPTURE_BROKEN;
        return microphone_fail("mikro_lesen: Geraet getrennt oder ALSA-Lesefehler");
    }
    if (remaining_ms(deadline) == 0 && timeout_ms > 0) {
        free(input); return microphone_fail("mikro_lesen: Timeout nach Teildaten");
    }

    int32_t shape[1] = { samples };
    MooTensor* tensor = moo_tensor_raw(1, shape);
    if (!tensor) { free(input); return moo_none(); }
    if (microphone->channels == 1) {
        for (int32_t i = 0; i < samples; ++i)
            tensor->data[i] = (float)input[i] / 32768.0f;
    } else {
        for (int32_t i = 0; i < samples; ++i) {
            int32_t mixed = (int32_t)input[(size_t)i * 2] +
                            (int32_t)input[(size_t)i * 2 + 1];
            tensor->data[i] = (float)mixed / 65536.0f;
        }
    }
    free(input);
    MooValue tensor_value = { MOO_TENSOR, 0 };
    moo_val_set_ptr(&tensor_value, tensor);
    MooValue result = moo_dict_new();
    moo_dict_set(result, moo_string_new("daten"), tensor_value);
    moo_dict_set(result, moo_string_new("rate"),
                 moo_number((double)microphone->rate));
    return result;
}

void moo_capture_microphone_close_native(MooMikro* microphone) {
    if (!microphone || !microphone->backend) return;
    MicrophoneNative* native = microphone->backend;
    if (native->pcm) {
        (void)snd_pcm_drop(native->pcm);
        snd_pcm_close(native->pcm);
        native->pcm = NULL;
    }
    free(native);
    microphone->backend = NULL;
}

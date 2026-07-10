/**
 * test_audio_asan.c — KI-MULTI-A1: FFT/STFT/WAV unter ASan+UBSan.
 *
 * Prueft radix-2 FFT gegen naive DFT (<1e-6), Parseval, Zero-Padding,
 * Betragsspektrum, Hann-STFT-Shape, 16-bit-PCM-Stereo->Mono und Fehlerpfade.
 */
#include "../moo_runtime.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_PI 3.14159265358979323846264338327950288

int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];

void moo_throw(MooValue error) {
    if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error));
    moo_error_flag = 1;
}
static void fehler_reset(void) { moo_error_flag = 0; }

void moo_socket_free(void* p)       { (void)p; }
void moo_thread_free(void* p)       { (void)p; }
void moo_channel_free(void* p)      { (void)p; }
void moo_db_free(void* p)           { (void)p; }
void moo_db_stmt_free(void* p)      { (void)p; }
void moo_window_free(void* p)       { (void)p; }
void moo_web_free(void* p)          { (void)p; }
void moo_voxel_free(void* p)        { (void)p; }

static int checks = 0;
#define CHECK(cond, name) do {     if (!(cond)) {         fprintf(stderr, "FAIL: %s (Zeile %d)\n", name, __LINE__);         return 1;     }     checks++; } while (0)

static MooValue tensor_1d(const float* values, int32_t count) {
    int32_t shape[1] = { count };
    MooTensor* tensor = moo_tensor_raw(1, shape);
    if (!tensor) return moo_none();
    memcpy(tensor->data, values, (size_t)count * sizeof(float));
    MooValue value;
    value.tag = MOO_TENSOR;
    moo_val_set_ptr(&value, tensor);
    return value;
}

static void dft_reference(const float* input, int32_t count, int32_t bin,
                          double* out_real, double* out_imag) {
    double real = 0.0;
    double imag = 0.0;
    for (int32_t n = 0; n < count; n++) {
        double angle = -2.0 * TEST_PI * (double)bin * (double)n /
                       (double)count;
        real += (double)input[n] * cos(angle);
        imag += (double)input[n] * sin(angle);
    }
    *out_real = real;
    *out_imag = imag;
}

static void write_u16(FILE* file, uint16_t value) {
    fputc((int)(value & 0xffu), file);
    fputc((int)((value >> 8u) & 0xffu), file);
}

static void write_u32(FILE* file, uint32_t value) {
    fputc((int)(value & 0xffu), file);
    fputc((int)((value >> 8u) & 0xffu), file);
    fputc((int)((value >> 16u) & 0xffu), file);
    fputc((int)((value >> 24u) & 0xffu), file);
}

static int write_test_wav(const char* path) {
    FILE* file = fopen(path, "wb");
    if (!file) return 0;
    const uint32_t data_size = 4u * 2u * 2u;
    fwrite("RIFF", 1, 4, file);
    write_u32(file, 36u + data_size);
    fwrite("WAVE", 1, 4, file);
    fwrite("fmt ", 1, 4, file);
    write_u32(file, 16u);
    write_u16(file, 1u);
    write_u16(file, 2u);
    write_u32(file, 8000u);
    write_u32(file, 8000u * 4u);
    write_u16(file, 4u);
    write_u16(file, 16u);
    fwrite("data", 1, 4, file);
    write_u32(file, data_size);

    /* Vier Stereo-Frames: Mitte, +0.5, -1.0, gegenphasig -> fast 0. */
    const int16_t samples[8] = {
        0, 0,
        16384, 16384,
        -32768, -32768,
        32767, -32768
    };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        write_u16(file, (uint16_t)samples[i]);
    }
    int ok = ferror(file) == 0 && fclose(file) == 0;
    return ok;
}

int main(void) {
    const float values[8] = {
        0.25f, -0.5f, 1.0f, 0.75f, -0.25f, 0.5f, 0.0f, -1.0f
    };
    MooValue input = tensor_1d(values, 8);
    CHECK(input.tag == MOO_TENSOR, "1D-Testtensor erzeugt");

    /* 1. FFT gegen naive DFT, jede einseitige Bin-Komponente < 1e-6. */
    MooValue fft = moo_fft(input);
    CHECK(fft.tag == MOO_TENSOR, "fft liefert Tensor");
    MooTensor* fp = MV_TENSOR(fft);
    CHECK(fp->ndim == 2 && fp->shape[0] == 5 && fp->shape[1] == 2,
          "fft Form [n/2+1,2]");
    for (int32_t bin = 0; bin < 5; bin++) {
        double expected_real = 0.0;
        double expected_imag = 0.0;
        dft_reference(values, 8, bin, &expected_real, &expected_imag);
        CHECK(fabs((double)fp->data[(int64_t)bin * 2] - expected_real) < 1e-6,
              "FFT Realteil gegen DFT < 1e-6");
        CHECK(fabs((double)fp->data[(int64_t)bin * 2 + 1] - expected_imag) < 1e-6,
              "FFT Imaginaerteil gegen DFT < 1e-6");
    }

    /* 2. Parseval fuer das einseitige reelle Spektrum. */
    double time_energy = 0.0;
    for (int i = 0; i < 8; i++) time_energy += (double)values[i] * values[i];
    double freq_energy = 0.0;
    for (int32_t bin = 0; bin < 5; bin++) {
        double real = fp->data[(int64_t)bin * 2];
        double imag = fp->data[(int64_t)bin * 2 + 1];
        double weight = (bin == 0 || bin == 4) ? 1.0 : 2.0;
        freq_energy += weight * (real * real + imag * imag);
    }
    freq_energy /= 8.0;
    CHECK(fabs(time_energy - freq_energy) < 1e-6, "Parseval < 1e-6");

    /* 3. Betragsspektrum stimmt mit hypot(FFT) ueberein. */
    MooValue magnitude = moo_spektrum_betrag(input);
    CHECK(magnitude.tag == MOO_TENSOR, "spektrum_betrag liefert Tensor");
    MooTensor* mp = MV_TENSOR(magnitude);
    CHECK(mp->ndim == 1 && mp->shape[0] == 5, "Betrag Form [bins]");
    for (int32_t bin = 0; bin < 5; bin++) {
        double expected = hypot(fp->data[(int64_t)bin * 2],
                                fp->data[(int64_t)bin * 2 + 1]);
        CHECK(fabs((double)mp->data[bin] - expected) < 1e-6,
              "Betrag entspricht hypot");
    }

    /* 4. Nicht-Zweierpotenz wird auf vier Samples gepolstert. */
    const float three[3] = { 1.0f, 2.0f, 3.0f };
    MooValue short_input = tensor_1d(three, 3);
    MooValue padded = moo_fft(short_input);
    MooTensor* pp = MV_TENSOR(padded);
    CHECK(pp->ndim == 2 && pp->shape[0] == 3,
          "3 Samples -> FFT-Laenge 4 -> 3 Bins");

    /* 5. Hann-STFT: 8 Samples, Fenster 4, Schritt 2 -> [3,3]. */
    MooValue stft = moo_spektrogramm(input, moo_number(4), moo_number(2));
    CHECK(stft.tag == MOO_TENSOR, "spektrogramm liefert Tensor");
    MooTensor* sp = MV_TENSOR(stft);
    CHECK(sp->ndim == 2 && sp->shape[0] == 3 && sp->shape[1] == 3,
          "Spektrogramm Form [3,3]");
    for (int64_t i = 0; i < sp->size; i++) {
        CHECK(isfinite(sp->data[i]) && sp->data[i] >= 0.0f,
              "Spektrogramm endlich und nichtnegativ");
    }

    /* 6. WAV 16-bit PCM stereo -> Mono-Dict mit Rate. */
    const char* path = "/tmp/moo_audio_test_asan.wav";
    CHECK(write_test_wav(path), "Test-WAV geschrieben");
    MooValue path_value = moo_string_new(path);
    MooValue wav = moo_wav_lesen(path_value);
    CHECK(wav.tag == MOO_DICT, "wav_lesen liefert Dict");
    MooValue data = moo_dict_get(wav, moo_string_new("daten"));
    MooValue rate = moo_dict_get(wav, moo_string_new("rate"));
    CHECK(data.tag == MOO_TENSOR, "wav daten ist Tensor");
    CHECK(rate.tag == MOO_NUMBER && moo_as_number(rate) == 8000.0,
          "wav rate = 8000");
    MooTensor* wp = MV_TENSOR(data);
    CHECK(wp->ndim == 1 && wp->shape[0] == 4, "wav Stereo-Frames -> 4 Mono-Samples");
    CHECK(fabsf(wp->data[0]) < 1e-7f, "wav Sample 0 = 0");
    CHECK(fabsf(wp->data[1] - 0.5f) < 1e-7f, "wav Sample 1 = +0.5");
    CHECK(fabsf(wp->data[2] + 1.0f) < 1e-7f, "wav Sample 2 = -1");
    CHECK(fabsf(wp->data[3] + 1.0f / 65536.0f) < 1e-7f,
          "wav Stereo-Mittel exakt");
    CHECK(remove(path) == 0, "Test-WAV entfernt");

    /* 7. Fehlerpfade kehren nach throw defensiv mit none zurueck. */
    fehler_reset();
    MooValue bad_fft = moo_fft(moo_number(1));
    CHECK(moo_error_flag == 1 && bad_fft.tag == MOO_NONE,
          "fft Nicht-Tensor wirft");
    fehler_reset();
    MooValue bad_stft = moo_spektrogramm(input, moo_number(1), moo_number(0));
    CHECK(moo_error_flag == 1 && bad_stft.tag == MOO_NONE,
          "spektrogramm ungueltige Parameter wirft");
    fehler_reset();

    moo_release(path_value);
    moo_release(data);
    moo_release(rate);
    moo_release(wav);
    moo_release(stft);
    moo_release(padded);
    moo_release(short_input);
    moo_release(magnitude);
    moo_release(fft);
    moo_release(input);

    printf("=== AUDIO ASan-Harness: ALLE %d CHECKS BESTANDEN ===\n", checks);
    return 0;
}

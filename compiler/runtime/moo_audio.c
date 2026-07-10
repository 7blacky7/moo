/**
 * moo_audio.c — Audio-Feature-Extraktion fuer KI-MULTI-A1.
 *
 * fft/spektrum: reelle radix-2 FFT, Zero-Padding auf die naechste Zweierpotenz.
 * spektrum_betrag: Betrag des einseitigen Spektrums.
 * spektrogramm: Hann-STFT als [frames, bins].
 * wav_lesen: RIFF/WAVE 16-bit PCM mono/stereo, Stereo wird zu Mono gemittelt.
 *
 * Alle Argumente sind geliehen, Rueckgaben +1 owning. Kein Autograd:
 * Audio-Features und Dateidekodierung sind bewusst nicht differenzierbar.
 */
#include "moo_runtime.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOO_AUDIO_PI 3.14159265358979323846264338327950288

static MooValue audio_fail(const char* message) {
    moo_throw(moo_error(message));
    return moo_none();
}

static MooValue audio_tensor_wrap(MooTensor* tensor) {
    if (!tensor) return moo_none();
    MooValue value;
    value.tag = MOO_TENSOR;
    moo_val_set_ptr(&value, tensor);
    return value;
}

static MooTensor* audio_expect_vector(MooValue value, const char* function_name) {
    if (value.tag != MOO_TENSOR) {
        char message[160];
        snprintf(message, sizeof(message),
                 "%s: Argument muss ein Tensor sein", function_name);
        moo_throw(moo_error(message));
        return NULL;
    }
    MooTensor* tensor = MV_TENSOR(value);
    if (!tensor || tensor->ndim != 1 || tensor->size < 1) {
        char message[192];
        snprintf(message, sizeof(message),
                 "%s: erwarte einen nicht-leeren 1D-Tensor [samples]", function_name);
        moo_throw(moo_error(message));
        return NULL;
    }
    moo_tensor_f32_sichern(tensor);
    if (!tensor->data) {
        char message[160];
        snprintf(message, sizeof(message),
                 "%s: Tensor hat keine lesbaren f32-Daten", function_name);
        moo_throw(moo_error(message));
        return NULL;
    }
    return tensor;
}

static bool audio_number_to_i32(MooValue value, int32_t minimum,
                                const char* name, int32_t* out) {
    if (value.tag != MOO_NUMBER) {
        char message[160];
        snprintf(message, sizeof(message), "%s muss eine Zahl sein", name);
        moo_throw(moo_error(message));
        return false;
    }
    double number = moo_as_number(value);
    if (!isfinite(number) || floor(number) != number ||
        number < (double)minimum || number > (double)INT32_MAX) {
        char message[192];
        snprintf(message, sizeof(message),
                 "%s muss eine ganze Zahl von %d bis %d sein",
                 name, minimum, INT32_MAX);
        moo_throw(moo_error(message));
        return false;
    }
    *out = (int32_t)number;
    return true;
}

static bool audio_next_power_of_two(int64_t requested, int32_t* out) {
    if (requested < 1 || requested > INT32_MAX) {
        moo_throw(moo_error(
            "fft: Sample-Anzahl liegt ausserhalb des unterstuetzten Bereichs"));
        return false;
    }
    uint32_t power = 1u;
    uint32_t target = (uint32_t)requested;
    while (power < target) {
        if (power > (uint32_t)INT32_MAX / 2u) {
            moo_throw(moo_error("fft: Zero-Padding waere zu gross"));
            return false;
        }
        power <<= 1u;
    }
    *out = (int32_t)power;
    return true;
}

static void audio_fft_in_place(double* real, double* imag, int32_t count) {
    for (uint32_t i = 1u, j = 0u; i < (uint32_t)count; i++) {
        uint32_t bit = (uint32_t)count >> 1u;
        while (j & bit) {
            j ^= bit;
            bit >>= 1u;
        }
        j ^= bit;
        if (i < j) {
            double tmp = real[i];
            real[i] = real[j];
            real[j] = tmp;
            tmp = imag[i];
            imag[i] = imag[j];
            imag[j] = tmp;
        }
    }

    for (uint32_t length = 2u; length <= (uint32_t)count; length <<= 1u) {
        double angle = -2.0 * MOO_AUDIO_PI / (double)length;
        double step_real = cos(angle);
        double step_imag = sin(angle);
        uint32_t half = length >> 1u;
        for (uint32_t base = 0u; base < (uint32_t)count; base += length) {
            double w_real = 1.0;
            double w_imag = 0.0;
            for (uint32_t offset = 0u; offset < half; offset++) {
                uint32_t even_index = base + offset;
                uint32_t odd_index = even_index + half;
                double odd_real =
                    real[odd_index] * w_real - imag[odd_index] * w_imag;
                double odd_imag =
                    real[odd_index] * w_imag + imag[odd_index] * w_real;
                double even_real = real[even_index];
                double even_imag = imag[even_index];
                real[even_index] = even_real + odd_real;
                imag[even_index] = even_imag + odd_imag;
                real[odd_index] = even_real - odd_real;
                imag[odd_index] = even_imag - odd_imag;

                double next_real =
                    w_real * step_real - w_imag * step_imag;
                w_imag = w_real * step_imag + w_imag * step_real;
                w_real = next_real;
            }
        }
        if (length == (uint32_t)count) break;
    }
}

static bool audio_fft_vector(MooTensor* input, int32_t fft_count,
                             double** out_real, double** out_imag) {
    double* real = (double*)calloc((size_t)fft_count, sizeof(double));
    double* imag = (double*)calloc((size_t)fft_count, sizeof(double));
    if (!real || !imag) {
        free(real);
        free(imag);
        moo_throw(moo_error("fft: nicht genug Speicher"));
        return false;
    }
    for (int64_t i = 0; i < input->size; i++) {
        real[i] = (double)input->data[i * input->strides[0]];
    }
    audio_fft_in_place(real, imag, fft_count);
    *out_real = real;
    *out_imag = imag;
    return true;
}

MooValue moo_fft(MooValue samples) {
    MooTensor* input = audio_expect_vector(samples, "fft");
    if (!input) return moo_none();

    int32_t fft_count = 0;
    if (!audio_next_power_of_two(input->size, &fft_count)) return moo_none();

    double* real = NULL;
    double* imag = NULL;
    if (!audio_fft_vector(input, fft_count, &real, &imag)) return moo_none();

    int32_t shape[2] = { fft_count / 2 + 1, 2 };
    MooTensor* output = moo_tensor_raw(2, shape);
    if (!output) {
        free(real);
        free(imag);
        return audio_fail("fft: nicht genug Speicher fuer den Ergebnistensor");
    }
    for (int32_t bin = 0; bin < shape[0]; bin++) {
        output->data[(int64_t)bin * 2] = (float)real[bin];
        output->data[(int64_t)bin * 2 + 1] = (float)imag[bin];
    }
    free(real);
    free(imag);
    return audio_tensor_wrap(output);
}

MooValue moo_spektrum_betrag(MooValue samples) {
    MooTensor* input = audio_expect_vector(samples, "spektrum_betrag");
    if (!input) return moo_none();

    int32_t fft_count = 0;
    if (!audio_next_power_of_two(input->size, &fft_count)) return moo_none();

    double* real = NULL;
    double* imag = NULL;
    if (!audio_fft_vector(input, fft_count, &real, &imag)) return moo_none();

    int32_t shape[1] = { fft_count / 2 + 1 };
    MooTensor* output = moo_tensor_raw(1, shape);
    if (!output) {
        free(real);
        free(imag);
        return audio_fail("spektrum_betrag: nicht genug Speicher fuer den Ergebnistensor");
    }
    for (int32_t bin = 0; bin < shape[0]; bin++) {
        output->data[bin] = (float)hypot(real[bin], imag[bin]);
    }
    free(real);
    free(imag);
    return audio_tensor_wrap(output);
}

MooValue moo_spektrogramm(MooValue samples, MooValue fenster,
                          MooValue schritt) {
    MooTensor* input = audio_expect_vector(samples, "spektrogramm");
    if (!input) return moo_none();

    int32_t window_count = 0;
    int32_t hop_count = 0;
    if (!audio_number_to_i32(fenster, 2, "spektrogramm: fenster",
                             &window_count) ||
        !audio_number_to_i32(schritt, 1, "spektrogramm: schritt",
                             &hop_count)) {
        return moo_none();
    }

    int32_t fft_count = 0;
    if (!audio_next_power_of_two(window_count, &fft_count)) return moo_none();
    int64_t frame_count = input->size <= window_count
        ? 1
        : 1 + (input->size - window_count) / hop_count;
    if (frame_count > INT32_MAX) {
        return audio_fail("spektrogramm: zu viele Frames");
    }

    int32_t shape[2] = { (int32_t)frame_count, fft_count / 2 + 1 };
    MooTensor* output = moo_tensor_raw(2, shape);
    if (!output) {
        return audio_fail("spektrogramm: nicht genug Speicher fuer den Ergebnistensor");
    }

    double* real = (double*)calloc((size_t)fft_count, sizeof(double));
    double* imag = (double*)calloc((size_t)fft_count, sizeof(double));
    if (!real || !imag) {
        free(real);
        free(imag);
        MooValue wrapped = audio_tensor_wrap(output);
        moo_release(wrapped);
        return audio_fail("spektrogramm: nicht genug Speicher");
    }

    for (int32_t frame = 0; frame < (int32_t)frame_count; frame++) {
        memset(real, 0, (size_t)fft_count * sizeof(double));
        memset(imag, 0, (size_t)fft_count * sizeof(double));
        int64_t start = (int64_t)frame * hop_count;
        for (int32_t i = 0; i < window_count; i++) {
            int64_t source = start + i;
            double sample = source < input->size
                ? (double)input->data[source * input->strides[0]]
                : 0.0;
            double hann = 0.5 -
                0.5 * cos(2.0 * MOO_AUDIO_PI * (double)i /
                          (double)(window_count - 1));
            real[i] = sample * hann;
        }
        audio_fft_in_place(real, imag, fft_count);
        for (int32_t bin = 0; bin < shape[1]; bin++) {
            output->data[(int64_t)frame * shape[1] + bin] =
                (float)hypot(real[bin], imag[bin]);
        }
    }

    free(real);
    free(imag);
    return audio_tensor_wrap(output);
}

static bool audio_read_u16(FILE* file, uint16_t* out) {
    uint8_t bytes[2];
    if (fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) return false;
    *out = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u);
    return true;
}

static bool audio_read_u32(FILE* file, uint32_t* out) {
    uint8_t bytes[4];
    if (fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) return false;
    *out = (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
    return true;
}

static bool audio_skip(FILE* file, uint32_t bytes) {
    if (bytes > (uint32_t)LONG_MAX) return false;
    return fseek(file, (long)bytes, SEEK_CUR) == 0;
}

MooValue moo_wav_lesen(MooValue pfad) {
    if (pfad.tag != MOO_STRING) {
        return audio_fail("wav_lesen: Pfad muss ein String sein");
    }
    const char* path = MV_STR(pfad)->chars;
    FILE* file = fopen(path, "rb");
    if (!file) {
        char message[320];
        snprintf(message, sizeof(message),
                 "wav_lesen: Datei konnte nicht geoeffnet werden: %s", path);
        return audio_fail(message);
    }

    char riff[4];
    char wave[4];
    uint32_t riff_size = 0;
    if (fread(riff, 1, 4, file) != 4 ||
        !audio_read_u32(file, &riff_size) ||
        fread(wave, 1, 4, file) != 4 ||
        memcmp(riff, "RIFF", 4) != 0 ||
        memcmp(wave, "WAVE", 4) != 0) {
        fclose(file);
        return audio_fail("wav_lesen: keine gueltige RIFF/WAVE-Datei");
    }
    (void)riff_size;

    uint16_t format = 0;
    uint16_t channels = 0;
    uint16_t bits = 0;
    uint32_t rate = 0;
    uint32_t data_size = 0;
    long data_position = -1;
    bool have_format = false;

    for (;;) {
        char chunk_id[4];
        uint32_t chunk_size = 0;
        if (fread(chunk_id, 1, 4, file) != 4) break;
        if (!audio_read_u32(file, &chunk_size)) break;

        if (memcmp(chunk_id, "fmt ", 4) == 0) {
            uint32_t byte_rate = 0;
            uint16_t block_align = 0;
            if (chunk_size < 16 ||
                !audio_read_u16(file, &format) ||
                !audio_read_u16(file, &channels) ||
                !audio_read_u32(file, &rate) ||
                !audio_read_u32(file, &byte_rate) ||
                !audio_read_u16(file, &block_align) ||
                !audio_read_u16(file, &bits) ||
                !audio_skip(file, chunk_size - 16u)) {
                fclose(file);
                return audio_fail("wav_lesen: beschaedigter fmt-Chunk");
            }
            (void)byte_rate;
            (void)block_align;
            have_format = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            data_position = ftell(file);
            data_size = chunk_size;
            if (data_position < 0 || !audio_skip(file, chunk_size)) {
                fclose(file);
                return audio_fail("wav_lesen: beschaedigter data-Chunk");
            }
        } else if (!audio_skip(file, chunk_size)) {
            fclose(file);
            return audio_fail("wav_lesen: beschaedigter RIFF-Chunk");
        }
        if ((chunk_size & 1u) != 0u && fseek(file, 1L, SEEK_CUR) != 0) {
            fclose(file);
            return audio_fail("wav_lesen: fehlendes RIFF-Padding");
        }
        if (have_format && data_position >= 0) break;
    }

    if (!have_format || data_position < 0) {
        fclose(file);
        return audio_fail("wav_lesen: fmt- oder data-Chunk fehlt");
    }
    if (format != 1 || (channels != 1 && channels != 2) || bits != 16 ||
        rate == 0) {
        fclose(file);
        return audio_fail(
            "wav_lesen: unterstuetzt wird nur 16-bit PCM mono oder stereo");
    }

    uint32_t frame_bytes = (uint32_t)channels * 2u;
    if (data_size == 0 || data_size % frame_bytes != 0u) {
        fclose(file);
        return audio_fail("wav_lesen: PCM-Datenlaenge passt nicht zum Format");
    }
    uint32_t sample_count = data_size / frame_bytes;
    if (sample_count > (uint32_t)INT32_MAX ||
        fseek(file, data_position, SEEK_SET) != 0) {
        fclose(file);
        return audio_fail("wav_lesen: Audio ist zu gross oder nicht lesbar");
    }

    int32_t shape[1] = { (int32_t)sample_count };
    MooTensor* tensor = moo_tensor_raw(1, shape);
    if (!tensor) {
        fclose(file);
        return audio_fail("wav_lesen: nicht genug Speicher fuer die PCM-Daten");
    }
    MooValue tensor_value = audio_tensor_wrap(tensor);

    for (uint32_t i = 0; i < sample_count; i++) {
        int32_t sum = 0;
        for (uint16_t channel = 0; channel < channels; channel++) {
            uint16_t encoded = 0;
            if (!audio_read_u16(file, &encoded)) {
                fclose(file);
                moo_release(tensor_value);
                return audio_fail("wav_lesen: PCM-Daten enden zu frueh");
            }
            int32_t signed_sample = encoded >= 32768u
                ? (int32_t)encoded - 65536
                : (int32_t)encoded;
            sum += signed_sample;
        }
        tensor->data[i] =
            (float)((double)sum / ((double)channels * 32768.0));
    }
    fclose(file);

    MooValue result = moo_dict_new();
    moo_dict_set(result, moo_string_new("daten"), tensor_value);
    moo_dict_set(result, moo_string_new("rate"), moo_number((double)rate));
    return result;
}

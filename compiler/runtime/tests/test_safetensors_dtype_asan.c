/**
 * test_safetensors_dtype_asan.c — KIP-D3: Safetensors DType-Vertrag (ASan+UBSan).
 * ============================================================================
 * BAUEN/LAUFEN: run_sanitize.sh (EXTRA_HARNESSES). Quell-Satz wie test_nn_asan.c.
 *
 * VERTRAG (D3):
 *   D3a IMPORT: moo_nn_safetensors liest F32 direkt, BF16 + F16 werden nach f32
 *               konvertiert (moo-Tensoren bleiben f32). Unbekannter dtype wirft.
 *   D3b EXPORT: moo_nn_speichern schreibt f32 (Default, byte-identisch) ODER
 *               bf16 (Env MOO_KI_SPEICHERN_BF16=1) mit korrektem dtype-Header.
 *
 * PRUEFT:
 *   1. F32-Import: Werte bit-exakt.
 *   2. BF16-Import: exakt darstellbare Werte bit-exakt; Decoder-Diskriminator
 *      (0x3C00 als BF16 == 0.0078125, NICHT 1.0) beweist korrekten bf16-Pfad.
 *   3. F16-Import: normale Werte exakt (0x3C00->1.0), subnormal (0x0001->2^-24),
 *      +Inf (0x7C00), NaN (0x7E00).
 *   4. Negativ: dtype "I64"/"F64" wirft erklaerend (moo_error_flag).
 *   5. Byte-Check: falsche data_offsets-Groesse (F32 4B vs BF16 2B) wirft.
 *   6. f32-Export Default: dtype-Header "F32", Daten byte-identisch zu p->data.
 *   7. bf16-Export (Env): dtype-Header "BF16", Reimport == bf16-gerundete
 *      Originale (bit-exakt via Determinismus von moo_f32_zu_bf16).
 * ASan detect_leaks=1: kein Leak in Import/Export/Roundtrip.
 * ============================================================================
 */
#include "../moo_runtime.h"
#include <stdarg.h>
#include <string.h>
#include <math.h>

/* --- Test-throw-Modell (ersetzt moo_error.c) ------------------------------ */
int moo_error_flag = 0;
MooValue moo_last_error;
int moo_try_depth = 0;
jmp_buf moo_try_stack[MOO_TRY_STACK_SIZE];
void moo_throw(MooValue error) {
    if (error.tag == MOO_ERROR) free(moo_val_as_ptr(error));
    moo_error_flag = 1;
}

/* --- Stubs fuer moo_release()-Dispatch-Ziele ------------------------------ */
void moo_socket_free(void* p)       { (void)p; }
void moo_thread_free(void* p)       { (void)p; }
void moo_channel_free(void* p)      { (void)p; }
void moo_db_free(void* p)           { (void)p; }
void moo_db_stmt_free(void* p)      { (void)p; }
void moo_window_free(void* p)       { (void)p; }
void moo_web_free(void* p)          { (void)p; }
void moo_voxel_free(void* p)        { (void)p; }
void moo_frame_free(void* p)        { (void)p; }
void moo_gif_handle_free(void* p)   { (void)p; }
void moo_video_handle_free(void* p) { (void)p; }

static int checks = 0;
#define CHECK(cond, name) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (Zeile %d)\n", name, __LINE__); return 1; } \
    checks++; \
} while (0)

static MooTensor* T(MooValue v) { return MV_TENSOR(v); }

/* moo_dict_get verbraucht die Key-Referenz (Transfer). Rueckgabe ist +1. */
static MooValue dget(MooValue d, const char* key) {
    MooValue k = moo_string_new(key);
    return moo_dict_get(d, k);
}

/* Lokale Referenz-Kodierer (Spiegel der Kanonik) fuer erwartete Werte. */
static uint16_t ref_f32_zu_bf16(float f) {
    uint32_t x; memcpy(&x, &f, sizeof(x));
    if (((x >> 23) & 0xFFu) == 0xFFu && (x & 0x7FFFFFu)) return (uint16_t)((x >> 16) | 0x0040u);
    uint32_t bias = 0x7FFFu + ((x >> 16) & 1u);
    x += bias;
    return (uint16_t)(x >> 16);
}
static float ref_bf16_zu_f32(uint16_t h) {
    uint32_t x = (uint32_t)h << 16; float f; memcpy(&f, &x, sizeof(f)); return f;
}

/* Schreibt eine Ein-Tensor-safetensors-Datei mit rohem 2-Byte-Payload. */
static void schreibe_16bit(const char* pfad, const char* dtype,
                           const uint16_t* daten, int n) {
    char hj[160];
    snprintf(hj, sizeof(hj),
             "{\"x\":{\"dtype\":\"%s\",\"shape\":[%d],\"data_offsets\":[0,%d]}}",
             dtype, n, n * 2);
    uint64_t hl = (uint64_t)strlen(hj);
    FILE* f = fopen(pfad, "wb");
    fwrite(&hl, 8, 1, f);
    fwrite(hj, 1, (size_t)hl, f);
    fwrite(daten, sizeof(uint16_t), (size_t)n, f);
    fclose(f);
}

int main(void) {
    /* ===== 1. F32-Import bit-exakt ===== */
    {
        const char* hj = "{\"w\":{\"dtype\":\"F32\",\"shape\":[2],\"data_offsets\":[0,8]}}";
        uint64_t hl = (uint64_t)strlen(hj);
        FILE* f = fopen("/tmp/d3_f32.safetensors", "wb");
        fwrite(&hl, 8, 1, f); fwrite(hj, 1, (size_t)hl, f);
        float w[2] = { 3.5f, -0.25f };
        fwrite(w, sizeof(float), 2, f); fclose(f);

        MooValue p = moo_string_new("/tmp/d3_f32.safetensors");
        MooValue d = moo_nn_safetensors(p); moo_release(p);
        CHECK(d.tag == MOO_DICT, "F32 import -> Dict");
        MooValue wv = dget(d, "w");
        CHECK(wv.tag == MOO_TENSOR && T(wv)->data[0] == 3.5f &&
              T(wv)->data[1] == -0.25f, "F32 Werte bit-exakt");
        moo_release(wv); moo_release(d);
        remove("/tmp/d3_f32.safetensors");
    }

    /* ===== 2. BF16-Import + Decoder-Diskriminator ===== */
    {
        /* 0x3F80=bf16 1.0, 0xC000=bf16 -2.0, 0x3C00=bf16 0.0078125 (NICHT 1.0!) */
        uint16_t bf[3] = { 0x3F80, 0xC000, 0x3C00 };
        schreibe_16bit("/tmp/d3_bf16.safetensors", "BF16", bf, 3);
        MooValue p = moo_string_new("/tmp/d3_bf16.safetensors");
        MooValue d = moo_nn_safetensors(p); moo_release(p);
        CHECK(d.tag == MOO_DICT, "BF16 import -> Dict");
        MooValue xv = dget(d, "x");
        CHECK(xv.tag == MOO_TENSOR && T(xv)->shape[0] == 3, "BF16 Form [3]");
        CHECK(T(xv)->data[0] == 1.0f, "BF16 0x3F80 -> 1.0");
        CHECK(T(xv)->data[1] == -2.0f, "BF16 0xC000 -> -2.0");
        CHECK(T(xv)->data[2] == ref_bf16_zu_f32(0x3C00) &&
              T(xv)->data[2] == 0.0078125f, "BF16 0x3C00 -> 0.0078125 (Decoder korrekt)");
        moo_release(xv); moo_release(d);
        remove("/tmp/d3_bf16.safetensors");
    }

    /* ===== 3. F16-Import: normal/subnormal/inf/nan ===== */
    {
        /* 0x3C00=1.0, 0xC000=-2.0, 0x3800=0.5, 0x0001=2^-24 (subnormal),
         * 0x7C00=+Inf, 0x7E00=NaN */
        uint16_t h16[6] = { 0x3C00, 0xC000, 0x3800, 0x0001, 0x7C00, 0x7E00 };
        schreibe_16bit("/tmp/d3_f16.safetensors", "F16", h16, 6);
        MooValue p = moo_string_new("/tmp/d3_f16.safetensors");
        MooValue d = moo_nn_safetensors(p); moo_release(p);
        CHECK(d.tag == MOO_DICT, "F16 import -> Dict");
        MooValue xv = dget(d, "x");
        CHECK(xv.tag == MOO_TENSOR && T(xv)->shape[0] == 6, "F16 Form [6]");
        CHECK(T(xv)->data[0] == 1.0f, "F16 0x3C00 -> 1.0");
        CHECK(T(xv)->data[1] == -2.0f, "F16 0xC000 -> -2.0");
        CHECK(T(xv)->data[2] == 0.5f, "F16 0x3800 -> 0.5");
        CHECK(T(xv)->data[3] == ldexpf(1.0f, -24), "F16 0x0001 -> 2^-24 (subnormal)");
        CHECK(isinf(T(xv)->data[4]) && T(xv)->data[4] > 0.0f, "F16 0x7C00 -> +Inf");
        CHECK(isnan(T(xv)->data[5]), "F16 0x7E00 -> NaN");
        moo_release(xv); moo_release(d);
        remove("/tmp/d3_f16.safetensors");
    }

    /* ===== 4. Negativ: unbekannter dtype wirft ===== */
    {
        const char* hj = "{\"x\":{\"dtype\":\"I64\",\"shape\":[1],\"data_offsets\":[0,8]}}";
        uint64_t hl = (uint64_t)strlen(hj);
        FILE* f = fopen("/tmp/d3_i64.safetensors", "wb");
        fwrite(&hl, 8, 1, f); fwrite(hj, 1, (size_t)hl, f);
        int64_t v = 7; fwrite(&v, 8, 1, f); fclose(f);
        moo_error_flag = 0;
        MooValue p = moo_string_new("/tmp/d3_i64.safetensors");
        MooValue d = moo_nn_safetensors(p); moo_release(p);
        CHECK(moo_error_flag == 1 && d.tag == MOO_NONE, "I64 wirft erklaerend");
        moo_error_flag = 0;
        remove("/tmp/d3_i64.safetensors");
    }

    /* ===== 5. Byte-Check: BF16-Header mit F32-Offsets (4B) wirft ===== */
    {
        /* dtype BF16 aber data_offsets [0,4] (=2 Elemente*2B waere [0,4]? nein:
         * shape[1] -> 1 Elem*2B = [0,2]. Wir behaupten [0,4] -> Mismatch). */
        const char* hj = "{\"x\":{\"dtype\":\"BF16\",\"shape\":[1],\"data_offsets\":[0,4]}}";
        uint64_t hl = (uint64_t)strlen(hj);
        FILE* f = fopen("/tmp/d3_bad.safetensors", "wb");
        fwrite(&hl, 8, 1, f); fwrite(hj, 1, (size_t)hl, f);
        uint16_t two[2] = { 0, 0 }; fwrite(two, 2, 2, f); fclose(f);
        moo_error_flag = 0;
        MooValue p = moo_string_new("/tmp/d3_bad.safetensors");
        MooValue d = moo_nn_safetensors(p); moo_release(p);
        CHECK(moo_error_flag == 1 && d.tag == MOO_NONE, "BF16 falsche Offsets werfen");
        moo_error_flag = 0;
        remove("/tmp/d3_bad.safetensors");
    }

    /* ===== 6+7. Export f32 (byte-identisch) + bf16 (Roundtrip) ===== */
    {
        MooValue schichten = moo_list_new(1);
        MooValue akt = moo_string_new("keine");   /* BORROWED von schicht_dicht */
        moo_list_append(schichten,
            moo_nn_schicht_dicht(moo_number(3), moo_number(2), akt, moo_number(7)));
        moo_release(akt);
        MooValue netz = moo_nn_ki_netz(schichten);
        MooValue params = moo_nn_parameter(netz);
        CHECK(params.tag == MOO_LIST && MV_LIST(params)->length >= 1, "Params vorhanden");

        /* --- 6. f32-Default: byte-identisch --- */
        unsetenv("MOO_KI_SPEICHERN_BF16");
        MooValue pf = moo_string_new("/tmp/d3_export_f32.mook");
        moo_release(moo_nn_speichern(netz, pf)); moo_release(pf);
        /* Header enthaelt "F32", nicht "BF16" */
        FILE* f = fopen("/tmp/d3_export_f32.mook", "rb");
        uint64_t hl = 0; fread(&hl, 8, 1, f);
        char* hb = (char*)malloc((size_t)hl + 1);
        fread(hb, 1, (size_t)hl, f); hb[hl] = '\0';
        CHECK(strstr(hb, "\"dtype\":\"F32\"") != NULL &&
              strstr(hb, "BF16") == NULL, "f32-Export Header dtype F32");
        free(hb); fclose(f);
        /* Reimport == Original bit-exakt */
        MooValue rp = moo_string_new("/tmp/d3_export_f32.mook");
        MooValue roh = moo_nn_safetensors(rp); moo_release(rp);
        CHECK(roh.tag == MOO_DICT, "f32-Export reimportiert");
        MooValue p0 = dget(roh, "p0");
        MooTensor* w0 = T(MV_LIST(params)->items[0]);
        bool f32_id = (p0.tag == MOO_TENSOR && T(p0)->size == w0->size);
        for (int64_t j = 0; f32_id && j < w0->size; j++)
            f32_id = T(p0)->data[j] == w0->data[j];
        CHECK(f32_id, "f32-Export Werte bit-identisch");
        moo_release(p0); moo_release(roh);
        remove("/tmp/d3_export_f32.mook");

        /* --- 7. bf16-Export: Header BF16, Reimport == bf16-gerundete Originale --- */
        setenv("MOO_KI_SPEICHERN_BF16", "1", 1);
        MooValue pf2 = moo_string_new("/tmp/d3_export_bf16.mook");
        moo_release(moo_nn_speichern(netz, pf2)); moo_release(pf2);
        unsetenv("MOO_KI_SPEICHERN_BF16");
        FILE* f2 = fopen("/tmp/d3_export_bf16.mook", "rb");
        uint64_t hl2 = 0; fread(&hl2, 8, 1, f2);
        char* hb2 = (char*)malloc((size_t)hl2 + 1);
        fread(hb2, 1, (size_t)hl2, f2); hb2[hl2] = '\0';
        CHECK(strstr(hb2, "\"dtype\":\"BF16\"") != NULL, "bf16-Export Header dtype BF16");
        free(hb2); fclose(f2);
        MooValue rp2 = moo_string_new("/tmp/d3_export_bf16.mook");
        MooValue roh2 = moo_nn_safetensors(rp2); moo_release(rp2);
        CHECK(roh2.tag == MOO_DICT, "bf16-Export reimportiert (->f32)");
        MooValue q0 = dget(roh2, "p0");
        bool bf16_id = (q0.tag == MOO_TENSOR && T(q0)->size == w0->size);
        for (int64_t j = 0; bf16_id && j < w0->size; j++)
            bf16_id = T(q0)->data[j] == ref_bf16_zu_f32(ref_f32_zu_bf16(w0->data[j]));
        CHECK(bf16_id, "bf16-Export == bf16-gerundete Originale (bit-exakt)");
        moo_release(q0); moo_release(roh2);
        remove("/tmp/d3_export_bf16.mook");

        moo_release(params); moo_release(netz); moo_release(schichten);
    }

    printf("test_safetensors_dtype_asan: %d Checks OK\n", checks);
    return 0;
}

/**
 * moo_frame_tensor.c — MOO_FRAME ↔ Tensor-Brücke (KI-MULTI-V1).
 *
 * tensor_aus_frame(frame[, "grau"]) -> Tensor [h, b, 4] bzw. [h, b, 1],
 *   float 0..1 (RGBA/255, top-left origin wie die Frame-Konvention).
 *   Luminanz-Formel: 0.299*R + 0.587*G + 0.114*B (Rec. 601).
 * frame_aus_tensor(t) -> MOO_FRAME (Clamp 0..1 -> RGBA8). Akzeptiert
 *   [h,b,4], [h,b,3] (Alpha=255), [h,b,1] und [h,b] (Grau).
 *
 * SDL-frei und IMMER Teil des Builds (wie moo_frame.c/moo_tensor.c) —
 * eigene Datei, damit der test_frame_asan-Harness (ohne Tensor-Familie)
 * unberührt bleibt; die Brücke hat ihren eigenen Harness
 * (tests/test_frame_tensor_asan.c).
 *
 * Kein Autograd: reine Feature-Extraktion/Ausgabe (nicht differenzierbar
 * durch die Byte-Quantisierung) — bewusst KEIN Registry-Op.
 */

#include "moo_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern MooValue moo_none(void);
extern void     moo_throw(MooValue v);
extern MooValue moo_error(const char* msg);

static float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

MooValue moo_tensor_aus_frame(MooValue frame, MooValue modus) {
    if (frame.tag != MOO_FRAME) {
        moo_throw(moo_error("tensor_aus_frame: erstes Argument muss ein Frame sein (test_frame_grab liefert einen)"));
        return moo_none();
    }
    const MooFrame* f = MV_FRAME(frame);
    if (!f || !f->pixels || f->width <= 0 || f->height <= 0) {
        moo_throw(moo_error("tensor_aus_frame: ungueltiger Frame"));
        return moo_none();
    }
    bool grau = false;
    if (modus.tag == MOO_STRING) {
        const char* m = MV_STR(modus)->chars;
        if (strcmp(m, "grau") == 0 || strcmp(m, "gray") == 0 || strcmp(m, "grey") == 0) {
            grau = true;
        } else {
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "tensor_aus_frame: unbekannter Modus \"%s\" (erlaubt: \"grau\")", m);
            moo_throw(moo_error(msg));
            return moo_none();
        }
    } else if (modus.tag != MOO_NONE) {
        moo_throw(moo_error("tensor_aus_frame: Modus muss ein String sein (z.B. \"grau\")"));
        return moo_none();
    }

    int32_t shape[3] = { f->height, f->width, grau ? 1 : 4 };
    MooTensor* t = moo_tensor_raw(3, shape);
    if (!t) {
        moo_throw(moo_error("tensor_aus_frame: nicht genug Speicher"));
        return moo_none();
    }
    const float inv = 1.0f / 255.0f;
    for (int32_t y = 0; y < f->height; y++) {
        const uint8_t* zeile = f->pixels + (size_t)y * (size_t)f->stride;
        for (int32_t x = 0; x < f->width; x++) {
            const uint8_t* px = zeile + (size_t)x * 4;
            if (grau) {
                float lum = (0.299f * (float)px[0] + 0.587f * (float)px[1] + 0.114f * (float)px[2]) * inv;
                t->data[(int64_t)y * f->width + x] = lum;
            } else {
                int64_t base = ((int64_t)y * f->width + x) * 4;
                t->data[base + 0] = (float)px[0] * inv;
                t->data[base + 1] = (float)px[1] * inv;
                t->data[base + 2] = (float)px[2] * inv;
                t->data[base + 3] = (float)px[3] * inv;
            }
        }
    }
    MooValue v;
    v.tag = MOO_TENSOR;
    moo_val_set_ptr(&v, t);
    return v;
}

MooValue moo_frame_aus_tensor(MooValue tv) {
    if (tv.tag != MOO_TENSOR) {
        moo_throw(moo_error("frame_aus_tensor: Argument muss ein Tensor sein (Form [hoehe, breite, kanaele])"));
        return moo_none();
    }
    MooTensor* t = MV_TENSOR(tv);
    moo_tensor_f32_sichern(t);  /* materialisiert bf16-store/GPU-residente Daten */
    if (!t || !t->data) {
        moo_throw(moo_error("frame_aus_tensor: Tensor hat keine f32-Daten"));
        return moo_none();
    }
    int32_t h = 0, w = 0, c = 1;
    int64_t sy = 0, sx = 0, sc = 0;
    if (t->ndim == 3) {
        h = t->shape[0]; w = t->shape[1]; c = t->shape[2];
        sy = t->strides[0]; sx = t->strides[1]; sc = t->strides[2];
    } else if (t->ndim == 2) {
        h = t->shape[0]; w = t->shape[1]; c = 1;
        sy = t->strides[0]; sx = t->strides[1]; sc = 0;
    } else {
        moo_throw(moo_error("frame_aus_tensor: erwarte Form [hoehe, breite] oder [hoehe, breite, kanaele]"));
        return moo_none();
    }
    if (c != 1 && c != 3 && c != 4) {
        moo_throw(moo_error("frame_aus_tensor: letzte Dimension muss 1, 3 oder 4 Kanaele haben"));
        return moo_none();
    }
    if (h <= 0 || w <= 0 || h > 65535 || w > 65535) {
        moo_throw(moo_error("frame_aus_tensor: Frame-Dimension ausserhalb 1..65535"));
        return moo_none();
    }
    uint8_t* px = (uint8_t*)malloc((size_t)w * (size_t)h * 4);
    if (!px) {
        moo_throw(moo_error("frame_aus_tensor: nicht genug Speicher"));
        return moo_none();
    }
    for (int32_t y = 0; y < h; y++) {
        for (int32_t x = 0; x < w; x++) {
            const float* q = t->data + (int64_t)y * sy + (int64_t)x * sx;
            uint8_t* ziel = px + ((size_t)y * (size_t)w + (size_t)x) * 4;
            if (c == 1) {
                uint8_t g = (uint8_t)(clamp01(q[0]) * 255.0f + 0.5f);
                ziel[0] = g; ziel[1] = g; ziel[2] = g; ziel[3] = 255;
            } else {
                ziel[0] = (uint8_t)(clamp01(q[0 * sc]) * 255.0f + 0.5f);
                ziel[1] = (uint8_t)(clamp01(q[1 * sc]) * 255.0f + 0.5f);
                ziel[2] = (uint8_t)(clamp01(q[2 * sc]) * 255.0f + 0.5f);
                ziel[3] = (c == 4) ? (uint8_t)(clamp01(q[3 * sc]) * 255.0f + 0.5f) : 255;
            }
        }
    }
    MooValue frame = moo_frame_new_take(w, h, px); /* uebernimmt px */
    if (frame.tag != MOO_FRAME) {
        moo_throw(moo_error("frame_aus_tensor: Frame-Erzeugung fehlgeschlagen"));
        return moo_none();
    }
    return frame;
}

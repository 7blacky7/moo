/**
 * moo_frame.c — Opaker Pixel-Frame-Heap-Typ (Plan-008 A3A).
 *
 * MOO_FRAME ist ein refcounteter Heap-Typ fuer Framebuffer-Pixeldaten. Pixel
 * werden NIEMALS als moo-Liste gehalten (MooValue=16B; ein Ultrawide-Frame
 * 3440x1440x4 ~= 20 MB — das wuerde als Liste ~320 MB belegen und das GC-/
 * Refcount-System ueberlasten). Stattdessen ein einziger malloc'ter uint8_t*-
 * Block.
 *
 * STANDARDISIERTES FORMAT (backend-uebergreifend identisch):
 *   - format MOO_FRAME_FMT_RGBA8 (=0): 4 Bytes/Pixel R,G,B,A
 *   - top-left origin (Pixel (0,0) ist oben links)
 *   - stride = width*4 (dicht gepackt)
 * Der Y-Flip und ggf. BGRA->RGBA-Swizzle wird IM Backend-Grab erledigt
 * (moo_test_api.c / die grab_rgba-Vtable), sodass dieser File rein
 * format-agnostisch mit RGBA8-top-left arbeitet.
 *
 * Dieser File ist IMMER Teil des Builds (auch Non-3D), weil moo_memory.c den
 * Release-Dispatch (moo_frame_free) braucht. Die backend-spezifische
 * test_frame_grab-Implementierung lebt dagegen in moo_test_api.c (nur 3D-Build).
 *
 * Refcount-Konvention: refcount ist das ERSTE Struct-Feld (siehe MooFrame in
 * moo_runtime.h). moo_frame_new_take() startet bei refcount=1, moo_release()
 * dekrementiert und ruft bei 0 moo_frame_free().
 */

#include "moo_runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Forward-Decls (in moo_value.c / moo_dict.c / moo_string.c definiert) ---- */
extern MooValue moo_number(double n);
extern MooValue moo_bool(bool b);
extern MooValue moo_string_new(const char* s);
extern MooValue moo_none(void);
extern void     moo_throw(MooValue v);
extern MooValue moo_dict_new(void);
extern void     moo_dict_set(MooValue dict, MooValue key, MooValue value);

/* ============================================================
 * Konstruktor + Destruktor
 * ============================================================ */

/* Uebernimmt den uebergebenen RGBA8-top-left-Buffer (take ownership). Der
 * Buffer MUSS width*height*4 Bytes gross und via malloc allokiert sein. Bei
 * pixels==NULL oder ungueltigen Dims wird der Buffer freigegeben und NONE
 * zurueckgegeben (kein halbinitialisierter Frame). */
MooValue moo_frame_new_take(int width, int height, uint8_t* rgba_pixels_top_left) {
    if (!rgba_pixels_top_left || width <= 0 || height <= 0) {
        free(rgba_pixels_top_left);
        return moo_none();
    }
    MooFrame* f = (MooFrame*)malloc(sizeof(MooFrame));
    if (!f) {
        free(rgba_pixels_top_left);
        return moo_none();
    }
    f->refcount = 1;
    f->width    = width;
    f->height   = height;
    f->format   = MOO_FRAME_FMT_RGBA8;
    f->stride   = width * 4;
    f->pixels   = rgba_pixels_top_left;

    MooValue v;
    v.tag = MOO_FRAME;
    moo_val_set_ptr(&v, f);
    return v;
}

void moo_frame_free(void* ptr) {
    if (!ptr) return;
    MooFrame* f = (MooFrame*)ptr;
    if (f->pixels) free(f->pixels);
    f->pixels = NULL;
    free(f);
}

/* ============================================================
 * Pixel-Lesen  ->  Dict { rot, gruen, blau, alpha }  (0..255)
 * ============================================================
 * NUR fuer ein direktes MOO_FRAME. x/y sind ganzzahlige Pixelkoordinaten,
 * top-left origin. Out-of-bounds / Nicht-Frame -> throw. Der Fenster-Pfad
 * (kurz grabben) liegt in moo_test_api.c (moo_test_pixel), da er die nur im
 * 3D-Build verfuegbare Grab-Infrastruktur braucht.
 */
MooValue moo_frame_pixel_dict(const MooFrame* f, int x, int y) {
    MooValue d = moo_dict_new();
    size_t off = (size_t)y * (size_t)f->stride + (size_t)x * 4;
    uint8_t r = f->pixels[off + 0];
    uint8_t g = f->pixels[off + 1];
    uint8_t b = f->pixels[off + 2];
    uint8_t a = f->pixels[off + 3];
    moo_dict_set(d, moo_string_new("rot"),   moo_number((double)r));
    moo_dict_set(d, moo_string_new("gruen"), moo_number((double)g));
    moo_dict_set(d, moo_string_new("blau"),  moo_number((double)b));
    moo_dict_set(d, moo_string_new("alpha"), moo_number((double)a));
    return d;
}

MooValue moo_frame_read_pixel(MooValue frame, MooValue x, MooValue y) {
    if (frame.tag != MOO_FRAME) {
        moo_throw(moo_string_new("test_pixel: kein gueltiger Frame"));
        return moo_none();
    }
    if (x.tag != MOO_NUMBER || y.tag != MOO_NUMBER) {
        moo_throw(moo_string_new("test_pixel: x und y muessen Zahlen sein"));
        return moo_none();
    }
    MooFrame* f = MV_FRAME(frame);
    if (!f || !f->pixels) {
        moo_throw(moo_string_new("test_pixel: ungueltiger Frame"));
        return moo_none();
    }
    int ix = (int)MV_NUM(x);
    int iy = (int)MV_NUM(y);
    if (ix < 0 || iy < 0 || ix >= f->width || iy >= f->height) {
        moo_throw(moo_string_new("test_pixel: Koordinate ausserhalb des Frames"));
        return moo_none();
    }
    return moo_frame_pixel_dict(f, ix, iy);
}

/* ============================================================
 * Frame -> BMP (24bpp, bottom-up). SDL-unabhaengig, damit dieser File auch
 * im Non-3D-Build linkbar bleibt. Eingang ist RGBA8 top-left.
 * ============================================================ */
static void put_le32(unsigned char* p, uint32_t v) {
    /* Little-Endian-Bytes; unsigned -> definiertes Wrap/Shift (ub-policy). */
    p[0] = (unsigned char)(v & 0xFFu);
    p[1] = (unsigned char)((v >> 8) & 0xFFu);
    p[2] = (unsigned char)((v >> 16) & 0xFFu);
    p[3] = (unsigned char)((v >> 24) & 0xFFu);
}

MooValue moo_test_frame_save_bmp(MooValue frame, MooValue pfad) {
    if (frame.tag != MOO_FRAME) {
        moo_throw(moo_string_new("test_frame_save_bmp: erstes Argument ist kein Frame"));
        return moo_bool(false);
    }
    if (pfad.tag != MOO_STRING) {
        moo_throw(moo_string_new("test_frame_save_bmp: Pfad muss ein String sein"));
        return moo_bool(false);
    }
    MooFrame* f = MV_FRAME(frame);
    if (!f || !f->pixels || f->width <= 0 || f->height <= 0) {
        moo_throw(moo_string_new("test_frame_save_bmp: ungueltiger Frame"));
        return moo_bool(false);
    }

    int w = f->width;
    int h = f->height;
    /* Zeilen-Padding auf 4-Byte-Grenze (BMP-Pflicht). unsigned-Arithmetik. */
    uint32_t row_bytes  = (uint32_t)w * 3u;
    uint32_t row_pad    = (4u - (row_bytes % 4u)) % 4u;
    uint32_t row_padded = row_bytes + row_pad;
    uint32_t data_size  = row_padded * (uint32_t)h;
    uint32_t file_size  = 54u + data_size;

    FILE* fp = fopen(MV_STR(pfad)->chars, "wb");
    if (!fp) {
        moo_throw(moo_string_new("test_frame_save_bmp: Datei nicht schreibbar"));
        return moo_bool(false);
    }

    unsigned char hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    put_le32(hdr + 2, file_size);
    put_le32(hdr + 10, 54u);          /* pixel-data offset */
    put_le32(hdr + 14, 40u);          /* DIB header size */
    put_le32(hdr + 18, (uint32_t)w);
    put_le32(hdr + 22, (uint32_t)h);  /* positiv = bottom-up */
    hdr[26] = 1;                       /* planes */
    hdr[28] = 24;                      /* bpp */
    put_le32(hdr + 34, data_size);
    fwrite(hdr, 1, 54, fp);

    unsigned char* row = (unsigned char*)malloc(row_padded);
    if (!row) {
        fclose(fp);
        moo_throw(moo_string_new("test_frame_save_bmp: Speicher fehlgeschlagen"));
        return moo_bool(false);
    }
    memset(row, 0, row_padded);

    /* Frame ist top-down RGBA; BMP ist bottom-up BGR -> Reihen umkehren. */
    for (int y = h - 1; y >= 0; y--) {
        const uint8_t* sp = f->pixels + (size_t)y * (size_t)f->stride;
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = sp[x * 4 + 2]; /* B */
            row[x * 3 + 1] = sp[x * 4 + 1]; /* G */
            row[x * 3 + 2] = sp[x * 4 + 0]; /* R */
        }
        fwrite(row, 1, row_padded, fp);
    }

    free(row);
    fclose(fp);
    return moo_bool(true);
}
